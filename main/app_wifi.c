#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_event.h>
#include <nvs.h>
#include "app_wifi.h"
#include "bsp_rtc.h"

#define TAG "app_wifi"

/* ---- 超时（ms）---- */
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define SNTP_SYNC_TIMEOUT_MS     20000
#define STA_START_TIMEOUT_MS     5000

/* ---- 凭据 NVS 存储：命名空间 wifi，槽位 cred0..cred7，seq 递增淘汰最旧 ---- */
#define CREDS_NS         "wifi"
#define CREDS_MAX        8
#define CREDS_SEQ_KEY    "seq"

typedef struct {
    char ssid[33];
    char pwd[65];
    uint32_t seq;
} wifi_cred_t;

typedef enum {
    WIFI_CMD_SCAN = 0,
    WIFI_CMD_CONNECT,
} wifi_cmd_t;

typedef struct {
    wifi_cmd_t type;
    char ssid[33];
    char pwd[65];       /* 开放网络为空串 */
    bool use_saved;     /* true = 忽略 pwd，用 NVS 凭据 */
} wifi_cmd_msg_t;

static app_wifi_status_t s_status = {
    .state = APP_WIFI_STATE_IDLE,
    .status_text = "",
    .ap_count = 0,
};

/* ---- 内部状态 ---- */
static bool s_worker_started = false;
static bool s_netif_ready = false;     /* netif + 事件循环，进程生命周期内只建一次 */
static bool s_wifi_up = false;         /* esp_wifi init+start，每次对时后 deinit */
static QueueHandle_t s_cmd_queue = NULL;
static EventGroupHandle_t s_wifi_events = NULL;

#define EVT_STA_STARTED   (1 << 0)
#define EVT_GOT_IP        (1 << 1)
#define EVT_CONNECT_FAIL  (1 << 2)

static int s_disconnect_reason = 0;

/* ============ 凭据存取 ============ */

static uint32_t creds_next_seq(nvs_handle_t nvs)
{
    uint32_t seq = 0;
    nvs_get_u32(nvs, CREDS_SEQ_KEY, &seq);
    seq += 1;
    nvs_set_u32(nvs, CREDS_SEQ_KEY, seq);
    return seq;
}

bool app_wifi_load_password(const char *ssid, char *pwd_out, size_t len)
{
    if (ssid == NULL || pwd_out == NULL || len == 0) {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(CREDS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    bool found = false;
    char key[16];
    wifi_cred_t cred;
    for (int i = 0; i < CREDS_MAX; i++) {
        snprintf(key, sizeof(key), "cred%d", i);
        size_t len_out = sizeof(cred);
        if (nvs_get_blob(nvs, key, &cred, &len_out) == ESP_OK &&
            strlen(cred.ssid) > 0 && strcmp(cred.ssid, ssid) == 0) {
            strlcpy(pwd_out, cred.pwd, len);
            found = true;
            break;
        }
    }
    nvs_close(nvs);
    return found;
}

static bool creds_has(const char *ssid)
{
    char dummy[2];
    return app_wifi_load_password(ssid, dummy, sizeof(dummy));
}

/* 连接成功（GOT_IP）后调用：同 SSID 更新密码不占新槽；满 8 条淘汰 seq 最小者 */
static void creds_save(const char *ssid, const char *pwd)
{
    nvs_handle_t nvs;
    if (nvs_open(CREDS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "creds save: nvs open failed");
        return;
    }

    char key[16];
    wifi_cred_t cred;
    int free_slot = -1;
    int victim = -1;
    uint32_t min_seq = UINT32_MAX;

    for (int i = 0; i < CREDS_MAX; i++) {
        snprintf(key, sizeof(key), "cred%d", i);
        size_t len_out = sizeof(cred);
        if (nvs_get_blob(nvs, key, &cred, &len_out) != ESP_OK) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }
        if (strlen(cred.ssid) > 0 && strcmp(cred.ssid, ssid) == 0) {
            /* 已存在：只更新密码 */
            strlcpy(cred.pwd, pwd, sizeof(cred.pwd));
            nvs_set_blob(nvs, key, &cred, sizeof(cred));
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "creds: updated password for %s", ssid);
            return;
        }
        if (cred.seq < min_seq) {
            min_seq = cred.seq;
            victim = i;
        }
    }

    int slot = (free_slot >= 0) ? free_slot : victim;
    if (slot < 0) {
        nvs_close(nvs);
        return;
    }
    snprintf(key, sizeof(key), "cred%d", slot);
    memset(&cred, 0, sizeof(cred));
    strlcpy(cred.ssid, ssid, sizeof(cred.ssid));
    strlcpy(cred.pwd, pwd, sizeof(cred.pwd));
    cred.seq = creds_next_seq(nvs);
    nvs_set_blob(nvs, key, &cred, sizeof(cred));
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "creds: saved %s to slot %d", ssid, slot);
}

static void creds_delete(const char *ssid)
{
    nvs_handle_t nvs;
    if (nvs_open(CREDS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    char key[16];
    wifi_cred_t cred;
    for (int i = 0; i < CREDS_MAX; i++) {
        snprintf(key, sizeof(key), "cred%d", i);
        size_t len_out = sizeof(cred);
        if (nvs_get_blob(nvs, key, &cred, &len_out) == ESP_OK &&
            strlen(cred.ssid) > 0 && strcmp(cred.ssid, ssid) == 0) {
            nvs_erase_key(nvs, key);
            nvs_commit(nvs);
            ESP_LOGI(TAG, "creds: deleted stale password for %s", ssid);
            break;
        }
    }
    nvs_close(nvs);
}

/* ============ WiFi 栈生命周期 ============ */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_events, EVT_STA_STARTED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_disconnect_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        xEventGroupSetBits(s_wifi_events, EVT_CONNECT_FAIL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, EVT_GOT_IP);
    }
}

static esp_err_t ensure_wifi_stack(void)
{
    esp_err_t ret;
    if (!s_netif_ready) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        /* handler 挂在 default event loop（进程生命周期），只注册一次：
         * 若放在 WiFi 栈分支，每次对时 teardown 后重建都会重复注册累积泄漏 */
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
        s_netif_ready = true;
    }
    if (s_wifi_up) {
        return ESP_OK;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* 等 STA_START（5s）*/
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, EVT_STA_STARTED, pdTRUE, pdTRUE, pdMS_TO_TICKS(STA_START_TIMEOUT_MS));
    if (!(bits & EVT_STA_STARTED)) {
        ESP_LOGE(TAG, "wifi start timeout");
        return ESP_ERR_TIMEOUT;
    }
    s_wifi_up = true;
    return ESP_OK;
}

/* 每次对时结束（DONE/FAILED 一律）调用：彻底断开并释放 WiFi 堆存 */
static void teardown_wifi_stack(void)
{
    esp_netif_sntp_deinit();
    if (s_wifi_up) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_up = false;
        ESP_LOGI(TAG, "wifi deinit done (~50KB heap released)");
    }
}

/* ============ 状态文本 ============ */

static void set_status(app_wifi_state_t st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_status(app_wifi_state_t st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status.status_text, sizeof(s_status.status_text), fmt, ap);
    va_end(ap);
    s_status.state = st;
    ESP_LOGI(TAG, "[%s] %s",
             st == APP_WIFI_STATE_DONE ? "DONE" :
             st == APP_WIFI_STATE_FAILED ? "FAILED" : "busy", s_status.status_text);
}

/* ============ SNTP → RTC ============ */

static bool sntp_sync_and_set_rtc(void)
{
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "pool.ntp.org", "time.google.com"));
    esp_err_t ret = esp_netif_sntp_init(&sntp_cfg);
    if (ret != ESP_OK) {
        return false;
    }
    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return false;
    }

    setenv("TZ", "CST-8", 1);
    tzset();
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int year = (tm_now.tm_year + 1900) % 100;
    ret = bsp_rtc_set_datetime(year, tm_now.tm_mon + 1, tm_now.tm_mday,
                               tm_now.tm_wday,               /* 0=周日，与 PCF85063 编码一致 */
                               tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    if (ret != ESP_OK) {
        return false;
    }

    /* 回读校验 */
    uint8_t hh, mm, ss, dd, wd, mo, yy;
    if (bsp_rtc_get_datetime(&hh, &mm, &ss, &dd, &wd, &mo, &yy) == ESP_OK) {
        ESP_LOGI(TAG, "RTC SET OK: 20%02d-%02d-%02d %02d:%02d:%02d", yy, mo, dd, hh, mm, ss);
    }
    return true;
}

/* ============ worker ============ */

/* 认证类失败：密码错误/握手失败，需删除失效凭据。
 * 覆盖 WPA2 与 WPA3/SAE 场景（手机热点改密码后旧凭据常见 203/204/205），
 * 宁可误删重输一次密码也不让坏凭据锁死网络。 */
static bool reason_is_auth_fail(int reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:              /* 202 */
    case WIFI_REASON_ASSOC_FAIL:             /* 203 */
    case WIFI_REASON_HANDSHAKE_TIMEOUT:      /* 204 */
    case WIFI_REASON_CONNECTION_FAIL:        /* 205 */
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: /* 15 */
    case WIFI_REASON_802_1X_AUTH_FAILED:     /* 23 */
        return true;
    default:
        return false;
    }
}

static void do_scan(void)
{
    set_status(APP_WIFI_STATE_SCANNING, "Scanning...");
    if (ensure_wifi_stack() != ESP_OK) {
        set_status(APP_WIFI_STATE_FAILED, "WiFi init failed");
        teardown_wifi_stack();
        return;
    }

    wifi_scan_config_t scan_cfg = { 0 };
    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true /* blocking */);
    if (ret != ESP_OK) {
        set_status(APP_WIFI_STATE_FAILED, "Scan failed");
        teardown_wifi_stack();
        return;
    }

    uint16_t ap_num = 20;
    wifi_ap_record_t recs[20];
    ret = esp_wifi_scan_get_ap_records(&ap_num, recs);
    if (ret != ESP_OK) {
        set_status(APP_WIFI_STATE_FAILED, "Scan read failed");
        teardown_wifi_stack();
        return;
    }

    /* esp_wifi 默认按 RSSI 降序返回；同名网络只留第一个 */
    size_t n = 0;
    for (uint16_t i = 0; i < ap_num && n < 20; i++) {
        bool dup = false;
        for (size_t j = 0; j < n; j++) {
            if (strncmp(s_status.ap_list[j].ssid, (const char *)recs[i].ssid, sizeof(s_status.ap_list[j].ssid)) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        strlcpy(s_status.ap_list[n].ssid, (const char *)recs[i].ssid, sizeof(s_status.ap_list[n].ssid));
        s_status.ap_list[n].rssi = recs[i].rssi;
        s_status.ap_list[n].auth_open = (recs[i].authmode == WIFI_AUTH_OPEN);
        s_status.ap_list[n].saved = creds_has(s_status.ap_list[n].ssid);
        ESP_LOGI(TAG, "AP[%d] %s (%d dBm, %s%s)", (int)n, s_status.ap_list[n].ssid,
                 recs[i].rssi, s_status.ap_list[n].auth_open ? "open" : "secured",
                 s_status.ap_list[n].saved ? ", saved" : "");
        n++;
    }
    s_status.ap_count = n;
    set_status(APP_WIFI_STATE_IDLE, n ? "Found %d networks" : "No networks found", (int)n);
    teardown_wifi_stack();
}

static void do_connect(const char *ssid, const char *pwd /* NULL=用凭据, ""=开放 */)
{
    char effective_pwd[65] = {0};
    bool used_saved = false;
    if (pwd == NULL) {
        if (!app_wifi_load_password(ssid, effective_pwd, sizeof(effective_pwd))) {
            set_status(APP_WIFI_STATE_FAILED, "No saved password");
            return;
        }
        used_saved = true;
    } else {
        strlcpy(effective_pwd, pwd, sizeof(effective_pwd));
    }

    set_status(APP_WIFI_STATE_CONNECTING, "Connecting %s%s...", ssid, used_saved ? " (saved)" : "");
    if (ensure_wifi_stack() != ESP_OK) {
        set_status(APP_WIFI_STATE_FAILED, "WiFi init failed");
        teardown_wifi_stack();
        return;
    }

    wifi_config_t sta_cfg = { 0 };
    /* SSID 字段 32 字节且不要求 NUL 终止：strlcpy 会把满长 32 字节 SSID
     * 截成 31 字节导致 NO_AP_FOUND，必须按长度 memcpy（密码字段带 NUL 语义不受影响） */
    size_t ssid_len = strnlen(ssid, sizeof(sta_cfg.sta.ssid));
    memcpy(sta_cfg.sta.ssid, ssid, ssid_len);
    strlcpy((char *)sta_cfg.sta.password, effective_pwd, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (ret != ESP_OK) {
        set_status(APP_WIFI_STATE_FAILED, "Config failed");
        teardown_wifi_stack();
        return;
    }

    xEventGroupClearBits(s_wifi_events, EVT_GOT_IP | EVT_CONNECT_FAIL);
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        set_status(APP_WIFI_STATE_FAILED, "Connect cmd failed");
        teardown_wifi_stack();
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, EVT_GOT_IP | EVT_CONNECT_FAIL,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (!(bits & EVT_GOT_IP)) {
        /* 认证失败且用的是保存的凭据 → 删除失效密码 */
        if (reason_is_auth_fail(s_disconnect_reason)) {
            set_status(APP_WIFI_STATE_FAILED, "Wrong password");
            if (used_saved) {
                creds_delete(ssid);
            }
        } else if (bits & EVT_CONNECT_FAIL) {
            set_status(APP_WIFI_STATE_FAILED, "Connect failed (%d)", s_disconnect_reason);
        } else {
            set_status(APP_WIFI_STATE_FAILED, "Connect timeout");
        }
        teardown_wifi_stack();
        return;
    }

    /* 连接成功：保存凭据（失败密码不落盘） */
    creds_save(ssid, effective_pwd);

    set_status(APP_WIFI_STATE_SYNCING, "Syncing time...");
    if (!sntp_sync_and_set_rtc()) {
        set_status(APP_WIFI_STATE_FAILED, "Time sync failed");
        teardown_wifi_stack();
        return;
    }
    {
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        set_status(APP_WIFI_STATE_DONE, "Sync OK %02d:%02d:%02d",
                   tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    }
    teardown_wifi_stack();
}

static void app_wifi_worker(void *arg)
{
    wifi_cmd_msg_t msg;
    while (1) {
        if (xQueueReceive(s_cmd_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (msg.type) {
        case WIFI_CMD_SCAN:
            do_scan();
            break;
        case WIFI_CMD_CONNECT:
            do_connect(msg.ssid, msg.use_saved ? NULL : msg.pwd);
            break;
        }
    }
}

/* ============ 公共 API ============ */

void app_wifi_ensure_started(void)
{
    if (s_worker_started) {
        return;
    }
    s_cmd_queue = xQueueCreate(4, sizeof(wifi_cmd_msg_t));
    assert(s_cmd_queue != NULL);
    s_wifi_events = xEventGroupCreate();
    assert(s_wifi_events != NULL);
    assert(xTaskCreate(app_wifi_worker, "app_wifi", 4096, NULL, 4, NULL) == pdPASS);
    s_worker_started = true;
    ESP_LOGI(TAG, "app_wifi worker started");
}

const app_wifi_status_t *app_wifi_get_status(void)
{
    return &s_status;
}

void app_wifi_cmd_scan(void)
{
    app_wifi_ensure_started();
    wifi_cmd_msg_t msg = { .type = WIFI_CMD_SCAN };
    xQueueSend(s_cmd_queue, &msg, 0);
}

void app_wifi_cmd_connect(const char *ssid, const char *password)
{
    app_wifi_ensure_started();
    wifi_cmd_msg_t msg = { .type = WIFI_CMD_CONNECT, .use_saved = (password == NULL) };
    strlcpy(msg.ssid, ssid, sizeof(msg.ssid));
    strlcpy(msg.pwd, password ? password : "", sizeof(msg.pwd));
    xQueueSend(s_cmd_queue, &msg, 0);
}

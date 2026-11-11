#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "hal/hal_wifi.h"

#define TAG "hal_wifi"

/* 扫描结果条数上限：16 条 × 最坏 ~100B（SSID 全转义）≈ 1.1KB，适配 console resp 缓冲 */
#define WIFI_SCAN_MAX_AP 16

/* 当前目标凭据：仅 gui_task（apply_pending）写入；event handler 只读 s_want_connect */
static char s_cur_ssid[33];
static char s_cur_pass[65];
static bool s_want_connect = false;
static wifi_ap_record_t s_ap_records[WIFI_SCAN_MAX_AP]; /* 静态缓冲，避免每次扫描堆分配 */

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_want_connect) {
            /* 凭据存在即重连（时钟设备常驻；驱动内部带退避） */
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = event_data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

void hal_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() == NULL ? ESP_FAIL : ESP_OK);
    ESP_ERROR_CHECK(esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_start()); /* 只启动不连接：扫描无需凭据，连接由 apply_credentials 发起 */
}

void hal_wifi_apply_credentials(const char *ssid, const char *pass)
{
    if (ssid[0] == '\0') {
        if (s_want_connect) {
            s_want_connect = false;
            s_cur_ssid[0] = '\0';
            s_cur_pass[0] = '\0';
            esp_wifi_disconnect();
        }
        return;
    }
    if (s_want_connect && strcmp(ssid, s_cur_ssid) == 0 && strcmp(pass, s_cur_pass) == 0) {
        return; /* 凭据未变：连接与断线重连由事件机制维护 */
    }

    snprintf(s_cur_ssid, sizeof(s_cur_ssid), "%s", ssid);
    snprintf(s_cur_pass, sizeof(s_cur_pass), "%s", pass);
    s_want_connect = true;

    /* authmode 阈值保持默认 OPEN：兼容开放网络与 WPA2/WPA3 */
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    /* 覆盖三种状态：空闲——disconnect 无操作（返回值不可靠，不依赖），connect 发起；
     * 已连接/连接中——disconnect 触发事件，handler 以新凭据重连（此处 connect 报错无害） */
    (void)esp_wifi_disconnect();
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "esp_wifi_connect: %s (reconnect via disconnect event)", esp_err_to_name(err));
    }
}

/* SSID 可安全拼入 JSON：非空且无控制字符（隐藏 AP 已由 show_hidden=false 过滤，
 * 此处兜底空 SSID；含控制字符的 SSID 直接跳过，极罕见） */
static bool ssid_json_safe(const char *ssid)
{
    if (ssid[0] == '\0') {
        return false;
    }
    for (const char *p = ssid; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

bool hal_wifi_scan(char *buf, size_t buf_len)
{
    wifi_scan_config_t cfg = {
        .show_hidden = false, /* 不请求隐藏 AP */
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, true); /* 阻塞到完成 */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(err));
        return false;
    }
    /* 驱动缓存已按 RSSI 降序，一次弹出前 16 条；清掉剩余记录释放内存 */
    uint16_t num = WIFI_SCAN_MAX_AP;
    err = esp_wifi_scan_get_ap_records(&num, s_ap_records);
    if (num < WIFI_SCAN_MAX_AP) {
        esp_wifi_clear_ap_list();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan get records: %s", esp_err_to_name(err));
        return false;
    }

    size_t pos = 0;
    bool first = true;
    buf[pos++] = '[';
    for (uint16_t i = 0; i < num; i++) {
        const wifi_ap_record_t *ap = &s_ap_records[i];
        if (!ssid_json_safe((const char *)ap->ssid)) {
            continue;
        }
        if (pos + 128 >= buf_len) {
            break; /* 单条最坏 ~100B（32B SSID 全转义 + 固定字段），128B 余量 */
        }
        pos += (size_t)snprintf(buf + pos, buf_len - pos, "%s{\"ssid\":\"", first ? "" : ",");
        first = false;
        for (const char *p = (const char *)ap->ssid; *p != '\0'; p++) {
            if (*p == '"' || *p == '\\') {
                buf[pos++] = '\\';
            }
            buf[pos++] = *p;
        }
        pos += (size_t)snprintf(buf + pos, buf_len - pos, "\",\"rssi\":%d,\"secure\":%s}",
                                ap->rssi, ap->authmode == WIFI_AUTH_OPEN ? "false" : "true");
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return true;
}

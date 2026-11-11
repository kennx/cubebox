#include <stdio.h>
#include <string.h>
#include <time.h>

#include <esp_err.h>
#include <esp_log.h>
#include <cJSON.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "config_model.h"
#include "config_store.h"
#include "hal/hal.h"

#define TAG "config_store"

/* NVS key 上限 15 字符；namespace 无此限制 */
#define CFG_NVS_NS     "cubebox"
#define CFG_KEY_BRIGHT "bright"
#define CFG_KEY_TZ     "tz"
#define CFG_KEY_SSID   "wifi_ssid"
#define CFG_KEY_PASS   "wifi_pass"

static config_model_t s_cfg;
/* console_task 置位 / gui_task 清零：单字节 bool 读写天然原子（对照 s_snap_req） */
static volatile bool s_apply_pending = false;

void config_store_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init %s, erasing and re-init", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    config_model_defaults(&s_cfg);

    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved config, using defaults");
        s_apply_pending = true;
        return;
    }

    uint8_t bright = 0;
    if (nvs_get_u8(h, CFG_KEY_BRIGHT, &bright) == ESP_OK) {
        /* 久置 NVS 可能存有越界值：经 model 验证拒绝则保持默认 */
        config_model_set_number(&s_cfg, "brightness", bright);
    }

    char buf[sizeof(s_cfg.wifi_pass)]; /* 各字符串字段均 <= 本尺寸 */
    size_t len = sizeof(buf);
    if (nvs_get_str(h, CFG_KEY_TZ, buf, &len) == ESP_OK) {
        config_model_set_string(&s_cfg, "tz", buf);
    }
    len = sizeof(buf);
    if (nvs_get_str(h, CFG_KEY_SSID, buf, &len) == ESP_OK) {
        config_model_set_string(&s_cfg, "wifi_ssid", buf);
    }
    len = sizeof(buf);
    if (nvs_get_str(h, CFG_KEY_PASS, buf, &len) == ESP_OK) {
        config_model_set_string(&s_cfg, "wifi_pass", buf);
    }
    nvs_close(h);

    s_apply_pending = true;
}

bool config_store_set_json(const char *json, char *err, size_t err_len)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        snprintf(err, err_len, "parse error");
        return false;
    }

    /* 先在副本上验证全部字段：任一失败整体放弃，s_cfg 与 NVS 都不动 */
    config_model_t tmp = s_cfg;
    bool ok = true;
    for (const cJSON *item = root->child; item != NULL; item = item->next) {
        const char *key = item->string ? item->string : "";
        config_model_type_t want;
        if (!config_model_key_type(key, &want)) {
            continue; /* 未知 key 忽略（向前兼容：新版网页发旧固件不认识的字段） */
        }
        if (want == CFG_TYPE_NUMBER) {
            ok = cJSON_IsNumber(item) && config_model_set_number(&tmp, key, item->valuedouble);
        } else {
            ok = cJSON_IsString(item) && config_model_set_string(&tmp, key, item->valuestring);
        }
        if (!ok) {
            /* 验证失败，或已知 key 的 JSON 类型不匹配（bool/null/嵌套对象在此拒绝） */
            snprintf(err, err_len, "bad field: %s", key);
            break;
        }
    }
    cJSON_Delete(root);
    if (!ok) {
        return false;
    }

    esp_err_t ret = ESP_OK;
    nvs_handle_t h;
    if ((ret = nvs_open(CFG_NVS_NS, NVS_READWRITE, &h)) != ESP_OK) {
        snprintf(err, err_len, "nvs open: %s", esp_err_to_name(ret));
        return false;
    }
    do {
        if ((ret = nvs_set_u8(h, CFG_KEY_BRIGHT, tmp.brightness)) != ESP_OK) {
            break;
        }
        if ((ret = nvs_set_str(h, CFG_KEY_TZ, tmp.tz)) != ESP_OK) {
            break;
        }
        if ((ret = nvs_set_str(h, CFG_KEY_SSID, tmp.wifi_ssid)) != ESP_OK) {
            break;
        }
        if ((ret = nvs_set_str(h, CFG_KEY_PASS, tmp.wifi_pass)) != ESP_OK) {
            break;
        }
        ret = nvs_commit(h);
    } while (0);
    nvs_close(h);
    if (ret != ESP_OK) {
        snprintf(err, err_len, "nvs write: %s", esp_err_to_name(ret));
        return false; /* NVS 失败：内存态不替换（tmp 丢弃） */
    }

    s_cfg = tmp;
    s_apply_pending = true;
    return true;
}

void config_store_apply_pending(void)
{
    if (!s_apply_pending) {
        return;
    }
    s_apply_pending = false;
    setenv("TZ", s_cfg.tz, 1);
    tzset();
    hal_display_set_backlight(s_cfg.brightness);
}

const char *config_store_get_json(char *buf, size_t buf_len)
{
    config_model_to_json(&s_cfg, buf, buf_len);
    return buf;
}

/*
 * 配置模型实现：验证规则集中在两个 static 谓词，JSON 拼装为有界追加。
 * 本文件保持纯 C（无 IDF/LVGL 依赖），供 tests/ 直接编译。
 */
#include <stdio.h>
#include <string.h>

#include "config_model.h"

static bool tz_char_allowed(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return strchr("+-,.:/<>", c) != NULL;
}

static bool tz_valid(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > sizeof(((config_model_t *)0)->tz) - 1) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!tz_char_allowed(s[i])) {
            return false;
        }
    }
    return true;
}

static bool printable_valid(const char *s, size_t min_len, size_t max_len)
{
    size_t n = strlen(s);
    if (n < min_len || n > max_len) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
    }
    return true;
}

void config_model_defaults(config_model_t *cfg)
{
    cfg->brightness = 100;
    strcpy(cfg->tz, "CST-8");
    cfg->wifi_ssid[0] = '\0';
    cfg->wifi_pass[0] = '\0';
}

bool config_model_set_number(config_model_t *cfg, const char *key, double value)
{
    if (strcmp(key, "brightness") == 0) {
        if (value != (double)(long long)value) {
            return false; /* 非整数（含 NaN） */
        }
        long long v = (long long)value;
        if (v < 0 || v > 100) {
            return false;
        }
        cfg->brightness = (uint8_t)v;
    }
    return true; /* 未知 key 忽略 */
}

bool config_model_set_string(config_model_t *cfg, const char *key, const char *value)
{
    if (value == NULL) {
        return false;
    }
    if (strcmp(key, "tz") == 0) {
        if (!tz_valid(value)) {
            return false;
        }
        snprintf(cfg->tz, sizeof(cfg->tz), "%s", value);
        return true;
    }
    if (strcmp(key, "wifi_ssid") == 0) {
        if (!printable_valid(value, 1, sizeof(cfg->wifi_ssid) - 1)) {
            return false;
        }
        snprintf(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), "%s", value);
        return true;
    }
    if (strcmp(key, "wifi_pass") == 0) {
        if (!printable_valid(value, 0, sizeof(cfg->wifi_pass) - 1)) {
            return false;
        }
        snprintf(cfg->wifi_pass, sizeof(cfg->wifi_pass), "%s", value);
        return true;
    }
    return true; /* 未知 key 忽略 */
}

/* ---- 有界 JSON 拼装 ---- */

static void append_str(char *buf, size_t cap, size_t *pos, const char *s)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (*pos + 1 >= cap) {
            break;
        }
        buf[(*pos)++] = s[i];
    }
}

/* JSON 字符串内容转义追加（调用方负责两侧引号） */
static void append_escaped(char *buf, size_t cap, size_t *pos, const char *s)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        char rep[7];
        size_t n;
        if (c == '"') {
            rep[0] = '\\';
            rep[1] = '"';
            n = 2;
        } else if (c == '\\') {
            rep[0] = '\\';
            rep[1] = '\\';
            n = 2;
        } else if (c < 0x20) {
            rep[0] = '\\';
            rep[1] = 'u';
            rep[2] = '0';
            rep[3] = '0';
            rep[4] = hx[(c >> 4) & 0xF];
            rep[5] = hx[c & 0xF];
            n = 6;
        } else {
            rep[0] = (char)c;
            n = 1;
        }
        for (size_t j = 0; j < n; j++) {
            if (*pos + 1 >= cap) {
                buf[*pos] = '\0';
                return;
            }
            buf[(*pos)++] = rep[j];
        }
    }
}

void config_model_to_json(const config_model_t *cfg, char *buf, size_t len)
{
    char num[16];
    size_t pos = 0;

    append_str(buf, len, &pos, "{\"brightness\":");
    snprintf(num, sizeof(num), "%u", cfg->brightness);
    append_str(buf, len, &pos, num);
    append_str(buf, len, &pos, ",\"tz\":\"");
    append_escaped(buf, len, &pos, cfg->tz);
    append_str(buf, len, &pos, "\",\"wifi_ssid\":\"");
    append_escaped(buf, len, &pos, cfg->wifi_ssid);
    append_str(buf, len, &pos, "\",\"wifi_pass_set\":");
    append_str(buf, len, &pos, cfg->wifi_pass[0] != '\0' ? "true" : "false");
    append_str(buf, len, &pos, "}");
    buf[pos] = '\0';
}

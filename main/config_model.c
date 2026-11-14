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

/* NTP 服务器：域名 / IPv4 / IPv6 字符集（字母数字 . : -） */
static bool ntp_host_valid(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > sizeof(((config_model_t *)0)->ntp_server) - 1) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            continue;
        }
        if (strchr(".:-", c) == NULL) {
            return false;
        }
    }
    return true;
}

/* 合法 UTF-8 多字节序列：返回序列字节数，非法返回 0（拒绝孤立续字节/截断/超长/代理对） */
static size_t utf8_seq_len(const unsigned char *s, size_t remain)
{
    const bool cont = remain >= 2 && (s[1] & 0xC0) == 0x80;
    const bool cont2 = remain >= 3 && (s[2] & 0xC0) == 0x80;
    const bool cont3 = remain >= 4 && (s[3] & 0xC0) == 0x80;
    unsigned char b = s[0];

    if (b >= 0xC2 && b <= 0xDF) {
        return cont ? 2 : 0;
    }
    if (b == 0xE0) {
        return (cont && s[1] >= 0xA0 && s[1] <= 0xBF && cont2) ? 3 : 0;
    }
    if ((b >= 0xE1 && b <= 0xEC) || b == 0xEE || b == 0xEF) {
        return (cont && cont2) ? 3 : 0;
    }
    if (b == 0xED) { /* 代理区 U+D800–DFFF 之外 */
        return (cont && s[1] >= 0x80 && s[1] <= 0x9F && cont2) ? 3 : 0;
    }
    if (b == 0xF0) {
        return (cont && s[1] >= 0x90 && s[1] <= 0xBF && cont2 && cont3) ? 4 : 0;
    }
    if (b >= 0xF1 && b <= 0xF3) {
        return (cont && cont2 && cont3) ? 4 : 0;
    }
    if (b == 0xF4) { /* 上限 U+10FFFF */
        return (cont && s[1] >= 0x80 && s[1] <= 0x8F && cont2 && cont3) ? 4 : 0;
    }
    return 0;
}

static bool printable_valid(const char *s, size_t min_len, size_t max_len)
{
    size_t n = strlen(s);
    if (n < min_len || n > max_len) {
        return false;
    }
    /* 可打印 ASCII 直过；多字节序列须为合法 UTF-8（中文 SSID 高频场景，字节计长） */
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c <= 0x7E) {
            i++;
            continue;
        }
        if (c < 0x80) {
            return false; /* 控制字符 / DEL */
        }
        size_t len = utf8_seq_len((const unsigned char *)s + i, n - i);
        if (len == 0) {
            return false;
        }
        i += len;
    }
    return true;
}

void config_model_defaults(config_model_t *cfg)
{
    cfg->brightness = 100;
    cfg->idle_timeout = 30;
    strcpy(cfg->tz, "CST-8");
    cfg->wifi_ssid[0] = '\0';
    cfg->wifi_pass[0] = '\0';
    strcpy(cfg->ntp_server, "ntp.aliyun.com");
    cfg->alarm_mode = 0;
    cfg->alarm_hour = 7;
    cfg->alarm_min = 0;
    cfg->alarm_weekdays = 127;
}

/* idle_timeout 合法值白名单（秒；0=永不）——网页选项与固件状态机共用语义 */
static bool idle_timeout_valid(long long v)
{
    return v == 0 || v == 30 || v == 60 || v == 300 || v == 600;
}

/* 数值字段公共路径：整数且在 [lo,hi] 才写入（失败不改动） */
static bool set_u8_bounded(uint8_t *field, double value, long long lo, long long hi)
{
    if (value != (double)(long long)value) {
        return false; /* 非整数（含 NaN） */
    }
    long long v = (long long)value;
    if (v < lo || v > hi) {
        return false;
    }
    *field = (uint8_t)v;
    return true;
}

static bool is_string_key(const char *key)
{
    return strcmp(key, "tz") == 0 || strcmp(key, "wifi_ssid") == 0 || strcmp(key, "wifi_pass") == 0
           || strcmp(key, "ntp_server") == 0;
}

static bool is_number_key(const char *key)
{
    return strcmp(key, "brightness") == 0 || strcmp(key, "idle_timeout") == 0
           || strcmp(key, "alarm_mode") == 0 || strcmp(key, "alarm_hour") == 0
           || strcmp(key, "alarm_min") == 0 || strcmp(key, "alarm_weekdays") == 0;
}

bool config_model_key_type(const char *key, config_model_type_t *out)
{
    if (is_number_key(key)) {
        *out = CFG_TYPE_NUMBER;
        return true;
    }
    if (is_string_key(key)) {
        *out = CFG_TYPE_STRING;
        return true;
    }
    return false;
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
        return true;
    }
    if (strcmp(key, "idle_timeout") == 0) {
        if (value != (double)(long long)value) {
            return false; /* 非整数（含 NaN） */
        }
        long long v = (long long)value;
        if (!idle_timeout_valid(v)) {
            return false;
        }
        cfg->idle_timeout = (uint16_t)v;
        return true;
    }
    if (strcmp(key, "alarm_mode") == 0) {
        return set_u8_bounded(&cfg->alarm_mode, value, 0, 3);
    }
    if (strcmp(key, "alarm_hour") == 0) {
        return set_u8_bounded(&cfg->alarm_hour, value, 0, 23);
    }
    if (strcmp(key, "alarm_min") == 0) {
        return set_u8_bounded(&cfg->alarm_min, value, 0, 59);
    }
    if (strcmp(key, "alarm_weekdays") == 0) {
        return set_u8_bounded(&cfg->alarm_weekdays, value, 0, 127);
    }
    if (is_string_key(key)) {
        return false; /* 已知 key 但类型不匹配（SETCFG {"tz":9} 整体拒绝） */
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
    if (strcmp(key, "ntp_server") == 0) {
        if (!ntp_host_valid(value)) {
            return false;
        }
        snprintf(cfg->ntp_server, sizeof(cfg->ntp_server), "%s", value);
        return true;
    }
    if (is_number_key(key)) {
        return false; /* 已知 key 但类型不匹配（SETCFG {"brightness":"40"} 整体拒绝） */
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
    append_str(buf, len, &pos, ",\"idle_timeout\":");
    snprintf(num, sizeof(num), "%u", cfg->idle_timeout);
    append_str(buf, len, &pos, num);
    append_str(buf, len, &pos, ",\"tz\":\"");
    append_escaped(buf, len, &pos, cfg->tz);
    append_str(buf, len, &pos, "\",\"wifi_ssid\":\"");
    append_escaped(buf, len, &pos, cfg->wifi_ssid);
    append_str(buf, len, &pos, "\",\"wifi_pass_set\":");
    append_str(buf, len, &pos, cfg->wifi_pass[0] != '\0' ? "true" : "false");
    append_str(buf, len, &pos, ",\"ntp_server\":\"");
    append_escaped(buf, len, &pos, cfg->ntp_server);
    append_str(buf, len, &pos, "\",\"alarm_mode\":");
    snprintf(num, sizeof(num), "%u", cfg->alarm_mode);
    append_str(buf, len, &pos, num);
    append_str(buf, len, &pos, ",\"alarm_hour\":");
    snprintf(num, sizeof(num), "%u", cfg->alarm_hour);
    append_str(buf, len, &pos, num);
    append_str(buf, len, &pos, ",\"alarm_min\":");
    snprintf(num, sizeof(num), "%u", cfg->alarm_min);
    append_str(buf, len, &pos, num);
    append_str(buf, len, &pos, ",\"alarm_weekdays\":");
    snprintf(num, sizeof(num), "%u", cfg->alarm_weekdays);
    append_str(buf, len, &pos, num);
    append_str(buf, len, &pos, "}");
    buf[pos] = '\0';
}

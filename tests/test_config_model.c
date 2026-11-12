/*
 * config_model 主机端测试：字段验证约束、失败不改动语义、JSON 拼装与转义。
 * 直接编译 main/config_model.c（纯 C，无 IDF/LVGL 依赖）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_model.h"

static void expect(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
}

static void expect_str(const char *actual, const char *expected, const char *label)
{
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n",
                label, expected, actual ? actual : "(null)");
        exit(1);
    }
}

static void expect_json_contains(const char *json, const char *needle, const char *label)
{
    if (strstr(json, needle) == NULL) {
        fprintf(stderr, "FAIL: %s (json '%s' lacks '%s')\n", label, json, needle);
        exit(1);
    }
}

int main(void)
{
    config_model_t cfg;

    /* ---- defaults ---- */
    config_model_defaults(&cfg);
    expect(cfg.brightness == 100, "default brightness 100");
    expect(cfg.idle_timeout == 30, "default idle_timeout 30");
    expect_str(cfg.tz, "CST-8", "default tz CST-8");
    expect_str(cfg.wifi_ssid, "", "default ssid empty");
    expect_str(cfg.wifi_pass, "", "default pass empty");
    expect_str(cfg.ntp_server, "ntp.aliyun.com", "default ntp_server");

    /* ---- set_number: brightness ---- */
    expect(config_model_set_number(&cfg, "brightness", 0) == true, "brightness 0 accepted");
    expect(cfg.brightness == 0, "brightness 0 stored");
    expect(config_model_set_number(&cfg, "brightness", 100) == true, "brightness 100 accepted");
    expect(cfg.brightness == 100, "brightness 100 stored");
    expect(config_model_set_number(&cfg, "brightness", -1) == false, "brightness -1 rejected");
    expect(config_model_set_number(&cfg, "brightness", 101) == false, "brightness 101 rejected");
    expect(config_model_set_number(&cfg, "brightness", 80.5) == false, "brightness 80.5 rejected");
    expect(cfg.brightness == 100, "brightness unchanged after rejects");
    /* 未知 key：忽略并返回 true，不改动 */
    expect(config_model_set_number(&cfg, "foo", 42) == true, "unknown number key ignored");
    expect(cfg.brightness == 100, "brightness unchanged by unknown key");

    /* ---- set_number: idle_timeout（白名单 0/30/60/300/600 秒，0=永不） ---- */
    expect(config_model_set_number(&cfg, "idle_timeout", 30) == true, "idle_timeout 30 accepted");
    expect(cfg.idle_timeout == 30, "idle_timeout 30 stored");
    expect(config_model_set_number(&cfg, "idle_timeout", 0) == true, "idle_timeout 0 (never) accepted");
    expect(cfg.idle_timeout == 0, "idle_timeout 0 stored");
    expect(config_model_set_number(&cfg, "idle_timeout", 60) == true, "idle_timeout 60 accepted");
    expect(cfg.idle_timeout == 60, "idle_timeout 60 stored");
    expect(config_model_set_number(&cfg, "idle_timeout", 300) == true, "idle_timeout 300 accepted");
    expect(config_model_set_number(&cfg, "idle_timeout", 600) == true, "idle_timeout 600 accepted");
    expect(cfg.idle_timeout == 600, "idle_timeout 600 stored");
    /* 非白名单整数拒绝且不改动 */
    expect(config_model_set_number(&cfg, "idle_timeout", 45) == false, "idle_timeout 45 rejected");
    expect(config_model_set_number(&cfg, "idle_timeout", 29) == false, "idle_timeout 29 rejected");
    expect(config_model_set_number(&cfg, "idle_timeout", 31) == false, "idle_timeout 31 rejected");
    expect(config_model_set_number(&cfg, "idle_timeout", 6000) == false, "idle_timeout 6000 rejected");
    expect(config_model_set_number(&cfg, "idle_timeout", -1) == false, "idle_timeout -1 rejected");
    expect(config_model_set_number(&cfg, "idle_timeout", 30.5) == false, "idle_timeout 30.5 rejected");
    expect(cfg.idle_timeout == 600, "idle_timeout unchanged after rejects");

    /* ---- set_string: tz ---- */
    expect(config_model_set_string(&cfg, "tz", "CST-8") == true, "tz CST-8 accepted");
    expect(config_model_set_string(&cfg, "tz", "GMT0BST,M3.5.0/1,M10.5.0") == true, "tz DST rule accepted");
    expect(config_model_set_string(&cfg, "tz", "AEST-10AEDT,M10.1.0,M4.1.0/3") == true, "tz southern DST accepted");
    expect(config_model_set_string(&cfg, "tz", "IST-5:30") == true, "tz offset colon accepted");
    expect(config_model_set_string(&cfg, "tz", "") == false, "tz empty rejected");
    expect(config_model_set_string(&cfg, "tz", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == true, "tz 39 chars accepted");
    expect(config_model_set_string(&cfg, "tz", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == false, "tz 40 chars rejected");
    expect(config_model_set_string(&cfg, "tz", "CST -8") == false, "tz space rejected");
    expect(config_model_set_string(&cfg, "tz", "CST-8;") == false, "tz semicolon rejected");
    expect(config_model_set_string(&cfg, "tz", "CST-8!") == false, "tz exclamation rejected");
    expect(config_model_set_string(&cfg, "tz", "<+0530>-5:30") == true, "tz angle bracket form accepted");

    /* ---- set_string: wifi_ssid ---- */
    expect(config_model_set_string(&cfg, "wifi_ssid", "Home_5G") == true, "ssid normal accepted");
    expect(config_model_set_string(&cfg, "wifi_ssid",
                                   "01234567890123456789012345678901") == true, "ssid 32 chars accepted");
    expect(config_model_set_string(&cfg, "wifi_ssid",
                                   "012345678901234567890123456789012") == false, "ssid 33 chars rejected");
    /* 引号/反斜杠是可打印 ASCII（0x20-0x7E），应接受并由 to_json 转义；
     * UTF-8 多字节放宽接受见下方「合法 UTF-8」块（终审 Ruling） */
    expect(config_model_set_string(&cfg, "wifi_ssid", "a\"b\\c") == true, "ssid quote/backslash accepted (printable)");
    expect(config_model_set_string(&cfg, "wifi_ssid", "") == false, "ssid empty rejected");
    expect_str(cfg.wifi_ssid, "a\"b\\c", "ssid unchanged after rejects");

    /* ---- set_string: wifi_pass ---- */
    {
        char pass65[66];
        memset(pass65, 'x', sizeof(pass65));
        pass65[64] = '\0';
        expect(config_model_set_string(&cfg, "wifi_pass", pass65) == true, "pass 64 chars accepted");
        pass65[64] = 'x';
        pass65[65] = '\0';
        expect(config_model_set_string(&cfg, "wifi_pass", pass65) == false, "pass 65 chars rejected");
        expect(config_model_set_string(&cfg, "wifi_pass", "") == true, "pass empty accepted (clear)");
        expect_str(cfg.wifi_pass, "", "pass cleared");
        expect(config_model_set_string(&cfg, "wifi_pass", "secret!@#") == true, "pass punctuation accepted");
    }

    /* ---- wifi_ssid/pass 放宽为合法 UTF-8（字节计长，中文 SSID 是高频场景） ---- */
    expect(config_model_set_string(&cfg, "wifi_ssid", "\xe5\xae\xb6\xe5\xba\xad") == true, "ssid UTF-8 accepted (6 bytes)");
    expect(config_model_set_string(&cfg, "wifi_pass", "\xe5\xaf\x86\xe7\xa0\x81") == true, "pass UTF-8 accepted");
    expect(config_model_set_string(&cfg, "wifi_ssid", "\x80") == false, "ssid lone continuation byte rejected");
    expect(config_model_set_string(&cfg, "wifi_ssid", "\xe5\xae") == false, "ssid truncated UTF-8 rejected");
    expect(config_model_set_string(&cfg, "wifi_ssid", "\xc0\x80") == false, "ssid overlong encoding rejected");
    {
        /* 字节上限：10 个中文 30 字节过，11 个 33 字节拒 */
        char u[64];
        memset(u, 0, sizeof(u));
        for (int i = 0; i < 10; i++) strcat(u, "\xe4\xb8\xad");
        expect(config_model_set_string(&cfg, "wifi_ssid", u) == true, "ssid 30 UTF-8 bytes accepted");
        strcat(u, "\xe4\xb8\xad");
        expect(config_model_set_string(&cfg, "wifi_ssid", u) == false, "ssid 33 UTF-8 bytes rejected");
    }

    /* ---- set_string: ntp_server（域名 / IPv4 / IPv6） ---- */
    expect(config_model_set_string(&cfg, "ntp_server", "ntp.aliyun.com") == true, "ntp domain accepted");
    expect(config_model_set_string(&cfg, "ntp_server", "time.windows.com") == true, "ntp long domain accepted");
    expect(config_model_set_string(&cfg, "ntp_server", "192.168.1.1") == true, "ntp IPv4 accepted");
    expect(config_model_set_string(&cfg, "ntp_server", "2400:3200::1") == true, "ntp IPv6 accepted");
    expect(config_model_set_string(&cfg, "ntp_server", "ntp") == true, "ntp bare label accepted");
    expect_str(cfg.ntp_server, "ntp", "ntp stored");
    expect(config_model_set_string(&cfg, "ntp_server", "") == false, "ntp empty rejected");
    expect(config_model_set_string(&cfg, "ntp_server", "a b") == false, "ntp space rejected");
    expect(config_model_set_string(&cfg, "ntp_server", "ntp@ex.com") == false, "ntp @ rejected");
    expect(config_model_set_string(&cfg, "ntp_server", "ntp\x01") == false, "ntp control char rejected");
    expect(config_model_set_string(&cfg, "ntp_server", "pool_\x80") == false, "ntp high byte rejected");
    expect_str(cfg.ntp_server, "ntp", "ntp unchanged after rejects");
    {
        char ntp64[65];
        memset(ntp64, 'a', sizeof(ntp64));
        ntp64[63] = '\0';
        expect(config_model_set_string(&cfg, "ntp_server", ntp64) == true, "ntp 63 chars accepted");
        ntp64[63] = 'a';
        ntp64[64] = '\0';
        expect(config_model_set_string(&cfg, "ntp_server", ntp64) == false, "ntp 64 chars rejected");
    }

    /* ---- set_string: 未知 key 忽略 ---- */
    expect(config_model_set_string(&cfg, "nope", "x") == true, "unknown string key ignored");

    /* ---- 已知 key 类型不匹配 → false（SETCFG {"brightness":"40"} 不得静默成功） ---- */
    expect(config_model_set_number(&cfg, "tz", 8) == false, "set_number on string key rejected");
    expect(config_model_set_number(&cfg, "wifi_ssid", 1) == false, "set_number on ssid rejected");
    expect(config_model_set_number(&cfg, "wifi_pass", 1) == false, "set_number on pass rejected");
    expect(config_model_set_number(&cfg, "ntp_server", 1) == false, "set_number on ntp_server rejected");
    expect(config_model_set_string(&cfg, "brightness", "40") == false, "set_string on number key rejected");
    expect(config_model_set_string(&cfg, "idle_timeout", "30") == false, "set_string on idle_timeout rejected");
    expect(cfg.brightness == 100 && cfg.brightness != 40, "brightness unchanged by type mismatch");

    /* ---- 验证失败不改动：tz 拒后仍为上次合法值 ---- */
    config_model_set_string(&cfg, "tz", "JST-9");
    expect(config_model_set_string(&cfg, "tz", "CST -8") == false, "invalid tz again rejected");
    expect_str(cfg.tz, "JST-9", "tz unchanged after reject");

    /* ---- to_json ---- */
    config_model_defaults(&cfg);
    char json[512];
    config_model_to_json(&cfg, json, sizeof(json));
    expect_str(json,
               "{\"brightness\":100,\"idle_timeout\":30,\"tz\":\"CST-8\",\"wifi_ssid\":\"\",\"wifi_pass_set\":false,"
               "\"ntp_server\":\"ntp.aliyun.com\"}",
               "to_json defaults exact");

    config_model_set_string(&cfg, "wifi_ssid", "Home");
    config_model_set_string(&cfg, "wifi_pass", "p");
    config_model_set_string(&cfg, "ntp_server", "cn.pool.ntp.org");
    config_model_set_number(&cfg, "idle_timeout", 0);
    config_model_to_json(&cfg, json, sizeof(json));
    expect_str(json,
               "{\"brightness\":100,\"idle_timeout\":0,\"tz\":\"CST-8\",\"wifi_ssid\":\"Home\",\"wifi_pass_set\":true,"
               "\"ntp_server\":\"cn.pool.ntp.org\"}",
               "to_json pass_set true when pass nonempty");

    /* 转义路径：setter 合法写入含引号/反斜杠的 ssid（规格 Review Focus #1 回环要求）；
     * 控制字符不可能经 setter 进入（可打印约束），手工构造进被序列化的 ssid 字段
     * 直测拼装器的控制字符转义（wifi_pass 不出现在 JSON 中，不能经它测） */
    config_model_defaults(&cfg);
    expect(config_model_set_string(&cfg, "wifi_ssid", "a\"b\\c") == true, "ssid special chars accepted");
    config_model_to_json(&cfg, json, sizeof(json));
    expect_json_contains(json, "a\\\"b\\\\c", "escape quote/backslash");
    strcpy(cfg.wifi_ssid, "x\x01y");
    config_model_to_json(&cfg, json, sizeof(json));
    expect_json_contains(json, "x\\u0001y", "escape control char as \\u00XX");

    /* UTF-8 原样直出（合法路径：setter 接受中文 ssid，to_json 不转义多字节） */
    config_model_defaults(&cfg);
    expect(config_model_set_string(&cfg, "wifi_ssid", "\xe5\xae\xb6") == true, "ssid 中文 accepted for json");
    config_model_to_json(&cfg, json, sizeof(json));
    expect_json_contains(json, "\"wifi_ssid\":\"\xe5\xae\xb6\"", "UTF-8 passthrough in json");

    /* 截断安全：小 buf 不越界（配合 asan），结果以 NUL 结尾 */
    char small[8];
    memset(small, 0xAA, sizeof(small));
    config_model_to_json(&cfg, small, sizeof(small));
    expect(strlen(small) == 7 && small[7] == '\0', "truncated to 7 chars + NUL");

    printf("test_config_model: all passed\n");
    return 0;
}

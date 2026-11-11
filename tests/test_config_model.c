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
    expect_str(cfg.tz, "CST-8", "default tz CST-8");
    expect_str(cfg.wifi_ssid, "", "default ssid empty");
    expect_str(cfg.wifi_pass, "", "default pass empty");

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
    /* 引号/反斜杠是可打印 ASCII（0x20-0x7E），按规格应接受并由 to_json 转义；
     * 多字节 UTF-8（字节 >= 0x80）才超出可打印范围被拒（Ruling 见 ledger） */
    expect(config_model_set_string(&cfg, "wifi_ssid", "a\"b\\c") == true, "ssid quote/backslash accepted (printable)");
    expect(config_model_set_string(&cfg, "wifi_ssid", "\xe5\xae\xb6") == false, "ssid UTF-8 rejected (printable only)");
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

    /* ---- set_string: 未知 key 忽略 ---- */
    expect(config_model_set_string(&cfg, "nope", "x") == true, "unknown string key ignored");

    /* ---- 验证失败不改动：tz 拒后仍为上次合法值 ---- */
    config_model_set_string(&cfg, "tz", "JST-9");
    expect(config_model_set_string(&cfg, "tz", "CST -8") == false, "invalid tz again rejected");
    expect_str(cfg.tz, "JST-9", "tz unchanged after reject");

    /* ---- to_json ---- */
    config_model_defaults(&cfg);
    char json[512];
    config_model_to_json(&cfg, json, sizeof(json));
    expect_str(json, "{\"brightness\":100,\"tz\":\"CST-8\",\"wifi_ssid\":\"\",\"wifi_pass_set\":false}",
               "to_json defaults exact");

    config_model_set_string(&cfg, "wifi_ssid", "Home");
    config_model_set_string(&cfg, "wifi_pass", "p");
    config_model_to_json(&cfg, json, sizeof(json));
    expect_str(json, "{\"brightness\":100,\"tz\":\"CST-8\",\"wifi_ssid\":\"Home\",\"wifi_pass_set\":true}",
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

    /* 截断安全：小 buf 不越界（配合 asan），结果以 NUL 结尾 */
    char small[8];
    memset(small, 0xAA, sizeof(small));
    config_model_to_json(&cfg, small, sizeof(small));
    expect(strlen(small) == 7 && small[7] == '\0', "truncated to 7 chars + NUL");

    printf("test_config_model: all passed\n");
    return 0;
}

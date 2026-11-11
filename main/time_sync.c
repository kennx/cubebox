#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "hal/hal.h"
#include "time_sync.h"

#define TAG "time_sync"

static volatile bool s_synced = false;

/* tm（UTC，struct tm 字段约定）→ Unix epoch，不依赖 TZ 环境变量（newlib 无 timegm）。
 * Howard Hinnant days_from_civil 民用历算法，1970-03-01 起算修正。 */
static long long tm_to_epoch(const struct tm *t)
{
    long long y = t->tm_year + 1900LL;
    const long long m = t->tm_mon + 1; /* 1-12 */
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                                 /* [0, 399] */
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)t->tm_mday - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                     /* [0, 146096] */
    const long long days = era * 146097LL + (long long)doe - 719468;
    return ((days * 24 + t->tm_hour) * 60 + t->tm_min) * 60 + t->tm_sec;
}

/* SNTP 任务上下文。系统时钟已由 lwip 自动 settimeofday；此处写回 RTC（存 UTC） */
static void on_sntp_sync(struct timeval *tv)
{
    s_synced = true;
    struct tm utc;
    time_t now = tv->tv_sec;
    gmtime_r(&now, &utc);
    if (hal_rtc_set_time(&utc)) {
        ESP_LOGI(TAG, "NTP synced, RTC updated");
    } else {
        ESP_LOGW(TAG, "NTP synced, but RTC write-back failed");
    }
}

void time_sync_init(void)
{
    /* 先从 RTC 恢复系统时间（掉电走时）；RTC 无效则保持上电默认，等 NTP 校准 */
    struct tm utc;
    if (hal_rtc_get_time(&utc)) {
        struct timeval tv = { .tv_sec = tm_to_epoch(&utc), .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "system time restored from RTC");
    } else {
        ESP_LOGI(TAG, "RTC invalid, waiting for NTP");
    }
    esp_sntp_set_time_sync_notification_cb(on_sntp_sync);
}

void time_sync_configure(const char *server)
{
    /* lwip sntp_setservername 只存指针不拷贝：传永久静态缓冲 */
    static char s_server[64];
    if (strcmp(server, s_server) == 0) {
        return;
    }
    snprintf(s_server, sizeof(s_server), "%s", server);

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_server);
    esp_sntp_init(); /* 未联网时安全：DNS 失败静默重试 */
    ESP_LOGI(TAG, "SNTP server: %s", s_server);
}

bool time_sync_is_synced(void)
{
    return s_synced;
}

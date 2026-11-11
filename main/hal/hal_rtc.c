#include <string.h>

#include "esp_log.h"
#include "pcf85063a.h"

#include "hal/hal_rtc.h"

#define TAG "hal_rtc"

/* year 寄存器 0-99（real = 1970 + reg）：早于此视为从未设置过 */
#define RTC_MIN_VALID_YEAR 2020

static pcf85063a_dev_t s_rtc;
static bool s_rtc_ok = false;

void hal_rtc_init(i2c_master_bus_handle_t bus)
{
    if (pcf85063a_init(&s_rtc, bus, PCF85063A_ADDRESS) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063 init failed, RTC disabled");
        return;
    }
    s_rtc_ok = true;
}

bool hal_rtc_get_time(struct tm *utc)
{
    if (!s_rtc_ok) {
        return false;
    }
    /* 秒寄存器 bit7 = OS：主电与电池均失效后的停振标志，置位表示时间不可信 */
    uint8_t sec_reg = 0;
    if (pcf85063a_read_register(&s_rtc, PCF85063A_RTC_SECOND_ADDR, &sec_reg, 1) != ESP_OK
        || (sec_reg & 0x80) != 0) {
        return false;
    }
    pcf85063a_datetime_t dt;
    if (pcf85063a_get_time_date(&s_rtc, &dt) != ESP_OK) {
        return false;
    }
    if (dt.year < RTC_MIN_VALID_YEAR) {
        return false; /* 从未校准过（出厂走时或寄存器随机值） */
    }

    memset(utc, 0, sizeof(*utc));
    utc->tm_year = (int)dt.year - 1900;
    utc->tm_mon = (int)dt.month - 1;
    utc->tm_mday = (int)dt.day;
    utc->tm_hour = (int)dt.hour;
    utc->tm_min = (int)dt.min;
    utc->tm_sec = (int)dt.sec;
    utc->tm_wday = (int)dt.dotw;
    return true;
}

bool hal_rtc_set_time(const struct tm *utc)
{
    if (!s_rtc_ok) {
        return false;
    }
    pcf85063a_datetime_t dt = {
        .year = (uint16_t)(utc->tm_year + 1900),
        .month = (uint8_t)(utc->tm_mon + 1),
        .day = (uint8_t)utc->tm_mday,
        .dotw = (uint8_t)utc->tm_wday,
        .hour = (uint8_t)utc->tm_hour,
        .min = (uint8_t)utc->tm_min,
        .sec = (uint8_t)utc->tm_sec,
    };
    return pcf85063a_set_time_date(&s_rtc, dt) == ESP_OK;
}

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "qmi8658.h"

#include "hal/hal_display.h"
#include "hal/hal_imu.h"
#include "hal/hal_orientation.h"

#define TAG "hal_imu"
#define IMU_READ_INTERVAL_US 100000
#define IMU_READ_FAIL_LIMIT 3

static qmi8658_dev_t s_imu;
static bool s_ready = false;
static uint8_t s_read_fail_cnt = 0;
static int64_t s_last_read_us = 0;
static hal_orientation_state_t s_orientation;

void hal_imu_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        ESP_LOGE(TAG, "i2c bus is NULL, orientation disabled");
        return;
    }
    esp_err_t err = qmi8658_init(&s_imu, bus, QMI8658_ADDRESS_HIGH);
    if (err == ESP_OK) {
        qmi8658_set_accel_unit_mps2(&s_imu, true);
        err = qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_LOWPOWER_21HZ);
    }
    if (err == ESP_OK) {
        err = qmi8658_enable_gyro(&s_imu, false);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 init failed: %s, orientation disabled", esp_err_to_name(err));
        return;
    }
    hal_orientation_reset(&s_orientation, HAL_ORIENTATION_0);
    s_last_read_us = 0;
    s_read_fail_cnt = 0;
    s_ready = true;
    ESP_LOGI(TAG, "QMI8658 orientation ready (accel low-power 21Hz)");
}

void hal_imu_service(void)
{
    if (!s_ready) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (now - s_last_read_us < IMU_READ_INTERVAL_US) {
        return;
    }
    s_last_read_us = now;

    float x, y, z;
    if (qmi8658_read_accel(&s_imu, &x, &y, &z) != ESP_OK) {
        s_read_fail_cnt++;
        if (s_read_fail_cnt >= IMU_READ_FAIL_LIMIT) {
            ESP_LOGW(TAG, "IMU I2C failed %u times, orientation frozen until reboot",
                     s_read_fail_cnt);
            s_ready = false;
        }
        return;
    }
    s_read_fail_cnt = 0;
    if (hal_orientation_update(&s_orientation, x, y, z)) {
        hal_display_set_orientation(s_orientation.current);
        ESP_LOGI(TAG, "display orientation %d (accel %.2f, %.2f, %.2f)",
                 s_orientation.current, x, y, z);
    }
}

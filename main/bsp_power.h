#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief 初始化 AXP2101 电源管理（在已创建的 i2c 总线上挂设备并配置输出）
 * @param bus i2c 主总线句柄（由 main.c 创建）
 */
void bsp_power_init(i2c_master_bus_handle_t bus);

/**
 * @brief 获取当前电池电量百分比
 * @param[out] pct 电池电量输出指针 (0-100)，读取失败时回退 99
 * @return esp_err_t
 */
esp_err_t bsp_power_get_battery_pct(uint8_t *pct);

/**
 * @brief 电池状态快照（电量 + 电压 + 供电/充电状态），供 BATT/BATTLOG 使用
 * @note 寄存器依据 AXP2101 datasheet + M5Unified CoreS3 实测对齐：
 *       0xA4 电量%、0x34/0x35 VBAT(mV)、0x00 bit5 VBUS、0x01[2:0] 充电状态
 */
typedef struct {
    uint8_t pct;         /* 电量百分比 0-100（reg 0xA4） */
    uint16_t vbat_mv;    /* 电池电压 mV（reg 0x34/0x35，14-bit 1mV/LSB） */
    bool vbus_present;   /* USB VBUS 在位（reg 0x00 bit5） */
    bool charging;       /* 充电中（reg 0x01[2:0] != 0b111；电池供电实测 0x37） */
    uint8_t raw_status;  /* reg 0x01 原始值，便于位定义现场校准 */
} bsp_power_batt_info_t;

/**
 * @brief 一次性读取电池电量、电压与供电/充电状态
 * @param[out] info 输出结构体
 * @return ESP_OK 或 I2C 错误
 */
esp_err_t bsp_power_get_battery_info(bsp_power_batt_info_t *info);

/**
 * @brief LCD 电源复位：ALDO3 高-低-高时序（屏复位由 AXP2101 控制，无独立 RST GPIO）
 */
void bsp_power_lcd_reset(void);

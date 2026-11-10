#pragma once

/* 前向声明 IDF 句柄类型（与 hal_power.h 逐字一致），使上层经 hal.h 聚合引入
 * 时不再传染 driver/i2c_master.h；总线由组合根 main.c 创建。 */
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;

/**
 * @brief 初始化 CST9220 触摸（I2C 0x5A@400kHz、RST=GPIO11、INT=GPIO5、
 *        swap_xy+mirror_y）并注册 LVGL 指针输入设备
 * @note  失败不致命：记错误日志后返回，不注册输入设备，固件继续运行
 *        （触摸非生存必需，与 hal_power/hal_display 的中止语义不同）。
 *        须在 hal_display_init 之后调用。
 */
void hal_touch_init(i2c_master_bus_handle_t bus);

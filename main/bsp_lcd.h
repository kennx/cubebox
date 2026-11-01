#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief flush 模式：时钟页叠绘点阵 / 设置页纯透传
 */
typedef enum {
    BSP_LCD_FLUSH_CLOCK_OVERLAY,   /* 时钟页：叠绘日期/时间点阵 */
    BSP_LCD_FLUSH_PASSTHROUGH,     /* 设置页等：LVGL 像素原样送显 */
} bsp_lcd_flush_mode_t;

/**
 * @brief 初始化 LCD（SPI/QSPI 总线 + SH8601 面板 + LVGL 显示驱动 + 2ms tick 定时器）
 * @note 内部调用 bsp_power_lcd_reset() 完成屏电源复位，需先 bsp_power_init()
 */
void bsp_lcd_init(void);

/**
 * @brief 切换 flush 模式（页面导航时调用；动画开始前切，防过渡帧叠绘残留）
 */
void bsp_lcd_set_flush_mode(bsp_lcd_flush_mode_t mode);
bsp_lcd_flush_mode_t bsp_lcd_get_flush_mode(void);

/**
 * @brief 设置背光亮度（发送 SH8601 命令 0x51，无独立背光 GPIO）
 * @param brightness 0-100，线性映射到寄存器 0-255
 */
void bsp_lcd_set_backlight(uint8_t brightness);

/**
 * @brief 用户亮度设置并持久化 NVS（滑条拖动结束时调用，避免拖动期间频繁写 flash）
 * @param pct 10-100（越界自动钳制）
 */
void bsp_lcd_set_brightness_pct(uint8_t pct);

/**
 * @brief 用户亮度即时生效（滑条拖动中每帧调用）：只写背光不落 NVS
 */
void bsp_lcd_set_brightness_live(uint8_t pct);

uint8_t bsp_lcd_get_brightness_pct(void);

/**
 * @brief 低功耗用：只改当前生效亮度，不动用户设定值、不写 NVS
 */
void bsp_lcd_set_brightness_raw(uint8_t pct);

/**
 * @brief 恢复用户设定亮度（低功耗唤醒时调用）
 */
void bsp_lcd_restore_user_brightness(void);

/* 低功耗亮度（≈10/255，待真机标定后调整） */
#define BSP_LCD_BRIGHTNESS_LOW_PCT 4

/**
 * @brief 请求一次屏幕截图（任意任务可调用；经 USB-Serial/JTAG 流式输出）
 * @note gui_task 轮询 bsp_lcd_snapshot_begin_if_requested() 触发全屏 invalidate，
 *       随后 flush_cb 按 "#SNAP x1 y1 x2 y2"+RGB565 hex 条带流出，"#SNAP END" 结束
 */
void bsp_lcd_snapshot_arm(void);

/**
 * @brief gui_task 每循环调用；true 表示本次需要 lv_obj_invalidate(lv_scr_act())
 */
bool bsp_lcd_snapshot_begin_if_requested(void);

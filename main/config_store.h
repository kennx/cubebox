#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * 配置存储与应用：NVS 持久化 + SETCFG 解析（cJSON）+ 副作用应用。
 * 字段验证与 JSON 拼装见 config_model；本模块依赖 IDF（NVS/cJSON）与 HAL。
 *
 * 副作用（亮度/tz）经 pending 标志 defer 到 gui_task 执行：亮度走
 * esp_lcd_panel_io_tx_param（SPI polling 事务），与 gui_task 的 draw_bitmap
 * （queue 事务）并发不在驱动线程安全契约内；tzset 修改 libc 全局同理
 * （对照 hal_display 截图的 s_snap_req 先例）。
 */

/** 初始化：NVS 挂载 → 读已存值覆盖默认值 → 置 apply pending（gui_task 首轮应用）。
 *  在 hal_display_init() 之后、gui_task 创建之前由 main.c 调用。 */
void config_store_init(void);

/** 解析并应用 SETCFG JSON（console_task 上下文）：验证 → NVS 写入 commit →
 *  置 apply pending。任一字段非法或 NVS 失败整体拒绝（内存态不动），err 填原因短语。 */
bool config_store_set_json(const char *json, char *err, size_t err_len);

/** 仅 gui_task 每轮调用：消费 pending，执行 setenv/tzset + 背光设置。 */
void config_store_apply_pending(void);

/** 当前配置序列化为 JSON 写入 buf，返回 buf 便于直接输出。 */
const char *config_store_get_json(char *buf, size_t buf_len);

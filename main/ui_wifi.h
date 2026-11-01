#pragma once

/**
 * @brief WiFi 子页：扫描列表 + 连接对时流程 + 状态显示
 *
 * 打开即自动扫描；有保存密码的网络点击直连，开放网络直连，
 * 其余弹出 T9 键盘输密码。下滑返回设置页。
 */

/**
 * @brief 创建常驻 WiFi 页 screen（gui_task 首帧前调用）
 */
void ui_wifi_create(void);

/**
 * @brief 滑入 WiFi 页并触发扫描
 */
void ui_wifi_show(void);

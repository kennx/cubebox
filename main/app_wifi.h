#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief WiFi 对时管理器：扫描/连接/SNTP 同步/断开清理，凭据 NVS 存取
 *
 * 无线电懒初始化（用户在 WiFi 页主动操作时才 ensure_started），
 * 每次对时（无论成败）结束都 stop+deinit 释放堆存。
 * UI（gui_task）经 app_wifi_get_status 轮询展示；命令经队列投递。
 */

typedef enum {
    APP_WIFI_STATE_IDLE = 0,
    APP_WIFI_STATE_SCANNING,
    APP_WIFI_STATE_CONNECTING,
    APP_WIFI_STATE_SYNCING,
    APP_WIFI_STATE_DONE,      /* 对时成功，status_text 形如 "Sync OK 12:34:56" */
    APP_WIFI_STATE_FAILED,    /* 失败，status_text 为短原因 */
} app_wifi_state_t;

typedef struct {
    char ssid[33];            /* 原始字节（中文 SSID 照存，UI 显示方块可接受） */
    int8_t rssi;
    bool auth_open;           /* true = 开放网络 */
    bool saved;               /* NVS 中有凭据 */
} app_wifi_ap_info_t;

typedef struct {
    volatile app_wifi_state_t state;   /* worker 写 / gui_task 读（单核，安全） */
    char status_text[64];
    app_wifi_ap_info_t ap_list[20];
    size_t ap_count;
} app_wifi_status_t;

/**
 * @brief 懒初始化（netif/事件循环 + worker 任务 + WiFi 栈），幂等
 */
void app_wifi_ensure_started(void);

/**
 * @brief 状态快照指针（worker 更新，gui_task 读取）
 */
const app_wifi_status_t *app_wifi_get_status(void);

/**
 * @brief 请求扫描（异步，结果经 status 的 ap_list/state 呈现）
 */
void app_wifi_cmd_scan(void);

/**
 * @brief 请求连接并对时
 * @param ssid 目标网络
 * @param password 密码；NULL = 使用 NVS 保存的凭据，"" = 开放网络
 */
void app_wifi_cmd_connect(const char *ssid, const char *password);

/**
 * @brief 查询 NVS 中是否存有该 SSID 的密码
 * @param pwd_out 命中时输出密码
 * @return true 命中
 */
bool app_wifi_load_password(const char *ssid, char *pwd_out, size_t len);

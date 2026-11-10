#pragma once

#include "apps/apps.h"

/*
 * 新应用模板（新建应用时复制本目录为 main/apps/app_<name>/ 起步，改名后
 * 在 main.c 里 apps_install 注册）。
 *
 * 生命周期契约见 apps/apps.h；对照 StackChan firmware/main/apps/app_template。
 * 无 UI 的应用不需要 view/ 子目录；需要 UI 时参照 app_test 的 view/ 组织。
 */
extern const app_lifecycle_t app_template;

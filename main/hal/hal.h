#pragma once

/* HAL 汇总入口：上层（apps/main.c）统一从这里引入硬件抽象接口。
 * 新增硬件子系统时：新建 hal/hal_<subsys>.{c,h} 并在此聚合。 */

#include "hal/hal_power.h"
#include "hal/hal_display.h"
#include "hal/hal_touch.h"

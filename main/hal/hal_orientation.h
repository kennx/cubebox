#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 显示补偿角，不是设备物理旋转角；90°/270° 与物理旋转方向相反。 */
typedef enum {
    HAL_ORIENTATION_0 = 0,
    HAL_ORIENTATION_90 = 90,
    HAL_ORIENTATION_180 = 180,
    HAL_ORIENTATION_270 = 270,
} hal_orientation_t;

typedef struct {
    hal_orientation_t current;
    hal_orientation_t candidate;
    uint8_t candidate_count;
} hal_orientation_state_t;

void hal_orientation_reset(hal_orientation_state_t *state, hal_orientation_t initial);

/* 平放、低重力分量或接近对角线时返回 current，其余按 dominant X/Y 轴选向。 */
hal_orientation_t hal_orientation_classify(float x, float y, float z, hal_orientation_t current);

/* 连续 3 次识别到同一新方向才更新 current；true 表示本次发生切换。 */
bool hal_orientation_update(hal_orientation_state_t *state, float x, float y, float z);

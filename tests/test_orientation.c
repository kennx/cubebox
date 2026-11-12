#include <stdio.h>
#include <stdlib.h>

#include "hal/hal_orientation.h"

static void expect(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
}

static void expect_change(hal_orientation_state_t *state, float x, float y, float z,
                          hal_orientation_t expected)
{
    expect(hal_orientation_update(state, x, y, z) == false, "first sample is pending");
    expect(hal_orientation_update(state, x, y, z) == false, "second sample is pending");
    expect(state->current != expected, "pending samples do not rotate");
    expect(hal_orientation_update(state, x, y, z) == true, "third sample rotates");
    expect(state->current == expected, "confirmed orientation applied");
    expect(hal_orientation_update(state, x, y, z) == false, "stable orientation is idempotent");
}

int main(void)
{
    /* 物理旋转与显示补偿相反：X- 为设备逆时针 90°，显示补偿 270°。 */
    expect(hal_orientation_classify(0, 9.8f, 0, HAL_ORIENTATION_180) == HAL_ORIENTATION_0,
           "classify default");
    expect(hal_orientation_classify(-9.8f, 0, 0, HAL_ORIENTATION_0) == HAL_ORIENTATION_270,
           "classify counterclockwise 90");
    expect(hal_orientation_classify(0, -9.8f, 0, HAL_ORIENTATION_0) == HAL_ORIENTATION_180,
           "classify 180");
    expect(hal_orientation_classify(9.8f, 0, 0, HAL_ORIENTATION_0) == HAL_ORIENTATION_90,
           "classify clockwise 90");

    /* 平放、低重力分量和接近对角线时保持原方向。 */
    expect(hal_orientation_classify(0, 0, 9.8f, HAL_ORIENTATION_90) == HAL_ORIENTATION_90,
           "flat keeps current");
    expect(hal_orientation_classify(2.0f, 0, 0, HAL_ORIENTATION_90) == HAL_ORIENTATION_90,
           "weak planar gravity keeps current");
    expect(hal_orientation_classify(7.0f, 7.0f, 0, HAL_ORIENTATION_90) == HAL_ORIENTATION_90,
           "diagonal keeps current");
    expect(hal_orientation_classify(8.0f, 6.0f, 0, HAL_ORIENTATION_180) == HAL_ORIENTATION_90,
           "dominant axis selects orientation");

    hal_orientation_state_t state;
    hal_orientation_reset(&state, HAL_ORIENTATION_0);
    expect_change(&state, 9.8f, 0, 0, HAL_ORIENTATION_90);
    expect_change(&state, 0, -9.8f, 0, HAL_ORIENTATION_180);
    expect_change(&state, -9.8f, 0, 0, HAL_ORIENTATION_270);
    expect_change(&state, 0, 9.8f, 0, HAL_ORIENTATION_0);

    /* 单次抖动和倾斜样本不能累计成旋转。 */
    expect(hal_orientation_update(&state, 9.8f, 0, 0) == false, "glitch first sample");
    expect(hal_orientation_update(&state, 0, 0, 9.8f) == false, "flat cancels pending candidate");
    expect(hal_orientation_update(&state, 9.8f, 0, 0) == false, "restart first sample");
    expect(hal_orientation_update(&state, 9.8f, 0, 0) == false, "restart second sample");
    expect(state.current == HAL_ORIENTATION_0, "glitch never rotated");
    expect(hal_orientation_update(&state, 9.8f, 0, 0) == true, "restart third sample rotates");
    expect(state.current == HAL_ORIENTATION_90, "glitch recovery orientation");

    /* 候选方向直接变化时必须从 1 重新计数。 */
    hal_orientation_reset(&state, HAL_ORIENTATION_0);
    expect(hal_orientation_update(&state, 9.8f, 0, 0) == false, "candidate A pending");
    expect(hal_orientation_update(&state, 0, -9.8f, 0) == false, "candidate B first sample");
    expect(state.candidate == HAL_ORIENTATION_180 && state.candidate_count == 1,
           "candidate change resets count");
    expect(hal_orientation_update(&state, 0, -9.8f, 0) == false, "candidate B second sample");
    expect(state.current == HAL_ORIENTATION_0, "candidate B still pending");
    expect(hal_orientation_update(&state, 0, -9.8f, 0) == true, "candidate B third sample rotates");
    expect(state.current == HAL_ORIENTATION_180, "candidate B orientation");

    /* 阈值本身可用；低于阈值或主导比例不足时保持。 */
    expect(hal_orientation_classify(4.0f, 0, 0, HAL_ORIENTATION_180) == HAL_ORIENTATION_90,
           "planar threshold is inclusive");
    expect(hal_orientation_classify(3.99f, 0, 0, HAL_ORIENTATION_180) == HAL_ORIENTATION_180,
           "below planar threshold keeps current");
    expect(hal_orientation_classify(6.0f, 5.0f, 0, HAL_ORIENTATION_180) == HAL_ORIENTATION_90,
           "dominance ratio threshold is inclusive");
    expect(hal_orientation_classify(5.99f, 5.0f, 0, HAL_ORIENTATION_180) == HAL_ORIENTATION_180,
           "below dominance ratio keeps current");
    expect(hal_orientation_classify(6.01f, 5.0f, 0, HAL_ORIENTATION_180) == HAL_ORIENTATION_90,
           "above dominance ratio selects orientation");

    printf("test_orientation: all passed\n");
    return 0;
}

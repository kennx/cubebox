#include "hal/hal_orientation.h"

#define ORIENTATION_MIN_PLANAR_MPS2   4.0f
#define ORIENTATION_DOMINANCE_RATIO   1.2f
#define ORIENTATION_CONFIRM_SAMPLES   3

static float absolute(float value)
{
    return value < 0 ? -value : value;
}

void hal_orientation_reset(hal_orientation_state_t *state, hal_orientation_t initial)
{
    state->current = initial;
    state->candidate = initial;
    state->candidate_count = 0;
}

hal_orientation_t hal_orientation_classify(float x, float y, float z, hal_orientation_t current)
{
    (void)z;
    float ax = absolute(x);
    float ay = absolute(y);
    float dominant = ax > ay ? ax : ay;
    float other = ax > ay ? ay : ax;
    if (dominant < ORIENTATION_MIN_PLANAR_MPS2 ||
        dominant < other * ORIENTATION_DOMINANCE_RATIO) {
        return current;
    }
    if (ax > ay) {
        return x > 0 ? HAL_ORIENTATION_90 : HAL_ORIENTATION_270;
    }
    return y > 0 ? HAL_ORIENTATION_0 : HAL_ORIENTATION_180;
}

bool hal_orientation_update(hal_orientation_state_t *state, float x, float y, float z)
{
    hal_orientation_t next = hal_orientation_classify(x, y, z, state->current);
    if (next == state->current) {
        state->candidate = state->current;
        state->candidate_count = 0;
        return false;
    }
    if (state->candidate != next) {
        state->candidate = next;
        state->candidate_count = 1;
    } else if (state->candidate_count < ORIENTATION_CONFIRM_SAMPLES) {
        state->candidate_count++;
    }
    if (state->candidate_count < ORIENTATION_CONFIRM_SAMPLES) {
        return false;
    }
    state->current = next;
    state->candidate = next;
    state->candidate_count = 0;
    return true;
}

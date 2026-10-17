#ifndef RT_NODE_LOCAL_FOLLOW_H
#define RT_NODE_LOCAL_FOLLOW_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float roll0_deg, pitch0_deg;
    int32_t last_pan_us, last_tilt_us;
    bool initialized;
} LocalFollowState;

/* Pure C, no hardware/RTOS. Start once at authorization, output starts center.
 * Caller checks READY/valid/fresh/new generation; Target only when applying.
 * Nonfinite input/difference returns false without changing state/outputs.
 */
bool LocalFollow_Start(LocalFollowState *s, float roll0_deg, float pitch0_deg);
bool LocalFollow_Target(LocalFollowState *s, float roll_deg, float pitch_deg,
                        int32_t *pan_us, int32_t *tilt_us);
#endif

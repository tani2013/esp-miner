#ifndef ASG_H_
#define ASG_H_

#include <stdbool.h>
#include <stdint.h>

// Adaptive Stability Governor (ASG)
// ---------------------------------
// A closed-loop frequency governor that keeps the ASIC operating just below its
// instability threshold by using the live hardware error rate as feedback.
//
// Every silicon sample ("silicon lottery") has a slightly different stable
// ceiling, and that ceiling shifts with ambient temperature. A fixed
// frequency/voltage either leaves performance on the table or runs unstable
// when the room warms up. The ASG continuously tracks the chip's real stable
// operating point instead of guessing it once.
//
// Safety model (important):
//   The governor NEVER raises frequency above the user-configured ceiling. It
//   only lowers frequency when instability (or thermal pressure) is detected,
//   and slowly recovers back toward the ceiling once the chip is healthy again.
//   This makes it impossible for the governor to push the hardware beyond the
//   limits the user has already chosen - it can only ever be more conservative.
//
// The control logic in this module is intentionally free of ESP-IDF
// dependencies so it can be unit-tested on the host.

typedef struct {
    bool    enabled;
    float   ceiling_freq;     // user-configured maximum frequency (MHz)
    float   floor_freq;       // hard minimum the governor will drop to (MHz)
    float   target_freq;      // governor's current commanded frequency (MHz)
    float   error_target_pct; // desired steady-state HW error rate (%)
    int     stable_windows;   // consecutive healthy decision windows observed
    int64_t last_decision_ms; // timestamp of the last decision
    int64_t last_change_ms;   // timestamp of the last frequency change
} AsgState;

// --- Tunable constants (exposed so tests can reason about them) ---
#define ASG_DECISION_INTERVAL_MS  15000  // re-evaluate at most every 15 s
#define ASG_FLOOR_FRACTION        0.80f  // never drop below 80% of the ceiling
#define ASG_ERROR_CRITICAL_MULT   2.5f   // critical threshold = target * 2.5
#define ASG_BACKOFF_GENTLE_MHZ    2.0f   // step down on mild instability
#define ASG_BACKOFF_HARD_MHZ      10.0f  // step down on strong instability
#define ASG_RECOVER_STEP_MHZ      1.0f   // step up when consistently healthy
#define ASG_RECOVER_AFTER_WINDOWS 8      // ~2 min stable before recovering
#define ASG_THERMAL_GUARD_C       68.0f  // back off if chip gets this warm

// Initialise the governor. The target starts at the ceiling and is wound down
// from there only if the chip proves unstable.
void asg_init(AsgState * asg, float ceiling_freq, float error_target_pct);

// Update the ceiling when the user changes the configured frequency. This
// re-probes from the user's new intent (target reset to the new ceiling).
void asg_set_ceiling(AsgState * asg, float ceiling_freq);

// Update the desired steady-state error target at runtime.
void asg_set_error_target(AsgState * asg, float error_target_pct);

// Run one governor step and return the recommended frequency (MHz). Safe to
// call frequently - it self-throttles real decisions to ASG_DECISION_INTERVAL_MS
// and simply returns the current target in between. A chip_temp_c <= 0 is
// treated as "unknown" (thermal guard disabled for that step).
float asg_step(AsgState * asg, float error_pct, float chip_temp_c, int64_t now_ms);

#endif // ASG_H_

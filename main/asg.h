#ifndef ASG_H_
#define ASG_H_

#include <stdbool.h>
#include <stdint.h>

// Adaptive Stability Governor (ASG) - v2 (joint frequency + voltage control)
// ---------------------------------------------------------------------------
// A closed-loop governor that keeps the ASIC at the sweet spot between
// stability and efficiency by jointly tuning frequency and core voltage, using
// the live hardware error rate (and a hashrate-collapse guard) as feedback.
//
// Two coupled goals, handled with clear priorities so the loops never fight:
//
//   * STABILITY (frequency):  if the chip becomes unstable or runs hot, the
//     governor slows it down. This is the dominant, safety-first behaviour.
//
//   * EFFICIENCY (voltage):   while the chip is healthy and cool, the governor
//     undervolts one small step at a time, hunting for the lowest voltage that
//     still mines cleanly at the current frequency - the best J/TH operating
//     point. If undervolting starts producing errors, it restores margin.
//
// Safety model (unchanged from v1, now covering both knobs):
//   The governor NEVER raises frequency or voltage above the user-configured
//   ceilings. It can only ever be MORE conservative than what the user set.
//   Frequency is clamped to [80% .. 100%] of the ceiling and voltage to
//   [90% .. 100%] of the ceiling, so neither can collapse.
//
// Hang guard:
//   Undervolting too far can wedge a chip so it stops producing shares. In that
//   state the hardware error rate misleadingly reads ~0% (there is no hashrate
//   to be wrong). The governor therefore also watches the ratio of actual to
//   expected hashrate and treats a collapse as a strong instability signal,
//   restoring voltage decisively.
//
// The control logic is free of ESP-IDF dependencies so it can be unit-tested on
// the host.

typedef struct {
    bool    enabled;
    bool    voltage_control;  // if false, voltage is pinned to the ceiling

    float   ceiling_freq;     // user-configured max frequency (MHz)
    float   floor_freq;       // hard minimum frequency (MHz)
    float   target_freq;      // governor's commanded frequency (MHz)

    float   ceiling_voltage;  // user-configured max core voltage (mV)
    float   floor_voltage;    // hard minimum core voltage (mV)
    float   target_voltage;   // governor's commanded core voltage (mV)

    float   error_target_pct; // desired steady-state HW error rate (%)
    int     stable_windows;   // consecutive healthy decision windows
    int     settle_windows;   // windows since the last freq/voltage change
    int64_t last_decision_ms; // timestamp of the last decision
    int64_t last_change_ms;   // timestamp of the last applied change
} AsgState;

typedef struct {
    float frequency_mhz;
    float voltage_mv;
} AsgOutput;

// --- Tunable constants (exposed so tests can reason about them) ---
#define ASG_DECISION_INTERVAL_MS    15000  // re-evaluate at most every 15 s
#define ASG_FLOOR_FRACTION          0.80f  // never drop below 80% of freq ceiling
#define ASG_VOLTAGE_FLOOR_FRACTION  0.90f  // never undervolt below 90% of ceiling
#define ASG_ERROR_CRITICAL_MULT     2.5f   // critical threshold = target * 2.5
#define ASG_BACKOFF_GENTLE_MHZ      2.0f   // step down on mild instability
#define ASG_BACKOFF_HARD_MHZ        10.0f  // step down on strong instability
#define ASG_RECOVER_STEP_MHZ        1.0f   // step up when consistently healthy
#define ASG_VOLTAGE_STEP_MV         10.0f  // voltage adjustment granularity
#define ASG_RECOVER_AFTER_WINDOWS   8      // ~2 min healthy before optimising
#define ASG_THERMAL_GUARD_C         68.0f  // back off if chip gets this warm
#define ASG_SETTLE_WINDOWS          2      // settle time before trusting hashrate
#define ASG_COLLAPSE_RATIO          0.6f   // hashrate < 60% of expected => collapse

// Initialise the governor. Targets start at the ceilings and are only ever
// wound down from there.
void asg_init(AsgState * asg, float ceiling_freq, float ceiling_voltage_mv, float error_target_pct);

// Update ceilings when the user changes the configured frequency/voltage. These
// re-probe from the user's new intent (target reset to the new ceiling).
void asg_set_ceiling(AsgState * asg, float ceiling_freq);
void asg_set_voltage_ceiling(AsgState * asg, float ceiling_voltage_mv);

// Update the desired steady-state error target at runtime.
void asg_set_error_target(AsgState * asg, float error_target_pct);

// Run one governor step and return the recommended frequency + voltage. Safe to
// call frequently; it self-throttles real decisions to ASG_DECISION_INTERVAL_MS.
//   error_pct      - live hardware error rate (%)
//   chip_temp_c    - ASIC temperature (C); <= 0 means "unknown" (guard disabled)
//   hashrate_ratio - actual / expected hashrate (1.0 == on target; pass 1.0 when
//                    expected is unknown to avoid false collapse detection)
AsgOutput asg_step(AsgState * asg, float error_pct, float chip_temp_c,
                   float hashrate_ratio, int64_t now_ms);

#endif // ASG_H_

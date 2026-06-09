#include "asg.h"

static float asg_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void asg_init(AsgState * asg, float ceiling_freq, float ceiling_voltage_mv, float error_target_pct)
{
    asg->enabled = true;
    asg->voltage_control = true;

    asg->ceiling_freq = ceiling_freq;
    asg->floor_freq = ceiling_freq * ASG_FLOOR_FRACTION;
    asg->target_freq = ceiling_freq;

    asg->ceiling_voltage = ceiling_voltage_mv;
    asg->floor_voltage = ceiling_voltage_mv * ASG_VOLTAGE_FLOOR_FRACTION;
    asg->target_voltage = ceiling_voltage_mv;

    asg->error_target_pct = error_target_pct;
    asg->stable_windows = 0;
    asg->settle_windows = 0;
    asg->last_decision_ms = 0;
    asg->last_change_ms = 0;
}

void asg_set_ceiling(AsgState * asg, float ceiling_freq)
{
    asg->ceiling_freq = ceiling_freq;
    asg->floor_freq = ceiling_freq * ASG_FLOOR_FRACTION;
    // Re-probe from the user's new intent rather than carrying a stale target.
    asg->target_freq = ceiling_freq;
    asg->stable_windows = 0;
}

void asg_set_voltage_ceiling(AsgState * asg, float ceiling_voltage_mv)
{
    asg->ceiling_voltage = ceiling_voltage_mv;
    asg->floor_voltage = ceiling_voltage_mv * ASG_VOLTAGE_FLOOR_FRACTION;
    asg->target_voltage = ceiling_voltage_mv;
    asg->stable_windows = 0;
}

void asg_set_error_target(AsgState * asg, float error_target_pct)
{
    asg->error_target_pct = error_target_pct;
}

AsgOutput asg_step(AsgState * asg, float error_pct, float chip_temp_c,
                   float hashrate_ratio, int64_t now_ms)
{
    AsgOutput out;

    if (!asg->enabled) {
        // Governor disabled: hand back the full configured operating point.
        asg->target_freq = asg->ceiling_freq;
        asg->target_voltage = asg->ceiling_voltage;
        out.frequency_mhz = asg->target_freq;
        out.voltage_mv = asg->target_voltage;
        return out;
    }

    // When voltage control is off, keep voltage pinned at the ceiling.
    if (!asg->voltage_control) {
        asg->target_voltage = asg->ceiling_voltage;
    }

    // Self-throttle: only make a real decision once per interval so the chip and
    // the error-rate signal have time to settle after each change.
    if (asg->last_decision_ms != 0 &&
        (now_ms - asg->last_decision_ms) < ASG_DECISION_INTERVAL_MS) {
        out.frequency_mhz = asg->target_freq;
        out.voltage_mv = asg->target_voltage;
        return out;
    }
    asg->last_decision_ms = now_ms;
    asg->settle_windows++;

    const float critical = asg->error_target_pct * ASG_ERROR_CRITICAL_MULT;
    const float prev_freq = asg->target_freq;
    const float prev_voltage = asg->target_voltage;
    const bool vctrl = asg->voltage_control;

    // Temperature readings are invalid (<= 0) while the ASIC is powered down.
    const bool temp_valid = (chip_temp_c > 0.0f);
    const bool thermal_pressure = temp_valid && (chip_temp_c > ASG_THERMAL_GUARD_C);

    // Hang guard: a wedged/over-undervolted chip stops hashing, which makes the
    // error rate read ~0% (nothing to be wrong). Only trusted once the chip has
    // settled for a couple of windows since our last change.
    const bool collapsed = (asg->settle_windows >= ASG_SETTLE_WINDOWS) &&
                           (hashrate_ratio > 0.0f) &&
                           (hashrate_ratio < ASG_COLLAPSE_RATIO);

    if (collapsed) {
        // Barely producing - almost certainly under-volted past the edge (or
        // otherwise stuck). Restore voltage margin fast and ease frequency.
        if (vctrl && asg->target_voltage < asg->ceiling_voltage) {
            asg->target_voltage += 2.0f * ASG_VOLTAGE_STEP_MV;
        }
        asg->target_freq -= ASG_BACKOFF_GENTLE_MHZ;
        asg->stable_windows = 0;
    } else if (error_pct > critical) {
        // Strong instability: add margin fast and drop frequency hard.
        if (vctrl && asg->target_voltage < asg->ceiling_voltage) {
            asg->target_voltage += 2.0f * ASG_VOLTAGE_STEP_MV;
        }
        asg->target_freq -= ASG_BACKOFF_HARD_MHZ;
        asg->stable_windows = 0;
    } else if (thermal_pressure) {
        // Too hot: shed heat by slowing down. Do NOT raise voltage (more heat).
        asg->target_freq -= ASG_BACKOFF_GENTLE_MHZ;
        asg->stable_windows = 0;
    } else if (error_pct > asg->error_target_pct) {
        // Mild instability: most likely from undervolting, so restore a little
        // margin first; only ease frequency once already at the voltage ceiling.
        if (vctrl && asg->target_voltage < asg->ceiling_voltage) {
            asg->target_voltage += ASG_VOLTAGE_STEP_MV;
        } else {
            asg->target_freq -= ASG_BACKOFF_GENTLE_MHZ;
        }
        asg->stable_windows = 0;
    } else {
        // Healthy and cool.
        asg->stable_windows++;
        const bool comfortable =
            !temp_valid || (chip_temp_c < (ASG_THERMAL_GUARD_C - 3.0f));
        if (asg->stable_windows >= ASG_RECOVER_AFTER_WINDOWS && comfortable) {
            // Efficiency first: undervolt toward the floor. Once at the voltage
            // floor, chase performance by recovering frequency toward the ceiling.
            if (vctrl && asg->target_voltage > asg->floor_voltage) {
                asg->target_voltage -= ASG_VOLTAGE_STEP_MV;
            } else if (asg->target_freq < asg->ceiling_freq) {
                asg->target_freq += ASG_RECOVER_STEP_MHZ;
            }
            asg->stable_windows = 0;
        }
    }

    asg->target_freq = asg_clampf(asg->target_freq, asg->floor_freq, asg->ceiling_freq);
    asg->target_voltage = asg_clampf(asg->target_voltage, asg->floor_voltage, asg->ceiling_voltage);

    if (asg->target_freq != prev_freq || asg->target_voltage != prev_voltage) {
        asg->last_change_ms = now_ms;
        asg->settle_windows = 0; // restart the settle clock after any change
    }

    out.frequency_mhz = asg->target_freq;
    out.voltage_mv = asg->target_voltage;
    return out;
}

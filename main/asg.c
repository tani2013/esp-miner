#include "asg.h"

static float asg_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void asg_init(AsgState * asg, float ceiling_freq, float error_target_pct)
{
    asg->enabled = true;
    asg->ceiling_freq = ceiling_freq;
    asg->floor_freq = ceiling_freq * ASG_FLOOR_FRACTION;
    asg->target_freq = ceiling_freq;
    asg->error_target_pct = error_target_pct;
    asg->stable_windows = 0;
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

void asg_set_error_target(AsgState * asg, float error_target_pct)
{
    asg->error_target_pct = error_target_pct;
}

float asg_step(AsgState * asg, float error_pct, float chip_temp_c, int64_t now_ms)
{
    if (!asg->enabled) {
        // Governor disabled: hand back the full configured frequency.
        asg->target_freq = asg->ceiling_freq;
        return asg->target_freq;
    }

    // Self-throttle: only make a real decision once per interval. Between
    // decisions we hold the current target steady so the chip (and the error
    // rate signal) have time to settle after each change.
    if (asg->last_decision_ms != 0 &&
        (now_ms - asg->last_decision_ms) < ASG_DECISION_INTERVAL_MS) {
        return asg->target_freq;
    }
    asg->last_decision_ms = now_ms;

    const float critical = asg->error_target_pct * ASG_ERROR_CRITICAL_MULT;
    const float prev = asg->target_freq;

    // Temperature readings are invalid (<= 0) while the ASIC is powered down.
    const bool temp_valid = (chip_temp_c > 0.0f);
    const bool thermal_pressure = temp_valid && (chip_temp_c > ASG_THERMAL_GUARD_C);

    if (error_pct > critical) {
        // Strong instability: drop hard and restart the stability clock.
        asg->target_freq -= ASG_BACKOFF_HARD_MHZ;
        asg->stable_windows = 0;
    } else if (error_pct > asg->error_target_pct || thermal_pressure) {
        // Mild instability or getting warm: ease off a notch.
        asg->target_freq -= ASG_BACKOFF_GENTLE_MHZ;
        asg->stable_windows = 0;
    } else {
        // Healthy window. Recover slowly toward the ceiling only after we have
        // been stable for a while and the chip is thermally comfortable.
        asg->stable_windows++;
        const bool thermally_comfortable =
            !temp_valid || (chip_temp_c < (ASG_THERMAL_GUARD_C - 3.0f));
        if (asg->stable_windows >= ASG_RECOVER_AFTER_WINDOWS && thermally_comfortable) {
            asg->target_freq += ASG_RECOVER_STEP_MHZ;
            asg->stable_windows = 0;
        }
    }

    asg->target_freq = asg_clampf(asg->target_freq, asg->floor_freq, asg->ceiling_freq);
    if (asg->target_freq != prev) {
        asg->last_change_ms = now_ms;
    }
    return asg->target_freq;
}

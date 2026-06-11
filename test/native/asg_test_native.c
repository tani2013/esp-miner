// Host (native) test for the Adaptive Stability Governor logic.
// asg.c is intentionally free of ESP-IDF dependencies, so the control logic can
// be compiled and executed with a plain compiler in CI:
//   gcc -I main test/native/asg_test_native.c main/asg.c -o asg_native -lm
// Exits non-zero (via assert) on any failure.
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "asg.h"

static int64_t clk;
static AsgOutput drive(AsgState * a, float e, float t, float hr)
{
    clk += ASG_DECISION_INTERVAL_MS;
    return asg_step(a, e, t, hr, clk);
}
#define EQ(a, b) (fabsf((a) - (b)) < 0.001f)

int main(void)
{
    AsgState a;
    AsgOutput o;

    // starts at ceilings
    asg_init(&a, 525, 1200, 2);
    assert(EQ(a.target_freq, 525) && EQ(a.target_voltage, 1200));
    assert(EQ(a.floor_freq, 525 * ASG_FLOOR_FRACTION) && EQ(a.floor_voltage, 1200 * ASG_VOLTAGE_FLOOR_FRACTION));

    // healthy forever: freq stays at ceiling, voltage undervolts to floor, never below
    clk = 0; asg_init(&a, 500, 1200, 2);
    for (int i = 0; i < 300; i++) { o = drive(&a, 0, 55, 1.0f); assert(o.frequency_mhz <= 500.001f && o.voltage_mv >= 1080 - 0.001f); }
    assert(EQ(a.target_freq, 500) && EQ(a.target_voltage, 1080));

    // mild instability at ceiling voltage -> gentle frequency backoff
    clk = 0; asg_init(&a, 500, 1200, 2); o = drive(&a, 3, 55, 1.0f); assert(EQ(o.frequency_mhz, 498) && EQ(o.voltage_mv, 1200));

    // critical -> hard frequency backoff
    clk = 0; asg_init(&a, 500, 1200, 2); o = drive(&a, 6, 55, 1.0f); assert(EQ(o.frequency_mhz, 490));

    // relentless errors -> frequency clamps at the floor
    clk = 0; asg_init(&a, 500, 1200, 2); for (int i = 0; i < 300; i++) drive(&a, 50, 55, 1.0f); assert(EQ(a.target_freq, 400));

    // healthy -> undervolt one step after RECOVER_AFTER windows
    clk = 0; asg_init(&a, 500, 1200, 2); for (int i = 0; i < ASG_RECOVER_AFTER_WINDOWS; i++) drive(&a, 0, 55, 1.0f);
    assert(EQ(a.target_voltage, 1190) && EQ(a.target_freq, 500));

    // undervolt then mild error -> restore a voltage step, frequency untouched
    o = drive(&a, 3, 55, 1.0f); assert(EQ(o.voltage_mv, 1200) && EQ(o.frequency_mhz, 500));

    // hang guard: voltage below ceiling + settled + collapsed hashrate -> restore voltage + ease frequency
    asg_init(&a, 500, 1200, 2); a.target_voltage = 1150; a.settle_windows = ASG_SETTLE_WINDOWS; a.last_decision_ms = 0;
    o = asg_step(&a, 0, 55, 0.4f, ASG_DECISION_INTERVAL_MS); assert(EQ(o.voltage_mv, 1170) && EQ(o.frequency_mhz, 498));

    // hang guard does not fire before the settle window elapses
    asg_init(&a, 500, 1200, 2); a.target_voltage = 1150; a.settle_windows = 0; a.last_decision_ms = 0;
    o = asg_step(&a, 0, 55, 0.4f, ASG_DECISION_INTERVAL_MS); assert(EQ(o.voltage_mv, 1150));

    // thermal pressure -> frequency backoff, voltage unchanged
    clk = 0; asg_init(&a, 500, 1200, 2); o = drive(&a, 0, ASG_THERMAL_GUARD_C + 5, 1.0f); assert(EQ(o.frequency_mhz, 498) && EQ(o.voltage_mv, 1200));

    // voltage control off -> voltage pinned to ceiling, frequency still governed
    clk = 0; asg_init(&a, 500, 1200, 2); a.voltage_control = false;
    for (int i = 0; i < ASG_RECOVER_AFTER_WINDOWS + 2; i++) { o = drive(&a, 0, 55, 1.0f); }
    assert(EQ(o.voltage_mv, 1200));

    // disabled -> both ceilings
    asg_init(&a, 500, 1200, 2); a.target_freq = 420; a.target_voltage = 1100; a.enabled = false;
    o = asg_step(&a, 99, 90, 0.1f, 999999); assert(EQ(o.frequency_mhz, 500) && EQ(o.voltage_mv, 1200));

    // re-probe on ceiling changes
    clk = 0; asg_init(&a, 500, 1200, 2); drive(&a, 6, 55, 1.0f); assert(a.target_freq < 500);
    asg_set_ceiling(&a, 480); assert(EQ(a.target_freq, 480) && EQ(a.floor_freq, 384));
    asg_set_voltage_ceiling(&a, 1100); assert(EQ(a.target_voltage, 1100) && EQ(a.floor_voltage, 990));

    // self-throttle between decisions
    asg_init(&a, 500, 1200, 2); o = asg_step(&a, 3, 55, 1.0f, ASG_DECISION_INTERVAL_MS); assert(EQ(o.frequency_mhz, 498));
    o = asg_step(&a, 3, 55, 1.0f, ASG_DECISION_INTERVAL_MS + 1000); assert(EQ(o.frequency_mhz, 498));

    printf("asg_test_native: ALL ASSERTIONS PASSED\n");
    return 0;
}

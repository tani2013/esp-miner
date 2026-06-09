#include "unity.h"
#include "asg.h"

// Helper: advance the clock past the decision interval so asg_step evaluates
// instead of self-throttling, and return its output.
static AsgOutput drive(AsgState * asg, float err, float temp, float hr, int64_t * clock_ms)
{
    *clock_ms += ASG_DECISION_INTERVAL_MS;
    return asg_step(asg, err, temp, hr, *clock_ms);
}

TEST_CASE("asg starts at the frequency and voltage ceilings", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 525.0f, 1200.0f, 2.0f);
    TEST_ASSERT_EQUAL_FLOAT(525.0f, asg.target_freq);
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, asg.target_voltage);
    TEST_ASSERT_EQUAL_FLOAT(525.0f * ASG_FLOOR_FRACTION, asg.floor_freq);
    TEST_ASSERT_EQUAL_FLOAT(1200.0f * ASG_VOLTAGE_FLOOR_FRACTION, asg.floor_voltage);
}

TEST_CASE("asg never exceeds the ceilings and undervolts to the floor when healthy", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    for (int i = 0; i < 300; i++) {
        AsgOutput o = drive(&asg, 0.0f, 55.0f, 1.0f, &clock);
        TEST_ASSERT_TRUE(o.frequency_mhz <= 500.0f);
        TEST_ASSERT_TRUE(o.voltage_mv <= 1200.0f);
        TEST_ASSERT_TRUE(o.voltage_mv >= asg.floor_voltage - 0.001f);
    }
    // Frequency held at ceiling; voltage driven down to the efficiency floor.
    TEST_ASSERT_EQUAL_FLOAT(500.0f, asg.target_freq);
    TEST_ASSERT_EQUAL_FLOAT(1080.0f, asg.target_voltage);
}

TEST_CASE("asg backs off frequency on mild instability when at the voltage ceiling", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    AsgOutput o = drive(&asg, 3.0f, 55.0f, 1.0f, &clock);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_GENTLE_MHZ, o.frequency_mhz);
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, o.voltage_mv);
}

TEST_CASE("asg backs off frequency hard on strong instability", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    AsgOutput o = drive(&asg, 6.0f, 55.0f, 1.0f, &clock); // critical = 5%
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_HARD_MHZ, o.frequency_mhz);
}

TEST_CASE("asg never drops frequency below the floor", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    for (int i = 0; i < 300; i++) {
        AsgOutput o = drive(&asg, 50.0f, 55.0f, 1.0f, &clock);
        TEST_ASSERT_TRUE(o.frequency_mhz >= asg.floor_freq - 0.001f);
    }
    TEST_ASSERT_EQUAL_FLOAT(asg.floor_freq, asg.target_freq);
}

TEST_CASE("asg undervolts for efficiency after sustained stability", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    for (int i = 0; i < ASG_RECOVER_AFTER_WINDOWS; i++) {
        drive(&asg, 0.0f, 55.0f, 1.0f, &clock);
    }
    TEST_ASSERT_EQUAL_FLOAT(1200.0f - ASG_VOLTAGE_STEP_MV, asg.target_voltage);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, asg.target_freq);
}

TEST_CASE("asg never undervolts below the voltage floor", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    for (int i = 0; i < 300; i++) {
        AsgOutput o = drive(&asg, 0.0f, 55.0f, 1.0f, &clock);
        TEST_ASSERT_TRUE(o.voltage_mv >= asg.floor_voltage - 0.001f);
    }
    TEST_ASSERT_EQUAL_FLOAT(asg.floor_voltage, asg.target_voltage);
}

TEST_CASE("asg restores a voltage step on mild instability after undervolting", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    for (int i = 0; i < ASG_RECOVER_AFTER_WINDOWS; i++) {
        drive(&asg, 0.0f, 55.0f, 1.0f, &clock);
    }
    TEST_ASSERT_EQUAL_FLOAT(1190.0f, asg.target_voltage);
    AsgOutput o = drive(&asg, 3.0f, 55.0f, 1.0f, &clock);
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, o.voltage_mv); // margin restored, freq untouched
    TEST_ASSERT_EQUAL_FLOAT(500.0f, o.frequency_mhz);
}

TEST_CASE("asg hang guard restores voltage when hashrate collapses", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    // Simulate having undervolted and settled.
    asg.target_voltage = 1150.0f;
    asg.settle_windows = ASG_SETTLE_WINDOWS;
    asg.last_decision_ms = 0;
    // Error rate reads healthy (~0%) but the chip is barely hashing.
    AsgOutput o = asg_step(&asg, 0.0f, 55.0f, 0.4f, ASG_DECISION_INTERVAL_MS);
    TEST_ASSERT_EQUAL_FLOAT(1150.0f + 2.0f * ASG_VOLTAGE_STEP_MV, o.voltage_mv);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_GENTLE_MHZ, o.frequency_mhz);
}

TEST_CASE("asg hang guard does not fire before the settle window elapses", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    asg.target_voltage = 1150.0f;
    asg.settle_windows = 0; // just changed something
    asg.last_decision_ms = 0;
    AsgOutput o = asg_step(&asg, 0.0f, 55.0f, 0.4f, ASG_DECISION_INTERVAL_MS);
    TEST_ASSERT_EQUAL_FLOAT(1150.0f, o.voltage_mv); // unchanged; collapse not trusted yet
}

TEST_CASE("asg treats thermal pressure as instability without raising voltage", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    AsgOutput o = drive(&asg, 0.0f, ASG_THERMAL_GUARD_C + 5.0f, 1.0f, &clock);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_GENTLE_MHZ, o.frequency_mhz);
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, o.voltage_mv);
}

TEST_CASE("asg pins voltage to the ceiling when voltage control is off", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    asg.voltage_control = false;
    int64_t clock = 0;
    AsgOutput o = {0};
    for (int i = 0; i < ASG_RECOVER_AFTER_WINDOWS + 2; i++) {
        o = drive(&asg, 0.0f, 55.0f, 1.0f, &clock);
    }
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, o.voltage_mv); // never undervolts
}

TEST_CASE("asg disabled returns the full ceilings", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    asg.target_freq = 420.0f;
    asg.target_voltage = 1100.0f;
    asg.enabled = false;
    AsgOutput o = asg_step(&asg, 99.0f, 90.0f, 0.1f, 999999);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, o.frequency_mhz);
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, o.voltage_mv);
}

TEST_CASE("asg re-probes from new ceilings when the user changes settings", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    int64_t clock = 0;
    drive(&asg, 6.0f, 55.0f, 1.0f, &clock); // wind freq down
    TEST_ASSERT_TRUE(asg.target_freq < 500.0f);

    asg_set_ceiling(&asg, 480.0f);
    TEST_ASSERT_EQUAL_FLOAT(480.0f, asg.target_freq);
    TEST_ASSERT_EQUAL_FLOAT(480.0f * ASG_FLOOR_FRACTION, asg.floor_freq);

    asg_set_voltage_ceiling(&asg, 1100.0f);
    TEST_ASSERT_EQUAL_FLOAT(1100.0f, asg.target_voltage);
    TEST_ASSERT_EQUAL_FLOAT(1100.0f * ASG_VOLTAGE_FLOOR_FRACTION, asg.floor_voltage);
}

TEST_CASE("asg self-throttles decisions to the decision interval", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 1200.0f, 2.0f);
    AsgOutput o1 = asg_step(&asg, 3.0f, 55.0f, 1.0f, ASG_DECISION_INTERVAL_MS);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_GENTLE_MHZ, o1.frequency_mhz);
    AsgOutput o2 = asg_step(&asg, 3.0f, 55.0f, 1.0f, ASG_DECISION_INTERVAL_MS + 1000);
    TEST_ASSERT_EQUAL_FLOAT(o1.frequency_mhz, o2.frequency_mhz);
}

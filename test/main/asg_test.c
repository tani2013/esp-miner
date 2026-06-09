#include "unity.h"
#include "asg.h"

// Helper: drive the governor for one decision, advancing the clock past the
// decision interval so asg_step actually evaluates instead of self-throttling.
static float drive(AsgState * asg, float err, float temp, int64_t * clock_ms)
{
    *clock_ms += ASG_DECISION_INTERVAL_MS;
    return asg_step(asg, err, temp, *clock_ms);
}

TEST_CASE("asg starts at the ceiling", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 525.0f, 2.0f);
    TEST_ASSERT_EQUAL_FLOAT(525.0f, asg.target_freq);
    TEST_ASSERT_EQUAL_FLOAT(525.0f * ASG_FLOOR_FRACTION, asg.floor_freq);
}

TEST_CASE("asg never exceeds the ceiling even when perfectly healthy", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;
    for (int i = 0; i < 200; i++) {
        float f = drive(&asg, 0.0f, 55.0f, &clock);
        TEST_ASSERT_TRUE(f <= 500.0f);
    }
    TEST_ASSERT_EQUAL_FLOAT(500.0f, asg.target_freq);
}

TEST_CASE("asg backs off gently on mild instability", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;
    float f = drive(&asg, 3.0f /* > target, < critical */, 55.0f, &clock);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_GENTLE_MHZ, f);
}

TEST_CASE("asg backs off hard on strong instability", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;
    // target 2% -> critical = 5%; feed 6% to trigger the hard path.
    float f = drive(&asg, 6.0f, 55.0f, &clock);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_HARD_MHZ, f);
}

TEST_CASE("asg never drops below the floor", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;
    for (int i = 0; i < 200; i++) {
        float f = drive(&asg, 50.0f /* relentless errors */, 55.0f, &clock);
        TEST_ASSERT_TRUE(f >= asg.floor_freq - 0.001f);
    }
    TEST_ASSERT_EQUAL_FLOAT(asg.floor_freq, asg.target_freq);
}

TEST_CASE("asg recovers toward the ceiling after sustained stability", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;

    // Force one backoff.
    drive(&asg, 3.0f, 55.0f, &clock);
    float after_backoff = asg.target_freq;
    TEST_ASSERT_TRUE(after_backoff < 500.0f);

    // Stay healthy long enough to earn one recovery step.
    for (int i = 0; i < ASG_RECOVER_AFTER_WINDOWS; i++) {
        drive(&asg, 0.0f, 55.0f, &clock);
    }
    TEST_ASSERT_EQUAL_FLOAT(after_backoff + ASG_RECOVER_STEP_MHZ, asg.target_freq);
}

TEST_CASE("asg treats thermal pressure as instability", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;
    // Error rate is healthy, but the chip is hot -> back off anyway.
    float f = drive(&asg, 0.0f, ASG_THERMAL_GUARD_C + 5.0f, &clock);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_GENTLE_MHZ, f);
}

TEST_CASE("asg ignores invalid (powered-down) temperature readings", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;
    // temp <= 0 means "unknown"; a healthy error rate must not trigger backoff.
    float f = drive(&asg, 0.0f, -1.0f, &clock);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, f);
}

TEST_CASE("asg self-throttles decisions to the decision interval", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);

    // First decision at t=interval backs off once.
    int64_t clock = ASG_DECISION_INTERVAL_MS;
    float f1 = asg_step(&asg, 3.0f, 55.0f, clock);
    TEST_ASSERT_EQUAL_FLOAT(500.0f - ASG_BACKOFF_GENTLE_MHZ, f1);

    // A call shortly after must NOT make another decision.
    float f2 = asg_step(&asg, 3.0f, 55.0f, clock + 1000);
    TEST_ASSERT_EQUAL_FLOAT(f1, f2);
}

TEST_CASE("asg disabled returns the full ceiling", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    asg.target_freq = 420.0f; // pretend it had wound down
    asg.enabled = false;
    float f = asg_step(&asg, 99.0f, 90.0f, 999999);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, f);
}

TEST_CASE("asg re-probes from the new ceiling when the user changes frequency", "[asg]")
{
    AsgState asg;
    asg_init(&asg, 500.0f, 2.0f);
    int64_t clock = 0;
    drive(&asg, 6.0f, 55.0f, &clock); // wind down
    TEST_ASSERT_TRUE(asg.target_freq < 500.0f);

    asg_set_ceiling(&asg, 480.0f);
    TEST_ASSERT_EQUAL_FLOAT(480.0f, asg.target_freq);
    TEST_ASSERT_EQUAL_FLOAT(480.0f * ASG_FLOOR_FRACTION, asg.floor_freq);
}

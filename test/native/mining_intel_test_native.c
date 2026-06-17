// Host (native) test for the Mining Intelligence Engine.
//   gcc -I main test/native/mining_intel_test_native.c main/mining_intel.c -o mi_native -lm
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "mining_intel.h"

#define CLOSE(a, b, tol) (fabs((a) - (b)) <= (tol) * fabs(b) + 1e-9)

int main(void)
{
    // Invalid inputs return -1.
    assert(mining_intel_seconds_to_block(0, 1e12) < 0);
    assert(mining_intel_seconds_to_block(1000, 0) < 0);
    assert(mining_intel_block_probability(0, 1e12, 86400) < 0);
    assert(mining_intel_expected_btc_per_day(-5, 1e12, 800000) < 0);

    // Seconds to block: difficulty * 2^32 / hashrate (hashrate in GH/s -> H/s).
    // 1000 GH/s = 1e12 H/s, difficulty 1 -> 2^32 / 1e12 s.
    double ettb = mining_intel_seconds_to_block(1000.0, 1.0);
    assert(CLOSE(ettb, 4294967296.0 / 1.0e12, 1e-9));

    // A ~1.2 TH/s Gamma at a realistic difficulty produces an enormous ETTB.
    double big = mining_intel_seconds_to_block(1200.0, 1.0e14);
    assert(big > 1.0e11); // > ~3000 years, as expected for solo
    // Monotonic: more hashrate -> less time.
    assert(mining_intel_seconds_to_block(2400.0, 1.0e14) < big);

    // Probability over a window is ~ window/ettb for tiny ratios, and bounded.
    double p = mining_intel_block_probability(1200.0, 1.0e14, 86400.0);
    assert(p > 0.0 && p < 1.0);
    double approx = 86400.0 / big;
    assert(CLOSE(p, approx, 1e-3)); // 1-exp(-x) ~ x for small x
    // Longer window -> higher probability.
    assert(mining_intel_block_probability(1200.0, 1.0e14, 86400.0 * 7) > p);

    // Block subsidy halving schedule.
    assert(CLOSE(mining_intel_block_subsidy_btc(0), 50.0, 1e-9));
    assert(CLOSE(mining_intel_block_subsidy_btc(210000), 25.0, 1e-9));
    assert(CLOSE(mining_intel_block_subsidy_btc(420000), 12.5, 1e-9));
    assert(CLOSE(mining_intel_block_subsidy_btc(840000), 3.125, 1e-9)); // post-2024
    assert(mining_intel_block_subsidy_btc(64 * 210000) == 0.0);
    assert(mining_intel_block_subsidy_btc(-1) == 0.0);

    // Expected BTC/day = (86400/ettb) * subsidy. Positive and tiny for solo.
    double ev = mining_intel_expected_btc_per_day(1200.0, 1.0e14, 840000);
    double expect = (86400.0 / big) * 3.125;
    assert(CLOSE(ev, expect, 1e-6));
    assert(ev > 0.0 && ev < 1.0e-3);

    printf("mining_intel_test_native: ALL ASSERTIONS PASSED\n");
    return 0;
}

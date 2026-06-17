#include "mining_intel.h"
#include <math.h>
#include <stdint.h>

// Number of hashes that must be tried, on average, per unit of difficulty.
#define MINING_INTEL_HASHES_PER_DIFFICULTY 4294967296.0 // 2^32
#define MINING_INTEL_GH 1.0e9
#define MINING_INTEL_HALVING_INTERVAL 210000

double mining_intel_seconds_to_block(double hashrate_ghs, double network_difficulty)
{
    if (hashrate_ghs <= 0.0 || network_difficulty <= 0.0) {
        return -1.0;
    }
    double hashes = network_difficulty * MINING_INTEL_HASHES_PER_DIFFICULTY;
    double hashrate_hs = hashrate_ghs * MINING_INTEL_GH;
    return hashes / hashrate_hs;
}

double mining_intel_block_probability(double hashrate_ghs, double network_difficulty, double window_seconds)
{
    double ettb = mining_intel_seconds_to_block(hashrate_ghs, network_difficulty);
    if (ettb <= 0.0 || window_seconds <= 0.0) {
        return -1.0;
    }
    return 1.0 - exp(-window_seconds / ettb);
}

double mining_intel_block_subsidy_btc(int block_height)
{
    if (block_height < 0) {
        return 0.0;
    }
    int halvings = block_height / MINING_INTEL_HALVING_INTERVAL;
    // After 64 halvings the subsidy has rounded to zero in Bitcoin Core.
    if (halvings >= 64) {
        return 0.0;
    }
    return 50.0 / (double) ((uint64_t) 1 << halvings);
}

double mining_intel_expected_btc_per_day(double hashrate_ghs, double network_difficulty, int block_height)
{
    double ettb = mining_intel_seconds_to_block(hashrate_ghs, network_difficulty);
    if (ettb <= 0.0) {
        return -1.0;
    }
    double blocks_per_day = 86400.0 / ettb;
    return blocks_per_day * mining_intel_block_subsidy_btc(block_height);
}

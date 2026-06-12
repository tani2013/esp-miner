#ifndef MINING_INTEL_H_
#define MINING_INTEL_H_

// Mining Intelligence Engine
// --------------------------
// Honest solo-mining mathematics that almost no miner surfaces on-device. Given
// your hashrate and the live network difficulty it answers the questions a solo
// miner actually cares about:
//   * how long, on average, until I find a block?
//   * what is my real probability of hitting one in the next day / week / year?
//   * what is the expected value (BTC/day) at these settings?
//
// It does NOT change the proof-of-work odds (nothing can) - it quantifies them
// precisely so you can reason about tactics: e.g. whether Turbo's extra hashrate
// is worth the extra power, or how much an efficiency gain shifts your expected
// value. Pure double-precision math, free of ESP-IDF dependencies, so it is
// unit-tested on the host.

// Average seconds to find a block: difficulty * 2^32 / hashrate.
// Returns -1 for invalid input (hashrate or difficulty <= 0).
double mining_intel_seconds_to_block(double hashrate_ghs, double network_difficulty);

// Probability (0..1) of finding at least one block within window_seconds,
// modelled as a Poisson process: 1 - exp(-window / seconds_to_block).
// Returns -1 for invalid input.
double mining_intel_block_probability(double hashrate_ghs, double network_difficulty, double window_seconds);

// Current block subsidy in BTC for a given height (50 BTC halving every
// 210000 blocks). Excludes transaction fees. height < 0 returns the current
// post-2024-halving subsidy assumption of 0 (caller should pass a real height).
double mining_intel_block_subsidy_btc(int block_height);

// Expected reward rate in BTC/day = (86400 / seconds_to_block) * subsidy(height).
// Returns -1 for invalid input.
double mining_intel_expected_btc_per_day(double hashrate_ghs, double network_difficulty, int block_height);

#endif // MINING_INTEL_H_

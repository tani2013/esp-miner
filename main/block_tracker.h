#ifndef BLOCK_TRACKER_H_
#define BLOCK_TRACKER_H_

#include <stdbool.h>
#include <stdint.h>

// Solo Sniper block tracker
// -------------------------
// Watches the Stratum previous-block-hash to detect when the network advances to
// a new block. For a solo miner pointed at a local node, reacting to a new block
// instantly is the only real edge there is: every hash spent on an
// already-solved block is wasted. This module makes that edge visible - it
// counts the blocks seen this session and timestamps the last change, so the UI
// can show how fresh the current work is and how quickly the miner switched.
//
// It does NOT (and cannot) change proof-of-work odds - it is the same lottery
// as everyone else's. It only ensures no hash is silently wasted on stale work
// and surfaces that to the operator. The logic is free of ESP-IDF dependencies
// so it can be unit-tested on the host.

#define BLOCK_TRACKER_HASH_LEN 64

typedef struct {
    char     last_prevhash[BLOCK_TRACKER_HASH_LEN + 1];
    uint32_t blocks_seen;     // distinct blocks observed this session
    int64_t  last_change_ms;  // timestamp of the last new block
    bool     initialized;
} BlockTracker;

void block_tracker_init(BlockTracker * t);

// Feed the latest Stratum prev_block_hash. Returns true when it represents a NEW
// block (hash differs from the previous one), updating blocks_seen and
// last_change_ms. A NULL/empty hash is ignored and returns false.
bool block_tracker_update(BlockTracker * t, const char * prevhash, int64_t now_ms);

#endif // BLOCK_TRACKER_H_

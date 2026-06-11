#include "block_tracker.h"
#include <string.h>

void block_tracker_init(BlockTracker * t)
{
    t->last_prevhash[0] = '\0';
    t->blocks_seen = 0;
    t->last_change_ms = 0;
    t->initialized = false;
}

bool block_tracker_update(BlockTracker * t, const char * prevhash, int64_t now_ms)
{
    if (prevhash == NULL || prevhash[0] == '\0') {
        return false;
    }

    // The first valid hash establishes the baseline: we are now mining a block,
    // so count it once but there is no prior block to compare against.
    if (!t->initialized) {
        strncpy(t->last_prevhash, prevhash, BLOCK_TRACKER_HASH_LEN);
        t->last_prevhash[BLOCK_TRACKER_HASH_LEN] = '\0';
        t->initialized = true;
        t->blocks_seen = 1;
        t->last_change_ms = now_ms;
        return true;
    }

    if (strncmp(t->last_prevhash, prevhash, BLOCK_TRACKER_HASH_LEN) == 0) {
        return false; // same block, nothing changed
    }

    strncpy(t->last_prevhash, prevhash, BLOCK_TRACKER_HASH_LEN);
    t->last_prevhash[BLOCK_TRACKER_HASH_LEN] = '\0';
    t->blocks_seen++;
    t->last_change_ms = now_ms;
    return true;
}

// Host (native) test for the Solo Sniper block tracker logic.
//   gcc -I main test/native/block_tracker_test_native.c main/block_tracker.c -o bt_native
// Exits non-zero (via assert) on any failure.
#include <stdio.h>
#include <assert.h>
#include "block_tracker.h"

int main(void)
{
    BlockTracker t;
    block_tracker_init(&t);
    assert(t.blocks_seen == 0 && !t.initialized);

    // NULL / empty hashes are ignored
    assert(block_tracker_update(&t, NULL, 10) == false);
    assert(block_tracker_update(&t, "", 10) == false);
    assert(t.blocks_seen == 0);

    // first valid hash counts once
    assert(block_tracker_update(&t, "aaaa", 100) == true);
    assert(t.blocks_seen == 1 && t.last_change_ms == 100);

    // same hash -> not new, time unchanged
    assert(block_tracker_update(&t, "aaaa", 200) == false);
    assert(t.blocks_seen == 1 && t.last_change_ms == 100);

    // new hash -> new, counter + time advance
    assert(block_tracker_update(&t, "bbbb", 300) == true);
    assert(t.blocks_seen == 2 && t.last_change_ms == 300);
    assert(block_tracker_update(&t, "cccc", 450) == true);
    assert(t.blocks_seen == 3);
    assert(block_tracker_update(&t, "cccc", 500) == false);
    assert(t.blocks_seen == 3);

    // full 64-char hashes differing only in the last character
    char h1[BLOCK_TRACKER_HASH_LEN + 1];
    char h2[BLOCK_TRACKER_HASH_LEN + 1];
    for (int i = 0; i < BLOCK_TRACKER_HASH_LEN; i++) { h1[i] = '0'; h2[i] = '0'; }
    h1[BLOCK_TRACKER_HASH_LEN] = '\0';
    h2[BLOCK_TRACKER_HASH_LEN] = '\0';
    h2[BLOCK_TRACKER_HASH_LEN - 1] = '1';
    block_tracker_init(&t);
    assert(block_tracker_update(&t, h1, 1) == true);
    assert(block_tracker_update(&t, h1, 2) == false);
    assert(block_tracker_update(&t, h2, 3) == true);
    assert(t.blocks_seen == 2);

    printf("block_tracker_test_native: ALL ASSERTIONS PASSED\n");
    return 0;
}

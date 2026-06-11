#include "unity.h"
#include "block_tracker.h"

TEST_CASE("block tracker ignores null and empty hashes", "[block_tracker]")
{
    BlockTracker t;
    block_tracker_init(&t);
    TEST_ASSERT_FALSE(block_tracker_update(&t, NULL, 10));
    TEST_ASSERT_FALSE(block_tracker_update(&t, "", 10));
    TEST_ASSERT_EQUAL_UINT32(0, t.blocks_seen);
    TEST_ASSERT_FALSE(t.initialized);
}

TEST_CASE("block tracker counts the first block and updates time", "[block_tracker]")
{
    BlockTracker t;
    block_tracker_init(&t);
    TEST_ASSERT_TRUE(block_tracker_update(&t, "aaaa", 100));
    TEST_ASSERT_EQUAL_UINT32(1, t.blocks_seen);
    TEST_ASSERT_EQUAL_INT64(100, t.last_change_ms);
}

TEST_CASE("block tracker does not re-count the same block", "[block_tracker]")
{
    BlockTracker t;
    block_tracker_init(&t);
    block_tracker_update(&t, "aaaa", 100);
    TEST_ASSERT_FALSE(block_tracker_update(&t, "aaaa", 200));
    TEST_ASSERT_EQUAL_UINT32(1, t.blocks_seen);
    TEST_ASSERT_EQUAL_INT64(100, t.last_change_ms); // unchanged
}

TEST_CASE("block tracker detects a new block and advances counters", "[block_tracker]")
{
    BlockTracker t;
    block_tracker_init(&t);
    block_tracker_update(&t, "aaaa", 100);
    TEST_ASSERT_TRUE(block_tracker_update(&t, "bbbb", 300));
    TEST_ASSERT_EQUAL_UINT32(2, t.blocks_seen);
    TEST_ASSERT_EQUAL_INT64(300, t.last_change_ms);
    TEST_ASSERT_TRUE(block_tracker_update(&t, "cccc", 450));
    TEST_ASSERT_EQUAL_UINT32(3, t.blocks_seen);
}

TEST_CASE("block tracker compares full 64-char hashes", "[block_tracker]")
{
    BlockTracker t;
    block_tracker_init(&t);
    char h1[BLOCK_TRACKER_HASH_LEN + 1];
    char h2[BLOCK_TRACKER_HASH_LEN + 1];
    for (int i = 0; i < BLOCK_TRACKER_HASH_LEN; i++) { h1[i] = '0'; h2[i] = '0'; }
    h1[BLOCK_TRACKER_HASH_LEN] = '\0';
    h2[BLOCK_TRACKER_HASH_LEN] = '\0';
    h2[BLOCK_TRACKER_HASH_LEN - 1] = '1'; // differ only in the last character

    TEST_ASSERT_TRUE(block_tracker_update(&t, h1, 1));
    TEST_ASSERT_FALSE(block_tracker_update(&t, h1, 2));
    TEST_ASSERT_TRUE(block_tracker_update(&t, h2, 3));
    TEST_ASSERT_EQUAL_UINT32(2, t.blocks_seen);
}

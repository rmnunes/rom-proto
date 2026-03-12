#include <gtest/gtest.h>
#include "protocoll/state/resolution.h"
#include <thread>
#include <chrono>

using namespace protocoll;

// --- ResolutionTier ---

TEST(Resolution, TierValues) {
    EXPECT_EQ(static_cast<uint8_t>(ResolutionTier::FULL), 0);
    EXPECT_EQ(static_cast<uint8_t>(ResolutionTier::NORMAL), 1);
    EXPECT_EQ(static_cast<uint8_t>(ResolutionTier::COARSE), 2);
    EXPECT_EQ(static_cast<uint8_t>(ResolutionTier::METADATA), 3);
}

// --- FULL tier ---

TEST(DeltaFilter, FullTierAlwaysForwards) {
    DeltaFilter filter(ResolutionTier::FULL);
    uint8_t data[] = {1, 2, 3};

    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(filter.should_forward(0x1234, data, 3));
    }
    EXPECT_EQ(filter.deltas_forwarded(), 100u);
    EXPECT_EQ(filter.deltas_filtered(), 0u);
}

// --- NORMAL tier (rate limiting) ---

TEST(DeltaFilter, NormalTierRateLimits) {
    ResolutionConfig cfg;
    cfg.normal_max_updates_per_sec = 5;
    DeltaFilter filter(ResolutionTier::NORMAL, cfg);

    uint8_t data[] = {1};
    uint32_t path = 0xAAAA;

    // First 5 should pass
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(filter.should_forward(path, data, 1))
            << "Update " << i << " should pass";
    }

    // 6th should be filtered (within same second)
    EXPECT_FALSE(filter.should_forward(path, data, 1));
    EXPECT_EQ(filter.deltas_forwarded(), 5u);
    EXPECT_EQ(filter.deltas_filtered(), 1u);
}

TEST(DeltaFilter, NormalTierDifferentPathsIndependent) {
    ResolutionConfig cfg;
    cfg.normal_max_updates_per_sec = 2;
    DeltaFilter filter(ResolutionTier::NORMAL, cfg);

    uint8_t data[] = {1};

    // Each path gets its own rate limit
    EXPECT_TRUE(filter.should_forward(0x1111, data, 1));
    EXPECT_TRUE(filter.should_forward(0x2222, data, 1));
    EXPECT_TRUE(filter.should_forward(0x1111, data, 1));
    EXPECT_TRUE(filter.should_forward(0x2222, data, 1));

    // Both at limit now
    EXPECT_FALSE(filter.should_forward(0x1111, data, 1));
    EXPECT_FALSE(filter.should_forward(0x2222, data, 1));
}

TEST(DeltaFilter, NormalTierResetsAfterOneSecond) {
    ResolutionConfig cfg;
    cfg.normal_max_updates_per_sec = 1;
    DeltaFilter filter(ResolutionTier::NORMAL, cfg);

    uint8_t data[] = {1};
    EXPECT_TRUE(filter.should_forward(0x1234, data, 1));
    EXPECT_FALSE(filter.should_forward(0x1234, data, 1));

    // Wait just over 1 second for rate limit reset
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    EXPECT_TRUE(filter.should_forward(0x1234, data, 1));
}

// --- COARSE tier (change threshold) ---

TEST(DeltaFilter, CoarseTierFirstDeltaAlwaysPasses) {
    ResolutionConfig cfg;
    cfg.coarse_min_change_bytes = 4;
    DeltaFilter filter(ResolutionTier::COARSE, cfg);

    uint8_t data[] = {1, 2, 3, 4};
    EXPECT_TRUE(filter.should_forward(0xBBBB, data, 4));
}

TEST(DeltaFilter, CoarseTierFiltersSmallChanges) {
    ResolutionConfig cfg;
    cfg.coarse_min_change_bytes = 3;
    DeltaFilter filter(ResolutionTier::COARSE, cfg);

    uint8_t data1[] = {1, 2, 3, 4, 5};
    uint8_t data2[] = {1, 2, 3, 4, 6}; // 1 byte different
    uint8_t data3[] = {1, 2, 3, 4, 7}; // still only 1 byte different from data2

    EXPECT_TRUE(filter.should_forward(0xCCCC, data1, 5));  // first always passes
    EXPECT_FALSE(filter.should_forward(0xCCCC, data2, 5)); // 1 byte diff < 3
    EXPECT_FALSE(filter.should_forward(0xCCCC, data3, 5)); // still compared to data1 (not stored)
}

TEST(DeltaFilter, CoarseTierForwardsLargeChanges) {
    ResolutionConfig cfg;
    cfg.coarse_min_change_bytes = 2;
    DeltaFilter filter(ResolutionTier::COARSE, cfg);

    uint8_t data1[] = {0x00, 0x00, 0x00, 0x00};
    uint8_t data2[] = {0xFF, 0xFF, 0x00, 0x00}; // 2 bytes different

    EXPECT_TRUE(filter.should_forward(0xDDDD, data1, 4));
    EXPECT_TRUE(filter.should_forward(0xDDDD, data2, 4)); // 2 bytes diff >= threshold
}

TEST(DeltaFilter, CoarseTierDifferentLengthCounts) {
    ResolutionConfig cfg;
    cfg.coarse_min_change_bytes = 2;
    DeltaFilter filter(ResolutionTier::COARSE, cfg);

    uint8_t data1[] = {1, 2};
    uint8_t data2[] = {1, 2, 3, 4}; // 2 extra bytes count as different

    EXPECT_TRUE(filter.should_forward(0xEEEE, data1, 2));
    EXPECT_TRUE(filter.should_forward(0xEEEE, data2, 4)); // 2 extra = 2 diff >= threshold
}

// --- METADATA tier ---

TEST(DeltaFilter, MetadataTierNeverForwardsDelta) {
    DeltaFilter filter(ResolutionTier::METADATA);
    uint8_t data[] = {1, 2, 3};

    EXPECT_FALSE(filter.should_forward(0x1234, data, 3));
    EXPECT_FALSE(filter.should_forward(0x1234, data, 3));
    EXPECT_EQ(filter.deltas_filtered(), 2u);
    EXPECT_EQ(filter.deltas_forwarded(), 0u);
}

TEST(DeltaFilter, MetadataTierTracksVersionInfo) {
    DeltaFilter filter(ResolutionTier::METADATA);
    uint8_t data[] = {1};

    // No version info pending initially
    EXPECT_FALSE(filter.should_send_version_info(0x1234));

    // Filtering a delta marks version as pending
    filter.should_forward(0x1234, data, 1);
    EXPECT_TRUE(filter.should_send_version_info(0x1234));

    // Consuming it clears the pending flag
    EXPECT_FALSE(filter.should_send_version_info(0x1234));
}

TEST(DeltaFilter, MetadataVersionInfoNotForOtherTiers) {
    DeltaFilter filter(ResolutionTier::FULL);
    EXPECT_FALSE(filter.should_send_version_info(0x1234));
}

// --- General ---

TEST(DeltaFilter, SetTierChangesBeahvior) {
    DeltaFilter filter(ResolutionTier::FULL);
    uint8_t data[] = {1};

    EXPECT_TRUE(filter.should_forward(0x1234, data, 1));
    filter.set_tier(ResolutionTier::METADATA);
    EXPECT_FALSE(filter.should_forward(0x1234, data, 1));
}

TEST(DeltaFilter, ResetClearsState) {
    ResolutionConfig cfg;
    cfg.normal_max_updates_per_sec = 1;
    DeltaFilter filter(ResolutionTier::NORMAL, cfg);

    uint8_t data[] = {1};
    filter.should_forward(0x1234, data, 1);
    EXPECT_FALSE(filter.should_forward(0x1234, data, 1));

    filter.reset();
    EXPECT_TRUE(filter.should_forward(0x1234, data, 1));
    EXPECT_EQ(filter.deltas_forwarded(), 1u); // reset cleared stats
}

TEST(DeltaFilter, DefaultConfig) {
    ResolutionConfig cfg;
    EXPECT_EQ(cfg.normal_max_updates_per_sec, 10u);
    EXPECT_EQ(cfg.coarse_min_change_bytes, 4u);
}

#include <gtest/gtest.h>
#include "protocoll/reliability/congestion.h"

using namespace protocoll;

// --- TrendlineEstimator ---

TEST(TrendlineEstimator, InitialSlopeIsZero) {
    TrendlineEstimator est;
    EXPECT_DOUBLE_EQ(est.slope(), 0.0);
}

TEST(TrendlineEstimator, StableDelayProducesLowSlope) {
    TrendlineEstimator est;
    // Constant delay: send_delta == arrival_delta
    for (int i = 0; i < 20; i++) {
        est.update(10.0, 10.0, static_cast<double>(i * 10));
    }
    // Slope should be near zero for constant delay
    EXPECT_NEAR(est.slope(), 0.0, 1.0);
}

TEST(TrendlineEstimator, IncreasingDelayProducesPositiveSlope) {
    TrendlineEstimator est;
    // Arrival delta grows over time (queuing delay building up)
    for (int i = 0; i < 20; i++) {
        double arrival_delta = 10.0 + static_cast<double>(i) * 0.5; // growing
        est.update(arrival_delta, 10.0, static_cast<double>(i * 10));
    }
    EXPECT_GT(est.slope(), 0.0);
}

TEST(TrendlineEstimator, DecreasingDelayProducesNegativeSlope) {
    TrendlineEstimator est;
    // First build up delay, then decrease
    for (int i = 0; i < 10; i++) {
        est.update(15.0, 10.0, static_cast<double>(i * 10));
    }
    for (int i = 10; i < 30; i++) {
        est.update(8.0, 10.0, static_cast<double>(i * 10));
    }
    EXPECT_LT(est.slope(), 0.0);
}

TEST(TrendlineEstimator, ResetClearsState) {
    TrendlineEstimator est;
    est.update(15.0, 10.0, 100.0);
    est.reset();
    EXPECT_DOUBLE_EQ(est.slope(), 0.0);
}

// --- OveruseDetector ---

TEST(OveruseDetector, NormalByDefault) {
    OveruseDetector det;
    EXPECT_EQ(det.state(), BandwidthUsage::NORMAL);
}

TEST(OveruseDetector, DetectsOveruse) {
    OveruseDetector det;
    // Feed large positive slope repeatedly
    for (int i = 0; i < 20; i++) {
        det.detect(20.0);
    }
    EXPECT_EQ(det.state(), BandwidthUsage::OVERUSE);
}

TEST(OveruseDetector, DetectsUnderuse) {
    OveruseDetector det;
    det.detect(-20.0);
    EXPECT_EQ(det.state(), BandwidthUsage::UNDERUSE);
}

TEST(OveruseDetector, ReturnsToNormal) {
    OveruseDetector det;
    for (int i = 0; i < 20; i++) det.detect(20.0);
    EXPECT_EQ(det.state(), BandwidthUsage::OVERUSE);

    det.detect(0.0);
    EXPECT_EQ(det.state(), BandwidthUsage::NORMAL);
}

// --- CongestionController ---

TEST(CongestionController, InitialState) {
    CongestionController cc;
    EXPECT_EQ(cc.state(), CongestionState::SLOW_START);
    EXPECT_GT(cc.send_rate_bps(), 0u);
    EXPECT_GT(cc.cwnd_bytes(), 0u);
    EXPECT_EQ(cc.bytes_in_flight(), 0u);
}

TEST(CongestionController, CanSendInitially) {
    CongestionController cc;
    EXPECT_TRUE(cc.can_send(1200));
}

TEST(CongestionController, TracksBytesInFlight) {
    CongestionController cc;
    cc.on_packet_sent(1, 1000, 1200);
    EXPECT_EQ(cc.bytes_in_flight(), 1200u);

    cc.on_packet_sent(2, 2000, 1200);
    EXPECT_EQ(cc.bytes_in_flight(), 2400u);
}

TEST(CongestionController, AckReducesBytesInFlight) {
    CongestionController cc;
    cc.on_packet_sent(1, 1000, 1200);
    cc.on_packet_sent(2, 2000, 1200);
    EXPECT_EQ(cc.bytes_in_flight(), 2400u);

    cc.on_ack_received(1, 1000, 11000, 500, 1200);
    EXPECT_EQ(cc.bytes_in_flight(), 1200u);
}

TEST(CongestionController, LossDecreasesSendRate) {
    CongestionController cc;
    uint32_t initial_rate = cc.send_rate_bps();

    cc.on_packet_sent(1, 1000, 1200);
    cc.on_packet_lost(50000, 1200);

    EXPECT_LT(cc.send_rate_bps(), initial_rate);
    EXPECT_EQ(cc.state(), CongestionState::DRAIN);
}

TEST(CongestionController, RateNeverBelowMinimum) {
    CongestionController cc;
    // Cause many losses to drive rate down
    for (int i = 0; i < 100; i++) {
        cc.on_packet_sent(static_cast<uint32_t>(i), static_cast<uint32_t>(i * 1000), 1200);
        cc.on_packet_lost(static_cast<uint32_t>(i * 1000 + 500), 1200);
    }
    EXPECT_GE(cc.send_rate_bps(), 10000u); // MIN_RATE_BPS
}

TEST(CongestionController, PacingDelayIsPositive) {
    CongestionController cc;
    EXPECT_GT(cc.pacing_delay_us(), 0u);
}

TEST(CongestionController, SlowStartGrowsRate) {
    CongestionController cc;
    uint32_t rate_before = cc.send_rate_bps();

    // Simulate stable ACKs with low delay
    uint32_t send_ts = 0;
    uint32_t ack_ts = 10000; // 10ms RTT
    for (int i = 0; i < 10; i++) {
        send_ts = static_cast<uint32_t>(i * 10000);
        ack_ts = send_ts + 10000;
        cc.on_packet_sent(static_cast<uint32_t>(i), send_ts, 1200);
        cc.on_ack_received(static_cast<uint32_t>(i), send_ts, ack_ts, 0, 1200);
    }

    EXPECT_GT(cc.send_rate_bps(), rate_before);
    EXPECT_EQ(cc.state(), CongestionState::SLOW_START);
}

TEST(CongestionController, LossExitsSlowStart) {
    CongestionController cc;
    EXPECT_EQ(cc.state(), CongestionState::SLOW_START);

    // Loss is a definitive congestion signal
    cc.on_packet_sent(1, 1000, 1200);
    cc.on_packet_lost(50000, 1200);

    EXPECT_EQ(cc.state(), CongestionState::DRAIN);
}

TEST(CongestionController, ResetRestoresInitialState) {
    CongestionController cc;
    cc.on_packet_sent(1, 1000, 1200);
    cc.on_packet_lost(5000, 1200);

    cc.reset();

    EXPECT_EQ(cc.state(), CongestionState::SLOW_START);
    EXPECT_EQ(cc.bytes_in_flight(), 0u);
}

#include <gtest/gtest.h>
#include "protocoll/state/subscription.h"

using namespace protocoll;

TEST(SubscriptionManager, SubscribeAndMatch) {
    SubscriptionManager mgr;
    StatePath pattern("/app/users/*/status");
    uint32_t sid = mgr.subscribe(1, pattern);
    EXPECT_EQ(mgr.count(), 1u);

    StatePath path("/app/users/alice/status");
    auto matches = mgr.match(path);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].sub_id, sid);
    EXPECT_EQ(matches[0].conn_id, 1);
}

TEST(SubscriptionManager, NoMatchWrongPath) {
    SubscriptionManager mgr;
    mgr.subscribe(1, StatePath("/app/users/*/status"));

    auto matches = mgr.match(StatePath("/app/users/alice/profile"));
    EXPECT_TRUE(matches.empty());
}

TEST(SubscriptionManager, MultipleSubscribers) {
    SubscriptionManager mgr;
    mgr.subscribe(1, StatePath("/game/score"));
    mgr.subscribe(2, StatePath("/game/score"));
    mgr.subscribe(3, StatePath("/game/player/*/pos"));

    auto matches = mgr.match(StatePath("/game/score"));
    EXPECT_EQ(matches.size(), 2u);

    auto matches2 = mgr.match(StatePath("/game/player/7/pos"));
    EXPECT_EQ(matches2.size(), 1u);
}

TEST(SubscriptionManager, Unsubscribe) {
    SubscriptionManager mgr;
    uint32_t sid = mgr.subscribe(1, StatePath("/app/data"));
    EXPECT_EQ(mgr.count(), 1u);

    ASSERT_TRUE(mgr.unsubscribe(sid));
    EXPECT_EQ(mgr.count(), 0u);

    EXPECT_FALSE(mgr.unsubscribe(sid)); // Already gone
}

TEST(SubscriptionManager, RemoveConnection) {
    SubscriptionManager mgr;
    mgr.subscribe(1, StatePath("/a"));
    mgr.subscribe(1, StatePath("/b"));
    mgr.subscribe(2, StatePath("/c"));
    EXPECT_EQ(mgr.count(), 3u);

    mgr.remove_connection(1);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.count_for_connection(1), 0u);
    EXPECT_EQ(mgr.count_for_connection(2), 1u);
}

TEST(SubscriptionManager, CreditBasedBackpressure) {
    SubscriptionManager mgr;
    uint32_t sid = mgr.subscribe(1, StatePath("/data"), 3); // 3 credits

    StatePath path("/data");

    // First 3 matches consume credits
    for (int i = 0; i < 3; i++) {
        auto m = mgr.match_and_consume(path);
        ASSERT_EQ(m.size(), 1u) << "Iteration " << i;
    }

    // 4th match: no credits left
    auto m = mgr.match_and_consume(path);
    EXPECT_TRUE(m.empty());

    // Replenish
    ASSERT_TRUE(mgr.add_credits(sid, 2));
    m = mgr.match_and_consume(path);
    EXPECT_EQ(m.size(), 1u);
}

TEST(SubscriptionManager, UnlimitedCredits) {
    SubscriptionManager mgr;
    mgr.subscribe(1, StatePath("/data"), -1); // Unlimited

    StatePath path("/data");
    for (int i = 0; i < 1000; i++) {
        auto m = mgr.match_and_consume(path);
        ASSERT_EQ(m.size(), 1u);
    }
}

TEST(SubscriptionManager, GetSubscription) {
    SubscriptionManager mgr;
    uint32_t sid = mgr.subscribe(1, StatePath("/test"), 100, 5000);

    auto* sub = mgr.get(sid);
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->conn_id, 1);
    EXPECT_EQ(sub->credits, 100);
    EXPECT_EQ(sub->freshness_us, 5000u);
    EXPECT_EQ(sub->pattern.str(), "/test");
}

// --- Wire format tests ---

TEST(SubscribePayload, RoundTrip) {
    SubscribeFramePayload payload;
    payload.sub_id = 42;
    payload.initial_credits = 100;
    payload.freshness_us = 5000;
    payload.pattern = "/app/users/*/status";

    std::vector<uint8_t> buf(payload.wire_size());
    size_t written = 0;
    ASSERT_TRUE(payload.encode(buf.data(), buf.size(), written));
    EXPECT_EQ(written, payload.wire_size());

    SubscribeFramePayload decoded;
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));
    EXPECT_EQ(decoded.sub_id, 42u);
    EXPECT_EQ(decoded.initial_credits, 100);
    EXPECT_EQ(decoded.freshness_us, 5000u);
    EXPECT_EQ(decoded.tier, ResolutionTier::FULL);
    EXPECT_EQ(decoded.pattern, "/app/users/*/status");
}

TEST(UnsubscribePayload, RoundTrip) {
    UnsubscribeFramePayload payload{99};
    uint8_t buf[UnsubscribeFramePayload::WIRE_SIZE];
    ASSERT_TRUE(payload.encode(buf, sizeof(buf)));

    UnsubscribeFramePayload decoded;
    ASSERT_TRUE(decoded.decode(buf, sizeof(buf)));
    EXPECT_EQ(decoded.sub_id, 99u);
}

TEST(CreditPayload, RoundTrip) {
    CreditFramePayload payload{42, 500};
    uint8_t buf[CreditFramePayload::WIRE_SIZE];
    ASSERT_TRUE(payload.encode(buf, sizeof(buf)));

    CreditFramePayload decoded;
    ASSERT_TRUE(decoded.decode(buf, sizeof(buf)));
    EXPECT_EQ(decoded.sub_id, 42u);
    EXPECT_EQ(decoded.credits, 500);
}

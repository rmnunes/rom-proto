#include <gtest/gtest.h>
#include "protocoll/relay/relay_node.h"
#include "protocoll/relay/aggregator.h"
#include "protocoll/state/crdt/lww_register.h"
#include "protocoll/state/crdt/g_counter.h"

using namespace protocoll;

// --- Aggregator tests ---

TEST(Aggregator, IngestAndFlush) {
    Aggregator agg;
    uint32_t emitted_path = 0;
    size_t emitted_len = 0;

    agg.set_emit_callback([&](uint32_t path, CrdtType, const uint8_t*, size_t len) {
        emitted_path = path;
        emitted_len = len;
    });

    // Create a valid LWW delta: [uint32_t timestamp][uint16_t node_id][value]
    LwwRegister lww(1);
    uint8_t val[] = {0xAA, 0xBB};
    lww.set(val, 2, 100);
    auto delta = lww.snapshot();

    agg.ingest(0x1234, CrdtType::LWW_REGISTER, delta.data(), delta.size());
    EXPECT_EQ(agg.pending_path_count(), 1u);
    EXPECT_EQ(agg.total_ingested(), 1u);

    size_t flushed = agg.flush();
    EXPECT_EQ(flushed, 1u);
    EXPECT_EQ(emitted_path, 0x1234u);
    EXPECT_GT(emitted_len, 0u);
    EXPECT_EQ(agg.total_emitted(), 1u);
}

TEST(Aggregator, MergesMultipleDeltas) {
    Aggregator agg;
    std::vector<uint8_t> emitted_data;

    agg.set_emit_callback([&](uint32_t, CrdtType, const uint8_t* data, size_t len) {
        emitted_data.assign(data, data + len);
    });

    // Two GCounter deltas from different nodes
    GCounter c1(1);
    c1.increment(5);
    auto d1 = c1.snapshot();

    GCounter c2(2);
    c2.increment(3);
    auto d2 = c2.snapshot();

    agg.ingest(0x1234, CrdtType::G_COUNTER, d1.data(), d1.size());
    agg.ingest(0x1234, CrdtType::G_COUNTER, d2.data(), d2.size());

    EXPECT_EQ(agg.total_ingested(), 2u);

    agg.flush();

    // The merged result should be a GCounter with total = 5 + 3 = 8
    GCounter merged(0);
    merged.merge(emitted_data.data(), emitted_data.size());
    EXPECT_EQ(merged.value(), 8u);
}

TEST(Aggregator, AutoFlushOnBatchSize) {
    AggregatorConfig cfg;
    cfg.max_batch_size = 3;
    Aggregator agg(cfg);

    int emit_count = 0;
    agg.set_emit_callback([&](uint32_t, CrdtType, const uint8_t*, size_t) {
        emit_count++;
    });

    LwwRegister lww(1);
    uint8_t v[] = {0x42};
    lww.set(v, 1, 100);
    auto d = lww.snapshot();

    // Ingest 3 deltas — should auto-flush at batch size
    agg.ingest(0x1234, CrdtType::LWW_REGISTER, d.data(), d.size());
    agg.ingest(0x1234, CrdtType::LWW_REGISTER, d.data(), d.size());
    EXPECT_EQ(emit_count, 0);

    agg.ingest(0x1234, CrdtType::LWW_REGISTER, d.data(), d.size());
    EXPECT_EQ(emit_count, 1);
    EXPECT_EQ(agg.pending_path_count(), 0u);
}

TEST(Aggregator, MultiplePaths) {
    Aggregator agg;
    std::vector<uint32_t> emitted_paths;

    agg.set_emit_callback([&](uint32_t path, CrdtType, const uint8_t*, size_t) {
        emitted_paths.push_back(path);
    });

    LwwRegister lww(1);
    uint8_t v[] = {0x11};
    lww.set(v, 1, 100);
    auto d = lww.snapshot();

    agg.ingest(0x1111, CrdtType::LWW_REGISTER, d.data(), d.size());
    agg.ingest(0x2222, CrdtType::LWW_REGISTER, d.data(), d.size());
    agg.ingest(0x3333, CrdtType::LWW_REGISTER, d.data(), d.size());

    EXPECT_EQ(agg.pending_path_count(), 3u);
    size_t flushed = agg.flush();
    EXPECT_EQ(flushed, 3u);
    EXPECT_EQ(emitted_paths.size(), 3u);
}

TEST(Aggregator, Reset) {
    Aggregator agg;
    LwwRegister lww(1);
    uint8_t v[] = {0x42};
    lww.set(v, 1, 100);
    auto d = lww.snapshot();

    agg.ingest(0x1234, CrdtType::LWW_REGISTER, d.data(), d.size());
    agg.reset();

    EXPECT_EQ(agg.pending_path_count(), 0u);
    EXPECT_EQ(agg.total_ingested(), 0u);
    EXPECT_EQ(agg.total_emitted(), 0u);
}

TEST(Aggregator, FlushEmptyNoEmit) {
    Aggregator agg;
    int emit_count = 0;
    agg.set_emit_callback([&](uint32_t, CrdtType, const uint8_t*, size_t) {
        emit_count++;
    });

    EXPECT_EQ(agg.flush(), 0u);
    EXPECT_EQ(emit_count, 0);
}

TEST(Aggregator, NoCallbackNoEmit) {
    Aggregator agg;
    LwwRegister lww(1);
    uint8_t v[] = {0x42};
    lww.set(v, 1, 100);
    auto d = lww.snapshot();

    agg.ingest(0x1234, CrdtType::LWW_REGISTER, d.data(), d.size());
    // No callback set — should not crash
    agg.flush();
    EXPECT_EQ(agg.total_emitted(), 0u);
}

TEST(Aggregator, ConfigDefaults) {
    AggregatorConfig cfg;
    EXPECT_EQ(cfg.max_batch_size, 10u);
    EXPECT_EQ(cfg.max_batch_age_us, 50000u);
}

// --- RelayNode tests ---

TEST(RelayNode, CreateWithNodeId) {
    RelayNode relay(42);
    EXPECT_EQ(relay.node_id(), 42);
    EXPECT_EQ(relay.deltas_received(), 0u);
    EXPECT_EQ(relay.deltas_forwarded(), 0u);
}

TEST(RelayNode, ReceiveAndForward) {
    RelayNode relay(1);

    uint32_t forwarded_path = 0;
    std::vector<uint8_t> forwarded_data;

    relay.set_forward_callback([&](uint32_t path, CrdtType, const uint8_t* data, size_t len) {
        forwarded_path = path;
        forwarded_data.assign(data, data + len);
    });

    // Create a valid LWW delta
    LwwRegister lww(2);
    uint8_t val[] = {0xDE, 0xAD};
    lww.set(val, 2, 200);
    auto delta = lww.snapshot();

    relay.receive_delta(0xAAAA, CrdtType::LWW_REGISTER, delta.data(), delta.size(), 2);
    EXPECT_EQ(relay.deltas_received(), 1u);

    // Flush to trigger forwarding
    relay.flush();
    relay.tick(); // Process forward queue

    EXPECT_EQ(forwarded_path, 0xAAAAu);
    EXPECT_FALSE(forwarded_data.empty());
    EXPECT_EQ(relay.deltas_forwarded(), 1u);
}

TEST(RelayNode, MergesBeforeForwarding) {
    RelayNode relay(1);

    std::vector<uint8_t> forwarded_data;
    int forward_count = 0;

    relay.set_forward_callback([&](uint32_t, CrdtType, const uint8_t* data, size_t len) {
        forwarded_data.assign(data, data + len);
        forward_count++;
    });

    // Two G-Counter deltas from different sources
    GCounter c1(10);
    c1.increment(7);
    auto d1 = c1.snapshot();

    GCounter c2(20);
    c2.increment(3);
    auto d2 = c2.snapshot();

    relay.receive_delta(0x5555, CrdtType::G_COUNTER, d1.data(), d1.size(), 10);
    relay.receive_delta(0x5555, CrdtType::G_COUNTER, d2.data(), d2.size(), 20);

    relay.flush();
    relay.tick();

    // Should emit 1 merged delta, not 2
    EXPECT_EQ(forward_count, 1);

    // Verify merged counter = 7 + 3 = 10
    GCounter result(0);
    result.merge(forwarded_data.data(), forwarded_data.size());
    EXPECT_EQ(result.value(), 10u);
}

TEST(RelayNode, DownstreamTracking) {
    RelayNode relay(1);
    EXPECT_FALSE(relay.is_downstream(5));

    relay.add_downstream(5);
    EXPECT_TRUE(relay.is_downstream(5));

    relay.remove_downstream(5);
    EXPECT_FALSE(relay.is_downstream(5));
}

TEST(RelayNode, StatsTracking) {
    RelayNode relay(1);
    relay.set_forward_callback([](uint32_t, CrdtType, const uint8_t*, size_t) {});

    LwwRegister lww(2);
    uint8_t val[] = {0x42};
    lww.set(val, 1, 100);
    auto d = lww.snapshot();

    relay.receive_delta(0x1111, CrdtType::LWW_REGISTER, d.data(), d.size(), 2);
    relay.receive_delta(0x2222, CrdtType::LWW_REGISTER, d.data(), d.size(), 3);

    EXPECT_EQ(relay.deltas_received(), 2u);

    relay.flush();
    relay.tick();

    EXPECT_EQ(relay.deltas_forwarded(), 2u);
}

TEST(RelayNode, ConfigUpdate) {
    RelayNode relay(1);

    RelayConfig cfg;
    cfg.aggregator.max_batch_size = 5;
    cfg.forward_tier = ResolutionTier::NORMAL;
    relay.set_config(cfg);

    EXPECT_EQ(relay.config().aggregator.max_batch_size, 5u);
    EXPECT_EQ(relay.config().forward_tier, ResolutionTier::NORMAL);
}

TEST(RelayNode, RelayConfigDefaults) {
    RelayConfig cfg;
    EXPECT_EQ(cfg.forward_tier, ResolutionTier::FULL);
    EXPECT_FALSE(cfg.store_local);
}

// --- Integration: A -> Relay -> B ---

TEST(RelayNode, ThreeNodePipeline) {
    // Simulate: Node A sends GCounter updates through relay to Node B

    // Node A: source
    GCounter counter_a(10);
    counter_a.increment(100);

    // Relay: receives from A, merges, forwards to B
    RelayNode relay(1);
    std::vector<uint8_t> to_b_data;
    uint32_t to_b_path = 0;

    relay.set_forward_callback([&](uint32_t path, CrdtType, const uint8_t* data, size_t len) {
        to_b_path = path;
        to_b_data.assign(data, data + len);
    });

    // A sends initial state
    auto delta_a = counter_a.snapshot();
    relay.receive_delta(0x9999, CrdtType::G_COUNTER, delta_a.data(), delta_a.size(), 10);

    // Another node C also increments
    GCounter counter_c(20);
    counter_c.increment(50);
    auto delta_c = counter_c.snapshot();
    relay.receive_delta(0x9999, CrdtType::G_COUNTER, delta_c.data(), delta_c.size(), 20);

    // Relay flushes merged state
    relay.flush();
    relay.tick();

    // Node B receives the merged delta
    GCounter counter_b(30);
    ASSERT_FALSE(to_b_data.empty());
    counter_b.merge(to_b_data.data(), to_b_data.size());

    // B should see total = 100 + 50 = 150
    EXPECT_EQ(counter_b.value(), 150u);
    EXPECT_EQ(to_b_path, 0x9999u);
}

TEST(RelayNode, BandwidthReduction) {
    // Verify that N ingested deltas produce only 1 emitted delta per path
    AggregatorConfig acfg;
    acfg.max_batch_size = 100; // high batch size to force manual flush

    RelayConfig cfg;
    cfg.aggregator = acfg;
    RelayNode relay(1, cfg);

    int forward_count = 0;
    relay.set_forward_callback([&](uint32_t, CrdtType, const uint8_t*, size_t) {
        forward_count++;
    });

    // 10 deltas for the same path
    for (int i = 0; i < 10; i++) {
        GCounter c(static_cast<uint16_t>(i + 1));
        c.increment(1);
        auto d = c.snapshot();
        relay.receive_delta(0x7777, CrdtType::G_COUNTER, d.data(), d.size(),
                            static_cast<uint16_t>(i + 1));
    }

    EXPECT_EQ(relay.deltas_received(), 10u);
    relay.flush();
    relay.tick();

    // Only 1 merged delta emitted despite 10 inputs
    EXPECT_EQ(forward_count, 1);
}

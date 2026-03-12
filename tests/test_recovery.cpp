#include <gtest/gtest.h>
#include "protocoll/connection/recovery.h"
#include "protocoll/state/state_registry.h"
#include "protocoll/state/event_log.h"
#include "protocoll/state/crdt/lww_register.h"

using namespace protocoll;

TEST(RecoveryRequest, EncodeDecodeRoundTrip) {
    RecoveryRequest req;
    VersionVector vv1; vv1.set(1, 5); vv1.set(2, 3);
    VersionVector vv2; vv2.set(1, 10);

    req.known_versions[0xAAAA] = vv1;
    req.known_versions[0xBBBB] = vv2;

    auto buf = req.encode();

    RecoveryRequest decoded;
    ASSERT_TRUE(RecoveryRequest::decode(buf.data(), buf.size(), decoded));
    ASSERT_EQ(decoded.known_versions.size(), 2u);

    EXPECT_EQ(decoded.known_versions[0xAAAA].get(1), 5u);
    EXPECT_EQ(decoded.known_versions[0xAAAA].get(2), 3u);
    EXPECT_EQ(decoded.known_versions[0xBBBB].get(1), 10u);
}

TEST(RecoveryRequest, EmptyRoundTrip) {
    RecoveryRequest req;
    auto buf = req.encode();

    RecoveryRequest decoded;
    ASSERT_TRUE(RecoveryRequest::decode(buf.data(), buf.size(), decoded));
    EXPECT_TRUE(decoded.known_versions.empty());
}

TEST(RecoveryManager, FreshClientGetsSnapshot) {
    StateRegistry registry(1);
    StatePath path("/app/data");
    registry.declare(path, CrdtType::LWW_REGISTER, Reliability::RELIABLE);

    EventLog log;
    VersionVector v1; v1.set(1, 1);
    uint8_t d[] = {1, 2, 3};
    log.append_delta(path.hash(), CrdtType::LWW_REGISTER, v1, d, 3, 1000);

    // Fresh client: no known versions
    RecoveryRequest req;
    auto plan = RecoveryManager::compute_plan(req, log, registry);

    ASSERT_EQ(plan.paths.size(), 1u);
    EXPECT_TRUE(plan.paths[0].needs_snapshot); // No prior state → snapshot
}

TEST(RecoveryManager, ReturningClientGetsDelta) {
    StateRegistry registry(1);
    StatePath path("/app/data");
    registry.declare(path, CrdtType::LWW_REGISTER, Reliability::RELIABLE);

    EventLog log;
    VersionVector v1; v1.set(1, 1);
    VersionVector v2; v2.set(1, 2);
    VersionVector v3; v3.set(1, 3);
    uint8_t d[] = {0};
    log.append_delta(path.hash(), CrdtType::LWW_REGISTER, v1, d, 1, 1000);
    log.append_delta(path.hash(), CrdtType::LWW_REGISTER, v2, d, 1, 2000);
    log.append_delta(path.hash(), CrdtType::LWW_REGISTER, v3, d, 1, 3000);

    // Client knows v1 — should get v2 and v3 as deltas
    RecoveryRequest req;
    req.known_versions[path.hash()] = v1;

    auto plan = RecoveryManager::compute_plan(req, log, registry);
    ASSERT_EQ(plan.paths.size(), 1u);
    EXPECT_FALSE(plan.paths[0].needs_snapshot);
    EXPECT_EQ(plan.paths[0].entries.size(), 2u); // v2 and v3
}

TEST(RecoveryManager, TooManyDeltasFallsBackToSnapshot) {
    StateRegistry registry(1);
    StatePath path("/app/data");
    registry.declare(path, CrdtType::LWW_REGISTER, Reliability::RELIABLE);

    EventLog log;
    uint8_t d[] = {0};
    for (int i = 1; i <= 100; i++) {
        VersionVector v; v.set(1, static_cast<uint32_t>(i));
        log.append_delta(path.hash(), CrdtType::LWW_REGISTER, v, d, 1, i * 1000);
    }

    // Client knows nothing — 100 deltas exceeds max_delta_entries(50)
    RecoveryRequest req;
    req.known_versions[path.hash()] = VersionVector{}; // Empty = knows nothing

    auto plan = RecoveryManager::compute_plan(req, log, registry, 50);
    ASSERT_EQ(plan.paths.size(), 1u);
    EXPECT_TRUE(plan.paths[0].needs_snapshot);
}

TEST(RecoveryManager, BuildRequest) {
    StateRegistry registry(1);
    StatePath p1("/app/a");
    StatePath p2("/app/b");
    registry.declare(p1, CrdtType::LWW_REGISTER, Reliability::RELIABLE);
    registry.declare(p2, CrdtType::G_COUNTER, Reliability::RELIABLE);

    // Simulate some state changes
    auto* r1 = registry.get(p1);
    r1->version.set(1, 5);
    auto* r2 = registry.get(p2);
    r2->version.set(1, 3);

    auto req = RecoveryManager::build_request(registry);
    EXPECT_EQ(req.known_versions.size(), 2u);
    EXPECT_EQ(req.known_versions[p1.hash()].get(1), 5u);
    EXPECT_EQ(req.known_versions[p2.hash()].get(1), 3u);
}

#include <gtest/gtest.h>
#include "protocoll/routing/router.h"
#include "protocoll/routing/route_table.h"

using namespace protocoll;

// --- RouteTable tests ---

TEST(RouteTable, AddAndGetRoute) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.5);

    auto routes = table.get_routes(0x1234);
    ASSERT_EQ(routes.size(), 1u);
    EXPECT_EQ(routes[0].next_hop_node_id, 2);
    EXPECT_DOUBLE_EQ(routes[0].weight, 0.5);
}

TEST(RouteTable, DuplicateAddIgnored) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.5);
    table.add_route(0x1234, 2, 0.9); // duplicate, should not override
    EXPECT_EQ(table.total_routes(), 1u);
}

TEST(RouteTable, MultipleRoutesPerPath) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.3);
    table.add_route(0x1234, 3, 0.7);
    table.add_route(0x1234, 4, 0.5);

    auto routes = table.get_routes(0x1234);
    ASSERT_EQ(routes.size(), 3u);
    // Should be sorted by weight descending
    EXPECT_EQ(routes[0].next_hop_node_id, 3); // 0.7
    EXPECT_EQ(routes[1].next_hop_node_id, 4); // 0.5
    EXPECT_EQ(routes[2].next_hop_node_id, 2); // 0.3
}

TEST(RouteTable, BestRoute) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.3);
    table.add_route(0x1234, 3, 0.9);

    auto best = table.best_route(0x1234);
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->next_hop_node_id, 3);
    EXPECT_DOUBLE_EQ(best->weight, 0.9);
}

TEST(RouteTable, BestRouteEmpty) {
    RouteTable table;
    EXPECT_FALSE(table.best_route(0x9999).has_value());
}

TEST(RouteTable, RemoveRoute) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.5);
    table.add_route(0x1234, 3, 0.7);

    EXPECT_TRUE(table.remove_route(0x1234, 2));
    EXPECT_EQ(table.total_routes(), 1u);

    auto routes = table.get_routes(0x1234);
    ASSERT_EQ(routes.size(), 1u);
    EXPECT_EQ(routes[0].next_hop_node_id, 3);
}

TEST(RouteTable, RemoveNonexistent) {
    RouteTable table;
    EXPECT_FALSE(table.remove_route(0x1234, 99));
}

TEST(RouteTable, RemoveNode) {
    RouteTable table;
    table.add_route(0x1111, 5, 0.5);
    table.add_route(0x2222, 5, 0.7);
    table.add_route(0x2222, 6, 0.3);

    table.remove_node(5);
    EXPECT_EQ(table.total_routes(), 1u);
    EXPECT_EQ(table.get_routes(0x1111).size(), 0u);
    EXPECT_EQ(table.get_routes(0x2222).size(), 1u);
}

TEST(RouteTable, RoutesAboveThreshold) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.1);
    table.add_route(0x1234, 3, 0.5);
    table.add_route(0x1234, 4, 0.8);

    auto above = table.routes_above(0x1234, 0.4);
    EXPECT_EQ(above.size(), 2u);
    EXPECT_EQ(above[0].next_hop_node_id, 4);
    EXPECT_EQ(above[1].next_hop_node_id, 3);
}

TEST(RouteTable, OnSuccessUpdatesLatency) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.5);

    table.on_success(0x1234, 2, 1000);
    auto routes = table.get_routes(0x1234);
    EXPECT_GT(routes[0].latency_ema_us, 0);
    EXPECT_EQ(routes[0].success_count, 1u);
}

TEST(RouteTable, OnFailureTracksCount) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.5);

    table.on_failure(0x1234, 2);
    auto routes = table.get_routes(0x1234);
    EXPECT_EQ(routes[0].failure_count, 1u);
}

TEST(RouteTable, DecayAll) {
    RouteTable table;
    table.add_route(0x1234, 2, 1.0);
    table.add_route(0x1234, 3, 0.5);

    table.decay_all(0.9);
    auto routes = table.get_routes(0x1234);
    EXPECT_NEAR(routes[0].weight, 0.9, 0.001);
    EXPECT_NEAR(routes[1].weight, 0.45, 0.001);
}

TEST(RouteTable, Clear) {
    RouteTable table;
    table.add_route(0x1234, 2, 0.5);
    table.add_route(0x5678, 3, 0.7);
    table.clear();
    EXPECT_EQ(table.total_routes(), 0u);
    EXPECT_EQ(table.path_count(), 0u);
}

// --- Router tests ---

TEST(Router, CreateWithNodeId) {
    Router router(1);
    EXPECT_EQ(router.local_node_id(), 1);
}

TEST(Router, AnnouncePath) {
    Router router(1);
    router.announce_path(0xAAAA);
    EXPECT_EQ(router.local_paths().size(), 1u);
    EXPECT_EQ(router.local_paths()[0], 0xAAAAu);
}

TEST(Router, AnnounceCallback) {
    Router router(1);
    uint32_t announced_hash = 0;
    uint16_t announced_node = 0;
    router.set_announce_callback([&](uint32_t hash, uint16_t node) {
        announced_hash = hash;
        announced_node = node;
    });

    router.announce_path(0xBBBB);
    EXPECT_EQ(announced_hash, 0xBBBBu);
    EXPECT_EQ(announced_node, 1);
}

TEST(Router, LearnRoute) {
    Router router(1);
    router.learn_route(0x1234, 2);
    EXPECT_TRUE(router.has_route(0x1234));
}

TEST(Router, LearnRouteSelfIgnored) {
    Router router(1);
    router.learn_route(0x1234, 1); // self
    EXPECT_FALSE(router.has_route(0x1234));
}

TEST(Router, SelectNextHops) {
    Router router(1);
    router.learn_route(0x1234, 2);
    router.learn_route(0x1234, 3);

    auto hops = router.select_next_hops(0x1234);
    EXPECT_GE(hops.size(), 1u);
    EXPECT_LE(hops.size(), 3u);
}

TEST(Router, SelectNextHopsEmpty) {
    Router router(1);
    EXPECT_TRUE(router.select_next_hops(0x9999).empty());
}

TEST(Router, HebbianSuccess) {
    Router router(1);
    router.learn_route(0x1234, 2);

    auto initial_weight = router.route_table().best_route(0x1234)->weight;
    router.on_delivery_success(2, 0x1234, 1000);

    auto new_weight = router.route_table().best_route(0x1234)->weight;
    EXPECT_GT(new_weight, initial_weight);
}

TEST(Router, HebbianFailure) {
    Router router(1);
    RouterConfig cfg;
    cfg.failure_decrement = 0.05;
    router.set_config(cfg);

    router.learn_route(0x1234, 2);

    auto initial_weight = router.route_table().best_route(0x1234)->weight;
    router.on_delivery_failure(2, 0x1234);

    auto new_route = router.route_table().best_route(0x1234);
    if (new_route.has_value()) {
        EXPECT_LT(new_route->weight, initial_weight);
    }
    // If weight dropped below min_weight, route was removed
}

TEST(Router, RepeatedFailureRemovesRoute) {
    Router router(1);
    RouterConfig cfg;
    cfg.failure_decrement = 0.5;
    cfg.min_weight = 0.01;
    router.set_config(cfg);

    router.learn_route(0x1234, 2);

    // Multiple failures should eventually kill the route
    for (int i = 0; i < 10; i++) {
        router.on_delivery_failure(2, 0x1234);
    }

    EXPECT_FALSE(router.has_route(0x1234));
}

TEST(Router, RemoveNodeClearsRoutes) {
    Router router(1);
    router.learn_route(0x1234, 2);
    router.learn_route(0x5678, 2);
    router.learn_route(0x5678, 3);

    router.remove_node(2);
    EXPECT_FALSE(router.has_route(0x1234));
    EXPECT_TRUE(router.has_route(0x5678)); // Still has route via 3
}

TEST(Router, Tick) {
    Router router(1);
    router.learn_route(0x1234, 2);

    auto before = router.route_table().best_route(0x1234)->weight;
    router.tick();
    auto after = router.route_table().best_route(0x1234)->weight;

    EXPECT_LT(after, before); // Decay reduces weight
}

TEST(Router, WeightClamp) {
    Router router(1);
    router.learn_route(0x1234, 2);

    // Many successes should not exceed max_weight
    for (int i = 0; i < 100; i++) {
        router.on_delivery_success(2, 0x1234, 500);
    }

    auto weight = router.route_table().best_route(0x1234)->weight;
    EXPECT_LE(weight, router.config().max_weight);
}

TEST(Router, DefaultConfig) {
    RouterConfig cfg;
    EXPECT_DOUBLE_EQ(cfg.success_increment, 0.1);
    EXPECT_DOUBLE_EQ(cfg.failure_decrement, 0.2);
    EXPECT_DOUBLE_EQ(cfg.decay_factor, 0.995);
    EXPECT_DOUBLE_EQ(cfg.min_route_weight, 0.1);
    EXPECT_EQ(cfg.max_routes_per_path, 3);
}

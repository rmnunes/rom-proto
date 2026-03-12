#include <gtest/gtest.h>
#include "protocoll/state/observer.h"
#include <string>
#include <vector>

using namespace protocoll;

class ObserverTest : public ::testing::Test {
protected:
    StateObserver obs;

    StateChangeEvent make_event(const StatePath& path, CrdtType type = CrdtType::LWW_REGISTER) {
        return StateChangeEvent{path, path.hash(), type, nullptr, 0, 0};
    }
};

TEST_F(ObserverTest, WatchAndNotify) {
    StatePath pattern("/game/player/1/pos");
    int count = 0;
    obs.watch(pattern, [&](const StateChangeEvent&) { count++; });

    StatePath path("/game/player/1/pos");
    auto event = make_event(path);
    obs.notify(event);

    EXPECT_EQ(count, 1);
}

TEST_F(ObserverTest, NoMatchNoNotify) {
    StatePath pattern("/game/player/1/pos");
    int count = 0;
    obs.watch(pattern, [&](const StateChangeEvent&) { count++; });

    StatePath other("/game/player/2/pos");
    auto event = make_event(other);
    obs.notify(event);

    EXPECT_EQ(count, 0);
}

TEST_F(ObserverTest, WildcardMatch) {
    StatePath pattern("/game/player/*/pos");
    std::vector<std::string> paths_received;
    obs.watch(pattern, [&](const StateChangeEvent& e) {
        paths_received.push_back(e.path.str());
    });

    StatePath p1("/game/player/1/pos");
    StatePath p2("/game/player/2/pos");
    StatePath p3("/game/player/3/hp");

    obs.notify(make_event(p1));
    obs.notify(make_event(p2));
    obs.notify(make_event(p3)); // Should not match

    EXPECT_EQ(paths_received.size(), 2u);
    EXPECT_EQ(paths_received[0], "/game/player/1/pos");
    EXPECT_EQ(paths_received[1], "/game/player/2/pos");
}

TEST_F(ObserverTest, CrdtTypeFilter) {
    StatePath pattern("/app/data");
    int count = 0;
    WatchOptions opts;
    opts.crdt_filter = CrdtType::G_COUNTER;
    obs.watch(pattern, [&](const StateChangeEvent&) { count++; }, opts);

    StatePath path("/app/data");
    obs.notify(make_event(path, CrdtType::LWW_REGISTER)); // Filtered out
    EXPECT_EQ(count, 0);

    obs.notify(make_event(path, CrdtType::G_COUNTER)); // Passes filter
    EXPECT_EQ(count, 1);
}

TEST_F(ObserverTest, OneShotWatch) {
    StatePath pattern("/events/click");
    int count = 0;
    WatchOptions opts;
    opts.max_notifications = 1;
    obs.watch(pattern, [&](const StateChangeEvent&) { count++; }, opts);

    StatePath path("/events/click");
    obs.notify(make_event(path));
    obs.notify(make_event(path));
    obs.notify(make_event(path));

    EXPECT_EQ(count, 1);
    EXPECT_EQ(obs.count(), 0u); // Auto-removed
}

TEST_F(ObserverTest, NShotWatch) {
    StatePath pattern("/events/tick");
    int count = 0;
    WatchOptions opts;
    opts.max_notifications = 3;
    obs.watch(pattern, [&](const StateChangeEvent&) { count++; }, opts);

    StatePath path("/events/tick");
    for (int i = 0; i < 5; i++) {
        obs.notify(make_event(path));
    }

    EXPECT_EQ(count, 3);
    EXPECT_EQ(obs.count(), 0u);
}

TEST_F(ObserverTest, Unwatch) {
    StatePath pattern("/test");
    int count = 0;
    auto h = obs.watch(pattern, [&](const StateChangeEvent&) { count++; });

    StatePath path("/test");
    obs.notify(make_event(path));
    EXPECT_EQ(count, 1);

    EXPECT_TRUE(obs.unwatch(h));

    obs.notify(make_event(path));
    EXPECT_EQ(count, 1); // No more notifications
}

TEST_F(ObserverTest, UnwatchInvalidHandle) {
    EXPECT_FALSE(obs.unwatch(999));
}

TEST_F(ObserverTest, MultipleWatchersSamePath) {
    StatePath pattern("/shared/data");
    int count_a = 0, count_b = 0;
    obs.watch(pattern, [&](const StateChangeEvent&) { count_a++; });
    obs.watch(pattern, [&](const StateChangeEvent&) { count_b++; });

    StatePath path("/shared/data");
    obs.notify(make_event(path));

    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
}

TEST_F(ObserverTest, Clear) {
    StatePath p1("/a");
    StatePath p2("/b");
    obs.watch(p1, [](const StateChangeEvent&) {});
    obs.watch(p2, [](const StateChangeEvent&) {});
    EXPECT_EQ(obs.count(), 2u);

    obs.clear();
    EXPECT_EQ(obs.count(), 0u);
}

TEST_F(ObserverTest, EventCarriesData) {
    StatePath pattern("/data/val");
    const uint8_t* received_data = nullptr;
    size_t received_len = 0;
    obs.watch(pattern, [&](const StateChangeEvent& e) {
        received_data = e.data;
        received_len = e.data_len;
    });

    uint8_t payload[] = {1, 2, 3, 4};
    StatePath path("/data/val");
    StateChangeEvent event{path, path.hash(), CrdtType::LWW_REGISTER, payload, 4, 42};
    obs.notify(event);

    EXPECT_EQ(received_data, payload);
    EXPECT_EQ(received_len, 4u);
}

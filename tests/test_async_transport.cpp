#include <gtest/gtest.h>
#include "protocoll/transport/async_transport.h"
#include "protocoll/transport/loopback_transport.h"
#include <atomic>
#include <thread>
#include <chrono>

using namespace protocoll;

class AsyncTransportTest : public ::testing::Test {
protected:
    std::shared_ptr<LoopbackBus> bus = std::make_shared<LoopbackBus>();
    LoopbackTransport sender_inner{bus};
    LoopbackTransport receiver_inner{bus};
    Endpoint sender_ep{"loopback", 1};
    Endpoint receiver_ep{"loopback", 2};

    void SetUp() override {
        sender_inner.bind(sender_ep);
        receiver_inner.bind(receiver_ep);
    }
};

TEST_F(AsyncTransportTest, StartStop) {
    AsyncTransport async(sender_inner, 2);
    async.start();
    EXPECT_TRUE(async.is_running());
    async.stop();
    EXPECT_FALSE(async.is_running());
}

TEST_F(AsyncTransportTest, AsyncSendReceive) {
    AsyncTransport async_sender(sender_inner, 2);
    AsyncTransport async_receiver(receiver_inner, 2);

    std::atomic<int> recv_count{0};
    std::vector<uint8_t> last_received;
    std::mutex recv_mutex;

    async_receiver.set_recv_handler([&](const uint8_t* data, size_t len,
                                         const Endpoint&, int err) {
        if (err == 0 && len > 0) {
            std::lock_guard<std::mutex> lock(recv_mutex);
            last_received.assign(data, data + len);
            recv_count.fetch_add(1);
        }
    });

    async_receiver.start();
    async_sender.start();

    // Send data
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::atomic<bool> send_done{false};
    async_sender.async_send(payload, sizeof(payload), receiver_ep,
        [&](int bytes, int err) {
            EXPECT_GT(bytes, 0);
            EXPECT_EQ(err, 0);
            send_done.store(true);
        });

    // Wait for send + receive
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (recv_count.load() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(send_done.load());
    EXPECT_EQ(recv_count.load(), 1);
    {
        std::lock_guard<std::mutex> lock(recv_mutex);
        ASSERT_EQ(last_received.size(), 4u);
        EXPECT_EQ(last_received[0], 0xDE);
        EXPECT_EQ(last_received[3], 0xEF);
    }

    async_sender.stop();
    async_receiver.stop();
}

TEST_F(AsyncTransportTest, MultiplePackets) {
    AsyncTransport async_sender(sender_inner, 2);
    AsyncTransport async_receiver(receiver_inner, 2);

    std::atomic<int> recv_count{0};
    async_receiver.set_recv_handler([&](const uint8_t*, size_t len,
                                         const Endpoint&, int err) {
        if (err == 0 && len > 0) recv_count.fetch_add(1);
    });

    async_receiver.start();
    async_sender.start();

    constexpr int NUM_PACKETS = 20;
    for (int i = 0; i < NUM_PACKETS; i++) {
        uint8_t data = static_cast<uint8_t>(i);
        async_sender.async_send(&data, 1, receiver_ep);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (recv_count.load() < NUM_PACKETS &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(recv_count.load(), NUM_PACKETS);

    async_sender.stop();
    async_receiver.stop();
}

TEST_F(AsyncTransportTest, SynchronousSend) {
    AsyncTransport async_sender(sender_inner, 1);

    uint8_t data[] = {1, 2, 3};
    int sent = async_sender.send_to(data, sizeof(data), receiver_ep);
    EXPECT_EQ(sent, 3);
    EXPECT_EQ(async_sender.packets_sent(), 1u);
    EXPECT_EQ(async_sender.bytes_sent(), 3u);
}

TEST_F(AsyncTransportTest, Statistics) {
    AsyncTransport async_sender(sender_inner, 2);
    AsyncTransport async_receiver(receiver_inner, 2);

    std::atomic<int> recv_count{0};
    async_receiver.set_recv_handler([&](const uint8_t*, size_t, const Endpoint&, int) {
        recv_count.fetch_add(1);
    });

    async_receiver.start();
    async_sender.start();

    uint8_t data[] = {0xAA, 0xBB};
    for (int i = 0; i < 5; i++) {
        async_sender.async_send(data, sizeof(data), receiver_ep);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (recv_count.load() < 5 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(async_sender.packets_sent(), 5u);
    EXPECT_EQ(async_sender.bytes_sent(), 10u);
    EXPECT_EQ(async_receiver.packets_received(), 5u);
    EXPECT_EQ(async_receiver.bytes_received(), 10u);

    async_sender.stop();
    async_receiver.stop();
}

TEST_F(AsyncTransportTest, StopDrainsQueue) {
    AsyncTransport async(sender_inner, 2);
    // Don't start — just queue sends and stop

    std::atomic<int> callback_count{0};
    for (int i = 0; i < 5; i++) {
        uint8_t d = static_cast<uint8_t>(i);
        async.async_send(&d, 1, receiver_ep, [&](int, int) {
            callback_count.fetch_add(1);
        });
    }

    // Start then immediately stop — should drain or complete all queued sends
    async.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    async.stop();

    // Callbacks should have fired (either success or error)
    EXPECT_EQ(callback_count.load(), 5);
}

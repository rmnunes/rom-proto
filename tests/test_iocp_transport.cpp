#include <gtest/gtest.h>

#ifdef _WIN32

#include "protocoll/transport/iocp_transport.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace protocoll;

TEST(IocpTransport, CreateDestroy) {
    IocpTransport iocp(2);
    EXPECT_FALSE(iocp.is_running());
}

TEST(IocpTransport, BindToLocalhost) {
    IocpTransport iocp;
    Endpoint ep{"127.0.0.1", 0}; // port 0 = OS assigns
    // Note: port 0 may not work with explicit bind, use a high port
    Endpoint ep2{"127.0.0.1", 19876};
    EXPECT_TRUE(iocp.bind(ep2));
    iocp.close();
}

TEST(IocpTransport, StartStop) {
    IocpTransport iocp(2);
    Endpoint ep{"127.0.0.1", 19877};
    ASSERT_TRUE(iocp.bind(ep));
    iocp.start();
    EXPECT_TRUE(iocp.is_running());
    iocp.stop();
    EXPECT_FALSE(iocp.is_running());
}

TEST(IocpTransport, StopWithoutStart) {
    IocpTransport iocp;
    iocp.stop(); // Should not crash
}

TEST(IocpTransport, DoubleStart) {
    IocpTransport iocp;
    Endpoint ep{"127.0.0.1", 19878};
    ASSERT_TRUE(iocp.bind(ep));
    iocp.start();
    iocp.start(); // Second start should be no-op
    EXPECT_TRUE(iocp.is_running());
    iocp.stop();
}

TEST(IocpTransport, SendAndReceiveLoopback) {
    // Two IOCP transports talking to each other on localhost
    IocpTransport sender(1);
    IocpTransport receiver(1);

    Endpoint sender_ep{"127.0.0.1", 19879};
    Endpoint receiver_ep{"127.0.0.1", 19880};

    ASSERT_TRUE(sender.bind(sender_ep));
    ASSERT_TRUE(receiver.bind(receiver_ep));

    std::atomic<bool> received{false};
    std::vector<uint8_t> received_data;

    receiver.set_recv_handler([&](const uint8_t* data, size_t len,
                                    const Endpoint& from, int error) {
        if (error == 0 && len > 0) {
            received_data.assign(data, data + len);
            received.store(true);
        }
    });

    receiver.start();
    sender.start();

    // Send a packet
    uint8_t msg[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    sender.send_to(msg, sizeof(msg), receiver_ep);

    // Wait for receive (with timeout)
    for (int i = 0; i < 50 && !received.load(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_TRUE(received.load());
    if (received.load()) {
        ASSERT_EQ(received_data.size(), 5u);
        EXPECT_EQ(received_data[0], 0xDE);
        EXPECT_EQ(received_data[4], 0x42);
    }

    EXPECT_GE(sender.packets_sent(), 1u);
    EXPECT_GE(receiver.packets_received(), 1u);

    sender.stop();
    receiver.stop();
}

TEST(IocpTransport, AsyncSendWithCallback) {
    IocpTransport sender(1);
    IocpTransport receiver(1);

    Endpoint sender_ep{"127.0.0.1", 19881};
    Endpoint receiver_ep{"127.0.0.1", 19882};

    ASSERT_TRUE(sender.bind(sender_ep));
    ASSERT_TRUE(receiver.bind(receiver_ep));

    std::atomic<bool> send_done{false};
    std::atomic<int> send_result{0};

    receiver.set_recv_handler([](const uint8_t*, size_t, const Endpoint&, int) {});
    receiver.start();
    sender.start();

    uint8_t msg[] = {1, 2, 3};
    sender.async_send(msg, sizeof(msg), receiver_ep,
        [&](int bytes_sent, int error) {
            send_result.store(bytes_sent);
            send_done.store(true);
        });

    for (int i = 0; i < 50 && !send_done.load(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_TRUE(send_done.load());
    EXPECT_EQ(send_result.load(), 3);

    sender.stop();
    receiver.stop();
}

TEST(IocpTransport, StatsTracking) {
    IocpTransport t(1);
    EXPECT_EQ(t.packets_sent(), 0u);
    EXPECT_EQ(t.packets_received(), 0u);
    EXPECT_EQ(t.bytes_sent(), 0u);
    EXPECT_EQ(t.bytes_received(), 0u);
}

TEST(IocpTransport, MultiplePackets) {
    IocpTransport sender(1);
    IocpTransport receiver(1);

    Endpoint sender_ep{"127.0.0.1", 19883};
    Endpoint receiver_ep{"127.0.0.1", 19884};

    ASSERT_TRUE(sender.bind(sender_ep));
    ASSERT_TRUE(receiver.bind(receiver_ep));

    std::atomic<int> recv_count{0};
    receiver.set_recv_handler([&](const uint8_t*, size_t len,
                                    const Endpoint&, int error) {
        if (error == 0 && len > 0) {
            recv_count.fetch_add(1);
        }
    });

    receiver.start();
    sender.start();

    // Send 10 packets
    for (int i = 0; i < 10; i++) {
        uint8_t msg = static_cast<uint8_t>(i);
        sender.send_to(&msg, 1, receiver_ep);
    }

    // Wait for all
    for (int i = 0; i < 100 && recv_count.load() < 10; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_GE(recv_count.load(), 8); // UDP may drop, but expect most

    sender.stop();
    receiver.stop();
}

#else // !_WIN32

// Stub test on non-Windows
TEST(IocpTransport, NotAvailableOnThisPlatform) {
    GTEST_SKIP() << "IOCP is Windows-only";
}

#endif // _WIN32

#pragma once

// Congestion Control: delay-based, GCC-inspired.
//
// Uses one-way delay gradient to detect congestion before packet loss.
// The controller adjusts a sending rate (bytes/sec) that the Peer uses
// to pace outgoing packets.
//
// Key properties:
//   - Reacts to delay increases, not just loss
//   - Smooth rate adaptation (no sawtooth)
//   - Multiplicative decrease, additive increase
//   - Minimum rate floor to keep the connection alive
//
// Based on: "A Google Congestion Control Algorithm for Real-Time
// Communication" (draft-ietf-rmcat-gcc)

#include <cstdint>
#include <cstddef>
#include <deque>

namespace protocoll {

// Congestion state
enum class CongestionState : uint8_t {
    SLOW_START,    // Exponential growth until first delay signal
    STEADY,        // Additive increase, monitoring delay gradient
    DRAIN,         // Multiplicative decrease after congestion detected
};

// One-way delay sample from a received ACK
struct DelaySample {
    uint32_t packet_number;
    uint32_t send_timestamp_us;   // When we sent it
    uint32_t recv_timestamp_us;   // When peer received it (from ACK delay)
    uint16_t packet_size;
};

// Delay gradient filter: Trendline estimator
// Computes a linear regression over recent delay samples to detect
// whether one-way delay is increasing (congestion) or stable.
class TrendlineEstimator {
public:
    // Add a delay sample. Returns the current trend slope.
    double update(double arrival_delta_ms, double send_delta_ms,
                  double now_ms);

    // Current slope: >0 means delay is increasing (congestion signal)
    double slope() const { return slope_; }

    void reset();

private:
    struct Point {
        double smoothed_delay;
        double arrival_time;
    };

    static constexpr size_t WINDOW_SIZE = 20;
    static constexpr double SMOOTHING_COEFF = 0.9;

    std::deque<Point> window_;
    double accumulated_delay_ = 0.0;
    double smoothed_delay_ = 0.0;
    double slope_ = 0.0;
    size_t count_ = 0;

    double compute_slope() const;
};

// Overuse detector: classifies trendline output into signal
enum class BandwidthUsage : uint8_t {
    NORMAL,
    OVERUSE,
    UNDERUSE,
};

class OveruseDetector {
public:
    BandwidthUsage detect(double trendline_slope);

    BandwidthUsage state() const { return state_; }

private:
    static constexpr double OVERUSE_THRESHOLD = 12.5;  // ms per second
    static constexpr double UNDERUSE_THRESHOLD = -12.5;
    static constexpr int OVERUSE_TIME_THRESHOLD_MS = 10;

    BandwidthUsage state_ = BandwidthUsage::NORMAL;
    double overuse_timer_ = 0.0;
};

// Rate controller: adjusts sending rate based on overuse signal
class CongestionController {
public:
    CongestionController();

    // Called when we send a packet
    void on_packet_sent(uint32_t packet_number, uint32_t timestamp_us,
                        uint16_t size);

    // Called when we get an ACK with timing info.
    // send_ts_us: when we sent the packet
    // ack_ts_us: current time when ACK was processed
    // ack_delay_us: delay reported by the peer in the ACK frame
    void on_ack_received(uint32_t packet_number,
                         uint32_t send_ts_us, uint32_t ack_ts_us,
                         uint16_t ack_delay_us, uint16_t packet_size);

    // Called when a packet is detected as lost
    void on_packet_lost(uint32_t timestamp_us, uint16_t packet_size);

    // Current allowed sending rate in bytes per second
    uint32_t send_rate_bps() const { return send_rate_bps_; }

    // Current congestion window in bytes
    uint32_t cwnd_bytes() const { return cwnd_; }

    // How many bytes are currently in flight
    uint32_t bytes_in_flight() const { return bytes_in_flight_; }

    // Can we send more data right now?
    bool can_send(uint16_t packet_size) const;

    // Pacing: microseconds to wait before sending the next packet
    uint32_t pacing_delay_us() const;

    CongestionState state() const { return state_; }

    // Reset to initial state
    void reset();

private:
    // Rate bounds
    static constexpr uint32_t MIN_RATE_BPS = 10000;        // 10 KB/s floor
    static constexpr uint32_t INITIAL_RATE_BPS = 300000;    // 300 KB/s initial
    static constexpr uint32_t MAX_RATE_BPS = 100000000;     // 100 MB/s ceiling

    // Congestion window bounds
    static constexpr uint32_t MIN_CWND = 2400;    // ~2 packets
    static constexpr uint32_t INITIAL_CWND = 15000; // ~10 packets

    // AIMD parameters
    static constexpr double MULTIPLICATIVE_DECREASE = 0.85;
    static constexpr uint32_t ADDITIVE_INCREASE_BPS = 8000; // 8 KB/s per RTT

    CongestionState state_ = CongestionState::SLOW_START;
    uint32_t send_rate_bps_ = INITIAL_RATE_BPS;
    uint32_t cwnd_ = INITIAL_CWND;
    uint32_t bytes_in_flight_ = 0;
    uint32_t last_update_us_ = 0;

    // Delay-based detection
    TrendlineEstimator trendline_;
    OveruseDetector detector_;

    // Tracking last send/recv timestamps for delta computation
    uint32_t last_send_ts_us_ = 0;
    uint32_t last_arrival_ts_us_ = 0;
    bool has_last_sample_ = false;

    void update_rate(BandwidthUsage usage, uint32_t timestamp_us);
    void update_cwnd_from_rate(uint32_t rtt_us);
};

} // namespace protocoll

#include "protocoll/reliability/congestion.h"
#include <algorithm>
#include <cmath>

namespace protocoll {

// --- TrendlineEstimator ---

double TrendlineEstimator::update(double arrival_delta_ms, double send_delta_ms,
                                   double now_ms) {
    double delay_delta = arrival_delta_ms - send_delta_ms;
    accumulated_delay_ += delay_delta;
    smoothed_delay_ = SMOOTHING_COEFF * smoothed_delay_ +
                      (1.0 - SMOOTHING_COEFF) * accumulated_delay_;

    window_.push_back({smoothed_delay_, now_ms});
    if (window_.size() > WINDOW_SIZE) {
        window_.pop_front();
    }

    count_++;
    if (window_.size() >= 2) {
        slope_ = compute_slope();
    }

    return slope_;
}

double TrendlineEstimator::compute_slope() const {
    if (window_.size() < 2) return 0.0;

    // Linear regression: slope of smoothed_delay vs arrival_time
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    double n = static_cast<double>(window_.size());

    for (const auto& p : window_) {
        sum_x += p.arrival_time;
        sum_y += p.smoothed_delay;
        sum_xy += p.arrival_time * p.smoothed_delay;
        sum_xx += p.arrival_time * p.arrival_time;
    }

    double denom = n * sum_xx - sum_x * sum_x;
    if (std::abs(denom) < 1e-9) return 0.0;

    return (n * sum_xy - sum_x * sum_y) / denom;
}

void TrendlineEstimator::reset() {
    window_.clear();
    accumulated_delay_ = 0.0;
    smoothed_delay_ = 0.0;
    slope_ = 0.0;
    count_ = 0;
}

// --- OveruseDetector ---

BandwidthUsage OveruseDetector::detect(double trendline_slope) {
    if (trendline_slope > OVERUSE_THRESHOLD) {
        overuse_timer_ += 1.0;
        if (overuse_timer_ > OVERUSE_TIME_THRESHOLD_MS) {
            state_ = BandwidthUsage::OVERUSE;
        }
    } else if (trendline_slope < UNDERUSE_THRESHOLD) {
        overuse_timer_ = 0.0;
        state_ = BandwidthUsage::UNDERUSE;
    } else {
        overuse_timer_ = 0.0;
        state_ = BandwidthUsage::NORMAL;
    }
    return state_;
}

// --- CongestionController ---

CongestionController::CongestionController() { reset(); }

void CongestionController::reset() {
    state_ = CongestionState::SLOW_START;
    send_rate_bps_ = INITIAL_RATE_BPS;
    cwnd_ = INITIAL_CWND;
    bytes_in_flight_ = 0;
    last_update_us_ = 0;
    last_send_ts_us_ = 0;
    last_arrival_ts_us_ = 0;
    has_last_sample_ = false;
    trendline_.reset();
}

void CongestionController::on_packet_sent(uint32_t /*packet_number*/,
                                           uint32_t /*timestamp_us*/,
                                           uint16_t size) {
    bytes_in_flight_ += size;
}

void CongestionController::on_ack_received(uint32_t /*packet_number*/,
                                            uint32_t send_ts_us,
                                            uint32_t ack_ts_us,
                                            uint16_t ack_delay_us,
                                            uint16_t packet_size) {
    // Reduce bytes in flight
    if (bytes_in_flight_ >= packet_size) {
        bytes_in_flight_ -= packet_size;
    } else {
        bytes_in_flight_ = 0;
    }

    // Compute one-way delay estimate
    uint32_t arrival_ts_us = ack_ts_us - ack_delay_us;

    if (has_last_sample_) {
        double send_delta_ms = static_cast<double>(send_ts_us - last_send_ts_us_) / 1000.0;
        double arrival_delta_ms = static_cast<double>(arrival_ts_us - last_arrival_ts_us_) / 1000.0;
        double now_ms = static_cast<double>(ack_ts_us) / 1000.0;

        double slope = trendline_.update(arrival_delta_ms, send_delta_ms, now_ms);
        BandwidthUsage usage = detector_.detect(slope);

        update_rate(usage, ack_ts_us);

        // Compute RTT and update cwnd
        uint32_t rtt_us = ack_ts_us - send_ts_us;
        if (rtt_us > 0) {
            update_cwnd_from_rate(rtt_us);
        }
    }

    last_send_ts_us_ = send_ts_us;
    last_arrival_ts_us_ = arrival_ts_us;
    has_last_sample_ = true;
    last_update_us_ = ack_ts_us;
}

void CongestionController::on_packet_lost(uint32_t timestamp_us, uint16_t packet_size) {
    if (bytes_in_flight_ >= packet_size) {
        bytes_in_flight_ -= packet_size;
    } else {
        bytes_in_flight_ = 0;
    }

    // Loss is a strong congestion signal — always decrease
    state_ = CongestionState::DRAIN;
    send_rate_bps_ = static_cast<uint32_t>(
        static_cast<double>(send_rate_bps_) * MULTIPLICATIVE_DECREASE);
    if (send_rate_bps_ < MIN_RATE_BPS) send_rate_bps_ = MIN_RATE_BPS;

    // Update cwnd using current srtt estimate (rough: use 100ms default)
    update_cwnd_from_rate(100000);
    last_update_us_ = timestamp_us;
}

void CongestionController::update_rate(BandwidthUsage usage, uint32_t timestamp_us) {
    uint32_t elapsed_us = timestamp_us - last_update_us_;
    if (elapsed_us == 0) return;

    switch (state_) {
    case CongestionState::SLOW_START:
        if (usage == BandwidthUsage::OVERUSE) {
            // Exit slow start on first overuse signal
            state_ = CongestionState::DRAIN;
            send_rate_bps_ = static_cast<uint32_t>(
                static_cast<double>(send_rate_bps_) * MULTIPLICATIVE_DECREASE);
        } else {
            // Double rate in slow start
            double growth = static_cast<double>(elapsed_us) / 1000000.0;
            send_rate_bps_ = static_cast<uint32_t>(
                static_cast<double>(send_rate_bps_) * (1.0 + growth));
        }
        break;

    case CongestionState::STEADY:
        if (usage == BandwidthUsage::OVERUSE) {
            state_ = CongestionState::DRAIN;
            send_rate_bps_ = static_cast<uint32_t>(
                static_cast<double>(send_rate_bps_) * MULTIPLICATIVE_DECREASE);
        } else if (usage == BandwidthUsage::NORMAL) {
            // Additive increase
            double seconds = static_cast<double>(elapsed_us) / 1000000.0;
            send_rate_bps_ += static_cast<uint32_t>(
                static_cast<double>(ADDITIVE_INCREASE_BPS) * seconds);
        }
        // UNDERUSE: hold rate
        break;

    case CongestionState::DRAIN:
        if (usage != BandwidthUsage::OVERUSE) {
            state_ = CongestionState::STEADY;
        }
        // While in DRAIN, hold the reduced rate
        break;
    }

    // Clamp rate
    if (send_rate_bps_ < MIN_RATE_BPS) send_rate_bps_ = MIN_RATE_BPS;
    if (send_rate_bps_ > MAX_RATE_BPS) send_rate_bps_ = MAX_RATE_BPS;
}

void CongestionController::update_cwnd_from_rate(uint32_t rtt_us) {
    // cwnd = rate * RTT
    double rtt_sec = static_cast<double>(rtt_us) / 1000000.0;
    uint32_t new_cwnd = static_cast<uint32_t>(
        static_cast<double>(send_rate_bps_) * rtt_sec);

    if (new_cwnd < MIN_CWND) new_cwnd = MIN_CWND;
    cwnd_ = new_cwnd;
}

bool CongestionController::can_send(uint16_t packet_size) const {
    return bytes_in_flight_ + packet_size <= cwnd_;
}

uint32_t CongestionController::pacing_delay_us() const {
    if (send_rate_bps_ == 0) return 0;
    // Time to send one MTU-sized packet (1200 bytes)
    // pacing_delay = packet_size / rate
    constexpr uint32_t MTU = 1200;
    return static_cast<uint32_t>(
        (static_cast<double>(MTU) / static_cast<double>(send_rate_bps_)) * 1000000.0);
}

} // namespace protocoll

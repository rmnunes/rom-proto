#include "protocoll/state/subscription.h"
#include "protocoll/util/platform.h"
#include <cstring>

namespace protocoll {

// --- SubscriptionManager ---

uint32_t SubscriptionManager::subscribe(uint16_t conn_id, const StatePath& pattern,
                                         int64_t initial_credits, uint32_t freshness_us) {
    uint32_t id = next_id_++;
    Subscription sub{};
    sub.sub_id = id;
    sub.conn_id = conn_id;
    sub.pattern = pattern;
    sub.credits = initial_credits;
    sub.freshness_us = freshness_us;
    subs_.emplace(id, std::move(sub));
    return id;
}

bool SubscriptionManager::unsubscribe(uint32_t sub_id) {
    return subs_.erase(sub_id) > 0;
}

void SubscriptionManager::remove_connection(uint16_t conn_id) {
    for (auto it = subs_.begin(); it != subs_.end();) {
        if (it->second.conn_id == conn_id) {
            it = subs_.erase(it);
        } else {
            ++it;
        }
    }
}

bool SubscriptionManager::add_credits(uint32_t sub_id, int64_t credits) {
    auto it = subs_.find(sub_id);
    if (it == subs_.end()) return false;
    if (it->second.credits >= 0) {
        it->second.credits += credits;
    }
    return true;
}

std::vector<MatchResult> SubscriptionManager::match_and_consume(const StatePath& path) {
    std::vector<MatchResult> results;
    for (auto& [id, sub] : subs_) {
        if (path.matches(sub.pattern) && sub.has_credits()) {
            results.push_back({sub.sub_id, sub.conn_id});
            sub.consume_credit();
        }
    }
    return results;
}

std::vector<MatchResult> SubscriptionManager::match(const StatePath& path) const {
    std::vector<MatchResult> results;
    for (const auto& [id, sub] : subs_) {
        if (path.matches(sub.pattern) && sub.has_credits()) {
            results.push_back({sub.sub_id, sub.conn_id});
        }
    }
    return results;
}

Subscription* SubscriptionManager::get(uint32_t sub_id) {
    auto it = subs_.find(sub_id);
    return it != subs_.end() ? &it->second : nullptr;
}

const Subscription* SubscriptionManager::get(uint32_t sub_id) const {
    auto it = subs_.find(sub_id);
    return it != subs_.end() ? &it->second : nullptr;
}

size_t SubscriptionManager::count_for_connection(uint16_t conn_id) const {
    size_t n = 0;
    for (const auto& [id, sub] : subs_) {
        if (sub.conn_id == conn_id) n++;
    }
    return n;
}

// --- Wire payloads ---

bool SubscribeFramePayload::encode(uint8_t* buf, size_t buf_len, size_t& written) const {
    size_t needed = wire_size();
    if (buf_len < needed) return false;
    write_u32(buf, sub_id);
    write_u32(buf + 4, static_cast<uint32_t>(initial_credits));
    write_u32(buf + 8, freshness_us);
    buf[12] = static_cast<uint8_t>(tier);
    write_u16(buf + 13, static_cast<uint16_t>(pattern.size()));
    if (!pattern.empty()) {
        std::memcpy(buf + 15, pattern.data(), pattern.size());
    }
    written = needed;
    return true;
}

bool SubscribeFramePayload::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < 15) return false;
    sub_id = read_u32(buf);
    initial_credits = static_cast<int32_t>(read_u32(buf + 4));
    freshness_us = read_u32(buf + 8);
    tier = static_cast<ResolutionTier>(buf[12]);
    uint16_t pat_len = read_u16(buf + 13);
    if (buf_len < 15 + pat_len) return false;
    pattern.assign(reinterpret_cast<const char*>(buf + 15), pat_len);
    return true;
}

bool UnsubscribeFramePayload::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u32(buf, sub_id);
    return true;
}

bool UnsubscribeFramePayload::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    sub_id = read_u32(buf);
    return true;
}

bool CreditFramePayload::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u32(buf, sub_id);
    write_u32(buf + 4, static_cast<uint32_t>(credits));
    return true;
}

bool CreditFramePayload::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    sub_id = read_u32(buf);
    credits = static_cast<int32_t>(read_u32(buf + 4));
    return true;
}

} // namespace protocoll

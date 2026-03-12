#include "protocoll/state/version_vector.h"
#include "protocoll/util/platform.h"
#include <algorithm>

namespace protocoll {

VersionEntry* VersionVector::find(uint16_t node_id) {
    for (auto& e : entries_) {
        if (e.node_id == node_id) return &e;
    }
    return nullptr;
}

const VersionEntry* VersionVector::find(uint16_t node_id) const {
    for (const auto& e : entries_) {
        if (e.node_id == node_id) return &e;
    }
    return nullptr;
}

uint32_t VersionVector::increment(uint16_t node_id) {
    if (auto* e = find(node_id)) {
        return ++e->sequence;
    }
    entries_.push_back({node_id, 1});
    return 1;
}

uint32_t VersionVector::get(uint16_t node_id) const {
    if (const auto* e = find(node_id)) return e->sequence;
    return 0;
}

void VersionVector::set(uint16_t node_id, uint32_t seq) {
    if (auto* e = find(node_id)) {
        e->sequence = seq;
    } else {
        entries_.push_back({node_id, seq});
    }
}

void VersionVector::merge(const VersionVector& other) {
    for (const auto& oe : other.entries_) {
        if (auto* e = find(oe.node_id)) {
            e->sequence = std::max(e->sequence, oe.sequence);
        } else {
            entries_.push_back(oe);
        }
    }
}

bool VersionVector::dominates(const VersionVector& other) const {
    for (const auto& oe : other.entries_) {
        if (get(oe.node_id) < oe.sequence) return false;
    }
    return true;
}

bool VersionVector::concurrent_with(const VersionVector& other) const {
    return !dominates(other) && !other.dominates(*this);
}

bool VersionVector::less_than(const VersionVector& other) const {
    return other.dominates(*this) && !(*this == other);
}

bool VersionVector::operator==(const VersionVector& other) const {
    // Equal if same values for all nodes present in either
    for (const auto& e : entries_) {
        if (other.get(e.node_id) != e.sequence) return false;
    }
    for (const auto& e : other.entries_) {
        if (get(e.node_id) != e.sequence) return false;
    }
    return true;
}

bool VersionVector::encode(uint8_t* buf, size_t buf_len) const {
    size_t needed = wire_size();
    if (buf_len < needed) return false;

    buf[0] = static_cast<uint8_t>(entries_.size());
    size_t offset = 1;
    for (const auto& e : entries_) {
        write_u16(buf + offset, e.node_id);
        write_u32(buf + offset + 2, e.sequence);
        offset += 6;
    }
    return true;
}

bool VersionVector::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < 1) return false;

    uint8_t count = buf[0];
    size_t needed = 1 + static_cast<size_t>(count) * 6;
    if (buf_len < needed) return false;

    entries_.clear();
    entries_.reserve(count);
    size_t offset = 1;
    for (uint8_t i = 0; i < count; i++) {
        uint16_t node_id = read_u16(buf + offset);
        uint32_t seq = read_u32(buf + offset + 2);
        entries_.push_back({node_id, seq});
        offset += 6;
    }
    return true;
}

} // namespace protocoll

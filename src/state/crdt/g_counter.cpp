#include "protocoll/state/crdt/g_counter.h"
#include "protocoll/util/platform.h"
#include <algorithm>
#include <cstring>

namespace protocoll {

GCounter::GCounter(uint16_t node_id) : node_id_(node_id) {
    entries_.push_back({node_id, 0});
}

GCounterEntry* GCounter::find(uint16_t node_id) {
    for (auto& e : entries_) {
        if (e.node_id == node_id) return &e;
    }
    return nullptr;
}

const GCounterEntry* GCounter::find(uint16_t node_id) const {
    for (const auto& e : entries_) {
        if (e.node_id == node_id) return &e;
    }
    return nullptr;
}

void GCounter::increment(uint64_t amount) {
    if (auto* e = find(node_id_)) {
        e->count += amount;
    } else {
        entries_.push_back({node_id_, amount});
    }
    dirty_ = true;
}

uint64_t GCounter::value() const {
    uint64_t sum = 0;
    for (const auto& e : entries_) sum += e.count;
    return sum;
}

uint64_t GCounter::local_count() const {
    if (const auto* e = find(node_id_)) return e->count;
    return 0;
}

bool GCounter::merge(const uint8_t* data, size_t len) {
    if (len < 1) return false;
    uint8_t count = data[0];
    if (len < 1 + static_cast<size_t>(count) * ENTRY_WIRE_SIZE) return false;

    bool changed = false;
    size_t offset = 1;
    for (uint8_t i = 0; i < count; i++) {
        uint16_t nid = read_u16(data + offset);
        // Read uint64 as two uint32s (big-endian)
        uint32_t hi = read_u32(data + offset + 2);
        uint32_t lo = read_u32(data + offset + 6);
        uint64_t cnt = (static_cast<uint64_t>(hi) << 32) | lo;
        offset += ENTRY_WIRE_SIZE;

        if (auto* e = find(nid)) {
            if (cnt > e->count) {
                e->count = cnt;
                changed = true;
            }
        } else {
            entries_.push_back({nid, cnt});
            changed = true;
        }
    }
    return changed;
}

std::vector<uint8_t> GCounter::snapshot() const {
    std::vector<uint8_t> buf(1 + entries_.size() * ENTRY_WIRE_SIZE);
    buf[0] = static_cast<uint8_t>(entries_.size());
    size_t offset = 1;
    for (const auto& e : entries_) {
        write_u16(buf.data() + offset, e.node_id);
        write_u32(buf.data() + offset + 2, static_cast<uint32_t>(e.count >> 32));
        write_u32(buf.data() + offset + 6, static_cast<uint32_t>(e.count & 0xFFFFFFFF));
        offset += ENTRY_WIRE_SIZE;
    }
    return buf;
}

std::vector<uint8_t> GCounter::delta() {
    if (!dirty_) return {};
    dirty_ = false;
    // Delta: just our own node's entry
    const auto* e = find(node_id_);
    if (!e) return {};

    std::vector<uint8_t> buf(1 + ENTRY_WIRE_SIZE);
    buf[0] = 1; // single entry
    write_u16(buf.data() + 1, e->node_id);
    write_u32(buf.data() + 3, static_cast<uint32_t>(e->count >> 32));
    write_u32(buf.data() + 7, static_cast<uint32_t>(e->count & 0xFFFFFFFF));
    return buf;
}

} // namespace protocoll

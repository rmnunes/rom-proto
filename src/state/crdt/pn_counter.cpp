#include "protocoll/state/crdt/pn_counter.h"
#include <cstring>

namespace protocoll {

PnCounter::PnCounter(uint16_t node_id) : p_(node_id), n_(node_id) {}

void PnCounter::increment(uint64_t amount) { p_.increment(amount); }
void PnCounter::decrement(uint64_t amount) { n_.increment(amount); }

int64_t PnCounter::value() const {
    return static_cast<int64_t>(p_.value()) - static_cast<int64_t>(n_.value());
}

bool PnCounter::has_pending_delta() const {
    return p_.has_pending_delta() || n_.has_pending_delta();
}

bool PnCounter::merge(const uint8_t* data, size_t len) {
    // Format: p_snapshot bytes then n_snapshot bytes
    // Each snapshot starts with uint8_t entry_count
    if (len < 2) return false;

    uint8_t p_count = data[0];
    size_t p_size = 1 + static_cast<size_t>(p_count) * GCounter::ENTRY_WIRE_SIZE;
    if (len < p_size + 1) return false;

    bool changed_p = p_.merge(data, p_size);

    const uint8_t* n_data = data + p_size;
    size_t n_len = len - p_size;
    bool changed_n = n_.merge(n_data, n_len);

    return changed_p || changed_n;
}

std::vector<uint8_t> PnCounter::snapshot() const {
    auto p_snap = p_.snapshot();
    auto n_snap = n_.snapshot();
    std::vector<uint8_t> buf;
    buf.reserve(p_snap.size() + n_snap.size());
    buf.insert(buf.end(), p_snap.begin(), p_snap.end());
    buf.insert(buf.end(), n_snap.begin(), n_snap.end());
    return buf;
}

std::vector<uint8_t> PnCounter::delta() {
    // Always emit both halves so merge can parse them
    auto p_part = p_.has_pending_delta() ? p_.delta() : p_.snapshot();
    auto n_part = n_.has_pending_delta() ? n_.delta() : n_.snapshot();

    std::vector<uint8_t> buf;
    buf.reserve(p_part.size() + n_part.size());
    buf.insert(buf.end(), p_part.begin(), p_part.end());
    buf.insert(buf.end(), n_part.begin(), n_part.end());
    return buf;
}

} // namespace protocoll

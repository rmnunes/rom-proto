#include "protocoll/state/crdt/lww_register.h"
#include "protocoll/util/platform.h"
#include <cstring>

namespace protocoll {

LwwRegister::LwwRegister(uint16_t node_id) : node_id_(node_id) {}

void LwwRegister::set(const uint8_t* data, size_t len, uint32_t timestamp_us) {
    value_.assign(data, data + len);
    timestamp_ = timestamp_us;
    dirty_ = true;
}

bool LwwRegister::incoming_wins(uint32_t ts, uint16_t node) const {
    if (ts > timestamp_) return true;
    if (ts == timestamp_ && node > node_id_) return true;
    return false;
}

bool LwwRegister::merge(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE) return false;

    uint32_t ts = read_u32(data);
    uint16_t node = read_u16(data + 4);

    if (incoming_wins(ts, node)) {
        timestamp_ = ts;
        node_id_ = node;
        value_.assign(data + HEADER_SIZE, data + len);
        return true;
    }
    return false;
}

std::vector<uint8_t> LwwRegister::encode_state() const {
    std::vector<uint8_t> buf(HEADER_SIZE + value_.size());
    write_u32(buf.data(), timestamp_);
    write_u16(buf.data() + 4, node_id_);
    if (!value_.empty()) {
        std::memcpy(buf.data() + HEADER_SIZE, value_.data(), value_.size());
    }
    return buf;
}

std::vector<uint8_t> LwwRegister::snapshot() const {
    return encode_state();
}

std::vector<uint8_t> LwwRegister::delta() {
    if (!dirty_) return {};
    dirty_ = false;
    return encode_state();
}

} // namespace protocoll

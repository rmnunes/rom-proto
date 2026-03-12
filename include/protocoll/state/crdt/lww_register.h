#pragma once

/*
 * LWW-Register (Last-Writer-Wins Register)
 *
 * Stores an arbitrary byte value with a timestamp.
 * On merge, the value with the highest timestamp wins.
 * Ties broken by node_id (higher wins).
 *
 * Wire format: [uint32_t timestamp_us][uint16_t node_id][...value bytes]
 */

#include "protocoll/state/crdt/crdt.h"
#include <cstdint>
#include <vector>

namespace protocoll {

class LwwRegister : public Crdt {
public:
    LwwRegister() = default;
    explicit LwwRegister(uint16_t node_id);

    // Set the value (assigns current timestamp)
    void set(const uint8_t* data, size_t len, uint32_t timestamp_us);

    // Get current value
    const std::vector<uint8_t>& value() const { return value_; }
    uint32_t timestamp() const { return timestamp_; }
    uint16_t node_id() const { return node_id_; }
    bool empty() const { return value_.empty() && timestamp_ == 0; }

    // CRDT interface
    bool merge(const uint8_t* data, size_t len) override;
    std::vector<uint8_t> snapshot() const override;
    std::vector<uint8_t> delta() override;
    bool has_pending_delta() const override { return dirty_; }

    static constexpr size_t HEADER_SIZE = 6; // 4 (timestamp) + 2 (node_id)

private:
    std::vector<uint8_t> value_;
    uint32_t timestamp_ = 0;
    uint16_t node_id_ = 0;
    bool dirty_ = false;

    // Returns true if (ts, node) wins over current (timestamp_, node_id_)
    bool incoming_wins(uint32_t ts, uint16_t node) const;

    std::vector<uint8_t> encode_state() const;
};

} // namespace protocoll

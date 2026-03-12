#include "protocoll/connection/recovery.h"
#include "protocoll/util/platform.h"
#include <cstring>

namespace protocoll {

// --- RecoveryRequest wire format ---
// uint16_t path_count
// For each path:
//   uint32_t path_hash
//   [VersionVector wire bytes]

std::vector<uint8_t> RecoveryRequest::encode() const {
    std::vector<uint8_t> buf;

    // Path count
    buf.resize(2);
    write_u16(buf.data(), static_cast<uint16_t>(known_versions.size()));

    for (const auto& [path_hash, vv] : known_versions) {
        size_t pos = buf.size();
        buf.resize(pos + 4 + vv.wire_size());
        write_u32(buf.data() + pos, path_hash);
        vv.encode(buf.data() + pos + 4, vv.wire_size());
    }

    return buf;
}

bool RecoveryRequest::decode(const uint8_t* data, size_t len, RecoveryRequest& out) {
    if (len < 2) return false;
    uint16_t count = read_u16(data);
    size_t off = 2;

    out.known_versions.clear();
    for (uint16_t i = 0; i < count; i++) {
        if (off + 4 > len) return false;
        uint32_t path_hash = read_u32(data + off);
        off += 4;

        VersionVector vv;
        if (off >= len) return false;
        // Peek at entry count to determine VV size
        uint8_t entry_count = data[off];
        size_t vv_size = 1 + static_cast<size_t>(entry_count) * 6;
        if (off + vv_size > len) return false;

        if (!vv.decode(data + off, vv_size)) return false;
        off += vv_size;

        out.known_versions[path_hash] = std::move(vv);
    }
    return true;
}

// --- RecoveryManager ---

RecoveryPlan RecoveryManager::compute_plan(
    const RecoveryRequest& request,
    const EventLog& log,
    const StateRegistry& registry,
    size_t max_delta_entries)
{
    RecoveryPlan plan;

    // For each state region the server knows about
    const_cast<StateRegistry&>(registry).for_each([&](const StateRegion& region) {
        RecoveryPlan::PathRecovery pr;
        pr.path_hash = region.path.hash();

        // Find client's known version for this path
        auto it = request.known_versions.find(pr.path_hash);
        if (it != request.known_versions.end()) {
            // Client has some version — send deltas since then
            auto entries = log.entries_since(pr.path_hash, it->second);
            if (entries.size() <= max_delta_entries) {
                pr.needs_snapshot = false;
                pr.entries = std::move(entries);
            } else {
                pr.needs_snapshot = true; // Too many deltas, send snapshot
            }
        } else {
            // Client has no version — send snapshot
            pr.needs_snapshot = true;
        }

        plan.paths.push_back(std::move(pr));
    });

    return plan;
}

RecoveryRequest RecoveryManager::build_request(const StateRegistry& registry) {
    RecoveryRequest req;
    const_cast<StateRegistry&>(registry).for_each([&](const StateRegion& region) {
        req.known_versions[region.path.hash()] = region.version;
    });
    return req;
}

} // namespace protocoll

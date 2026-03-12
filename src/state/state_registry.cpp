#include "protocoll/state/state_registry.h"
#include "protocoll/state/crdt/pn_counter.h"
#include "protocoll/state/crdt/or_set.h"

namespace protocoll {

StateRegistry::StateRegistry(uint16_t node_id) : node_id_(node_id) {}

std::unique_ptr<Crdt> StateRegistry::create_crdt(CrdtType type) {
    switch (type) {
        case CrdtType::LWW_REGISTER:
            return std::make_unique<LwwRegister>(node_id_);
        case CrdtType::G_COUNTER:
            return std::make_unique<GCounter>(node_id_);
        case CrdtType::PN_COUNTER:
            return std::make_unique<PnCounter>(node_id_);
        case CrdtType::OR_SET:
            return std::make_unique<OrSet>(node_id_);
        default:
            return nullptr;
    }
}

bool StateRegistry::declare(const StatePath& path, CrdtType type, Reliability rel) {
    if (regions_.count(path.hash())) return false; // Already declared

    auto crdt = create_crdt(type);
    if (!crdt) return false;

    StateRegion region;
    region.path = path;
    region.crdt_type = type;
    region.reliability = rel;
    region.crdt = std::move(crdt);
    regions_.emplace(path.hash(), std::move(region));
    return true;
}

StateRegion* StateRegistry::get(uint32_t path_hash) {
    auto it = regions_.find(path_hash);
    return it != regions_.end() ? &it->second : nullptr;
}

const StateRegion* StateRegistry::get(uint32_t path_hash) const {
    auto it = regions_.find(path_hash);
    return it != regions_.end() ? &it->second : nullptr;
}

bool StateRegistry::apply_delta(uint32_t path_hash, const uint8_t* data, size_t len) {
    auto* region = get(path_hash);
    if (!region) return false;

    bool changed = region->crdt->merge(data, len);
    if (changed && on_update_) {
        auto snap = region->crdt->snapshot();
        on_update_(region->path, snap.data(), snap.size());
    }
    return changed;
}

bool StateRegistry::apply_snapshot(uint32_t path_hash, const uint8_t* data, size_t len) {
    // For snapshots, merge is the same operation (CRDT merge is idempotent)
    return apply_delta(path_hash, data, len);
}

std::vector<StateRegistry::PendingDelta> StateRegistry::collect_deltas() {
    std::vector<PendingDelta> deltas;
    for (auto& [hash, region] : regions_) {
        if (region.crdt->has_pending_delta()) {
            auto d = region.crdt->delta();
            if (!d.empty()) {
                deltas.push_back({hash, region.crdt_type, region.reliability, std::move(d)});
            }
        }
    }
    return deltas;
}

} // namespace protocoll

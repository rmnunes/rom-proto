#include "protocoll/state/event_log.h"

namespace protocoll {

uint64_t EventLog::append(LogEntry entry) {
    entry.sequence = next_seq_++;
    uint64_t seq = entry.sequence;
    entries_.push_back(std::move(entry));
    return seq;
}

uint64_t EventLog::append_delta(uint32_t path_hash, CrdtType crdt_type,
                                 const VersionVector& version,
                                 const uint8_t* data, size_t len,
                                 uint32_t timestamp_us) {
    LogEntry entry{};
    entry.path_hash = path_hash;
    entry.crdt_type = crdt_type;
    entry.version = version;
    entry.data.assign(data, data + len);
    entry.is_snapshot = false;
    entry.timestamp_us = timestamp_us;
    return append(std::move(entry));
}

uint64_t EventLog::append_snapshot(uint32_t path_hash, CrdtType crdt_type,
                                    const VersionVector& version,
                                    const uint8_t* data, size_t len,
                                    uint32_t timestamp_us) {
    LogEntry entry{};
    entry.path_hash = path_hash;
    entry.crdt_type = crdt_type;
    entry.version = version;
    entry.data.assign(data, data + len);
    entry.is_snapshot = true;
    entry.timestamp_us = timestamp_us;
    return append(std::move(entry));
}

std::vector<const LogEntry*> EventLog::entries_since(uint32_t path_hash,
                                                      const VersionVector& since) const {
    std::vector<const LogEntry*> result;
    for (const auto& entry : entries_) {
        if (entry.path_hash != path_hash) continue;
        // Include if entry's version is NOT dominated by `since`
        // (i.e., it contains new information)
        if (!since.dominates(entry.version)) {
            result.push_back(&entry);
        }
    }
    return result;
}

const LogEntry* EventLog::latest_snapshot(uint32_t path_hash) const {
    const LogEntry* latest = nullptr;
    for (const auto& entry : entries_) {
        if (entry.path_hash == path_hash && entry.is_snapshot) {
            latest = &entry;
        }
    }
    return latest;
}

std::vector<const LogEntry*> EventLog::entries_for(uint32_t path_hash) const {
    std::vector<const LogEntry*> result;
    for (const auto& entry : entries_) {
        if (entry.path_hash == path_hash) {
            result.push_back(&entry);
        }
    }
    return result;
}

size_t EventLog::compact(uint32_t path_hash, const VersionVector& snapshot_version) {
    size_t before = entries_.size();
    std::deque<LogEntry> kept;
    for (auto& entry : entries_) {
        if (entry.path_hash == path_hash) {
            // Keep if: it's newer than snapshot, or it IS the snapshot
            if (entry.is_snapshot || !snapshot_version.dominates(entry.version)) {
                kept.push_back(std::move(entry));
            }
        } else {
            kept.push_back(std::move(entry));
        }
    }
    entries_ = std::move(kept);
    return before - entries_.size();
}

} // namespace protocoll

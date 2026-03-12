#pragma once

// EventLog: append-only log of state changes for persistence and recovery.
//
// Each entry records: path_hash, version_vector, crdt_type, delta bytes.
// The log supports:
//   - Append new entries
//   - Query entries since a given version vector (for recovery)
//   - Snapshot compaction (replace old entries with a snapshot)
//
// Phase 1: in-memory log. Disk persistence is Phase 4+.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <deque>
#include <mutex>

#include "protocoll/wire/frame_types.h"
#include "protocoll/state/version_vector.h"

namespace protocoll {

struct LogEntry {
    uint64_t    sequence;       // Monotonic log sequence number
    uint32_t    path_hash;
    CrdtType    crdt_type;
    VersionVector version;      // Version at time of this entry
    std::vector<uint8_t> data;  // Delta or snapshot bytes
    bool        is_snapshot;    // true = snapshot, false = delta
    uint32_t    timestamp_us;   // When this entry was created
};

class EventLog {
public:
    // Append a delta entry. Returns the assigned sequence number.
    uint64_t append_delta(uint32_t path_hash, CrdtType crdt_type,
                          const VersionVector& version,
                          const uint8_t* data, size_t len,
                          uint32_t timestamp_us);

    // Append a snapshot entry (for compaction or initial sync).
    uint64_t append_snapshot(uint32_t path_hash, CrdtType crdt_type,
                             const VersionVector& version,
                             const uint8_t* data, size_t len,
                             uint32_t timestamp_us);

    // Get all entries for a path since a given version vector.
    // Returns entries where entry.version is NOT dominated by `since`.
    std::vector<const LogEntry*> entries_since(uint32_t path_hash,
                                                const VersionVector& since) const;

    // Get the latest snapshot for a path (or nullptr if none).
    const LogEntry* latest_snapshot(uint32_t path_hash) const;

    // Get all entries for a path (for debugging/inspection).
    std::vector<const LogEntry*> entries_for(uint32_t path_hash) const;

    // Compact: remove all entries for a path older than the given snapshot.
    // The snapshot replaces them.
    size_t compact(uint32_t path_hash, const VersionVector& snapshot_version);

    // Total entries in the log
    size_t size() const { return entries_.size(); }

    // Latest sequence number
    uint64_t latest_sequence() const { return next_seq_ - 1; }

    // Maximum entries before auto-compaction hint
    void set_max_entries(size_t max) { max_entries_ = max; }
    bool needs_compaction() const { return entries_.size() > max_entries_; }

private:
    uint64_t next_seq_ = 1;
    std::deque<LogEntry> entries_;
    size_t max_entries_ = 10000;

    uint64_t append(LogEntry entry);
};

} // namespace protocoll

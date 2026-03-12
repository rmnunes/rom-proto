#include "protocoll/state/crdt/or_set.h"
#include "protocoll/util/platform.h"
#include <algorithm>
#include <cstring>

namespace protocoll {

OrSet::OrSet(uint16_t node_id) : node_id_(node_id) {}

bool OrSet::add(const uint8_t* elem, size_t len) {
    ElemKey key(elem, elem + len);
    auto tag = next_tag();
    entries_[key].insert(tag);
    pending_adds_[key].insert(tag);
    dirty_ = true;
    return true;
}

bool OrSet::remove(const uint8_t* elem, size_t len) {
    ElemKey key(elem, elem + len);
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.empty()) return false;

    // Observed-remove: remove all currently visible tags
    for (const auto& tag : it->second) {
        pending_removes_.insert(tag);
    }
    it->second.clear();
    entries_.erase(it);
    dirty_ = true;
    return true;
}

bool OrSet::contains(const uint8_t* elem, size_t len) const {
    ElemKey key(elem, elem + len);
    auto it = entries_.find(key);
    return it != entries_.end() && !it->second.empty();
}

std::vector<std::vector<uint8_t>> OrSet::elements() const {
    std::vector<std::vector<uint8_t>> result;
    for (const auto& [key, tags] : entries_) {
        if (!tags.empty()) result.push_back(key);
    }
    return result;
}

size_t OrSet::size() const {
    size_t count = 0;
    for (const auto& [key, tags] : entries_) {
        if (!tags.empty()) count++;
    }
    return count;
}

// --- Wire format ---

void OrSet::encode_entries(const std::map<ElemKey, std::set<OrSetTag>>& entries,
                           const std::set<OrSetTag>& tombstones,
                           std::vector<uint8_t>& buf) {
    // Entry count
    uint16_t entry_count = 0;
    for (const auto& [key, tags] : entries) {
        if (!tags.empty()) entry_count++;
    }
    size_t pos = buf.size();
    buf.resize(pos + 2);
    write_u16(buf.data() + pos, entry_count);

    for (const auto& [key, tags] : entries) {
        if (tags.empty()) continue;
        // Element length + bytes
        size_t epos = buf.size();
        buf.resize(epos + 2 + key.size());
        write_u16(buf.data() + epos, static_cast<uint16_t>(key.size()));
        if (!key.empty()) std::memcpy(buf.data() + epos + 2, key.data(), key.size());

        // Tag count + tags
        size_t tpos = buf.size();
        buf.resize(tpos + 1 + tags.size() * 6);
        buf[tpos] = static_cast<uint8_t>(tags.size());
        size_t off = tpos + 1;
        for (const auto& tag : tags) {
            write_u16(buf.data() + off, tag.node_id);
            write_u32(buf.data() + off + 2, tag.sequence);
            off += 6;
        }
    }

    // Tombstone count + tombstones
    size_t rpos = buf.size();
    buf.resize(rpos + 2 + tombstones.size() * 6);
    write_u16(buf.data() + rpos, static_cast<uint16_t>(tombstones.size()));
    size_t off = rpos + 2;
    for (const auto& tag : tombstones) {
        write_u16(buf.data() + off, tag.node_id);
        write_u32(buf.data() + off + 2, tag.sequence);
        off += 6;
    }
}

bool OrSet::decode_entries(const uint8_t* data, size_t len, Decoded& out) {
    if (len < 2) return false;
    size_t off = 0;

    uint16_t entry_count = read_u16(data + off); off += 2;

    for (uint16_t i = 0; i < entry_count; i++) {
        if (off + 2 > len) return false;
        uint16_t elem_len = read_u16(data + off); off += 2;
        if (off + elem_len > len) return false;
        ElemKey key(data + off, data + off + elem_len); off += elem_len;

        if (off + 1 > len) return false;
        uint8_t tag_count = data[off]; off += 1;
        if (off + tag_count * 6 > len) return false;

        std::set<OrSetTag> tags;
        for (uint8_t t = 0; t < tag_count; t++) {
            uint16_t nid = read_u16(data + off);
            uint32_t seq = read_u32(data + off + 2);
            tags.insert({nid, seq});
            off += 6;
        }
        out.entries[key].insert(tags.begin(), tags.end());
    }

    // Tombstones
    if (off + 2 > len) return false;
    uint16_t tomb_count = read_u16(data + off); off += 2;
    if (off + tomb_count * 6 > len) return false;

    for (uint16_t i = 0; i < tomb_count; i++) {
        uint16_t nid = read_u16(data + off);
        uint32_t seq = read_u32(data + off + 2);
        out.tombstones.insert({nid, seq});
        off += 6;
    }

    return true;
}

bool OrSet::merge(const uint8_t* data, size_t len) {
    Decoded incoming;
    if (!decode_entries(data, len, incoming)) return false;

    bool changed = false;

    // Apply tombstones: remove matching tags from local entries
    for (auto& [key, tags] : entries_) {
        size_t before = tags.size();
        for (const auto& tomb : incoming.tombstones) {
            tags.erase(tomb);
        }
        if (tags.size() != before) changed = true;
    }

    // Add incoming entries (union of tags per element)
    // Skip tags that appear in the incoming tombstones — they were removed
    for (const auto& [key, tags] : incoming.entries) {
        auto& local_tags = entries_[key];
        for (const auto& tag : tags) {
            if (incoming.tombstones.count(tag)) continue;
            if (local_tags.insert(tag).second) {
                changed = true;
            }
        }
    }

    // Clean up empty entries
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.empty()) {
            it = entries_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    // Track max sequence per node to avoid tag reuse
    for (const auto& [key, tags] : incoming.entries) {
        for (const auto& tag : tags) {
            if (tag.node_id == node_id_ && tag.sequence > seq_) {
                seq_ = tag.sequence;
            }
        }
    }

    return changed;
}

std::vector<uint8_t> OrSet::snapshot() const {
    std::vector<uint8_t> buf;
    std::set<OrSetTag> empty_tombs;
    encode_entries(entries_, empty_tombs, buf);
    return buf;
}

std::vector<uint8_t> OrSet::delta() {
    if (!dirty_) return {};
    dirty_ = false;

    std::vector<uint8_t> buf;
    encode_entries(pending_adds_, pending_removes_, buf);

    pending_adds_.clear();
    pending_removes_.clear();
    return buf;
}

} // namespace protocoll

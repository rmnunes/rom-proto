#pragma once

// StatePath: hierarchical state addressing.
// Format: /domain/entity-type/entity-id/property
// Example: /app/users/alice/profile
//
// Paths are hashed (xxHash32) for wire-level efficiency.
// Wildcard subscriptions: /app/users/*/status

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace protocoll {

class StatePath {
public:
    StatePath() = default;
    explicit StatePath(std::string_view path);

    // Full path string
    const std::string& str() const { return path_; }

    // xxHash32 of the path (used in wire frames)
    uint32_t hash() const { return hash_; }

    // Path segments (split by '/')
    const std::vector<std::string_view>& segments() const { return segments_; }

    // Check if this path matches a pattern (supports '*' wildcard per segment)
    bool matches(const StatePath& pattern) const;

    // Check if this path is a prefix of another
    bool is_prefix_of(const StatePath& other) const;

    bool empty() const { return path_.empty(); }

    // Copy must re-parse to fix up string_view pointers
    StatePath(const StatePath& other) : path_(other.path_), hash_(other.hash_) { parse_segments(); }
    StatePath& operator=(const StatePath& other) {
        if (this != &other) { path_ = other.path_; hash_ = other.hash_; parse_segments(); }
        return *this;
    }
    // Move must also re-parse: SSO strings store data inline, so moving
    // a short string changes the buffer address and invalidates views.
    StatePath(StatePath&& other) noexcept : path_(std::move(other.path_)), hash_(other.hash_) { parse_segments(); }
    StatePath& operator=(StatePath&& other) noexcept {
        if (this != &other) { path_ = std::move(other.path_); hash_ = other.hash_; parse_segments(); }
        return *this;
    }

    bool operator==(const StatePath& o) const { return hash_ == o.hash_ && path_ == o.path_; }
    bool operator!=(const StatePath& o) const { return !(*this == o); }

private:
    std::string path_;
    uint32_t hash_ = 0;
    std::vector<std::string_view> segments_;

    void parse();
    void parse_segments();
};

} // namespace protocoll

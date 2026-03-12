#include "protocoll/state/state_path.h"
#include "protocoll/util/hash.h"

namespace protocoll {

StatePath::StatePath(std::string_view path) : path_(path) {
    parse();
}

void StatePath::parse() {
    hash_ = xxhash32(path_);
    parse_segments();
}

void StatePath::parse_segments() {
    segments_.clear();

    std::string_view sv = path_;
    // Skip leading '/'
    if (!sv.empty() && sv[0] == '/') sv.remove_prefix(1);
    // Skip trailing '/'
    if (!sv.empty() && sv.back() == '/') sv.remove_suffix(1);

    while (!sv.empty()) {
        auto pos = sv.find('/');
        if (pos == std::string_view::npos) {
            segments_.push_back(sv);
            break;
        }
        segments_.push_back(sv.substr(0, pos));
        sv.remove_prefix(pos + 1);
    }
}

bool StatePath::matches(const StatePath& pattern) const {
    const auto& ps = pattern.segments();
    if (ps.size() != segments_.size()) return false;

    for (size_t i = 0; i < ps.size(); i++) {
        if (ps[i] == "*") continue;
        if (ps[i] != segments_[i]) return false;
    }
    return true;
}

bool StatePath::is_prefix_of(const StatePath& other) const {
    if (segments_.size() > other.segments_.size()) return false;
    for (size_t i = 0; i < segments_.size(); i++) {
        if (segments_[i] != other.segments_[i]) return false;
    }
    return true;
}

} // namespace protocoll

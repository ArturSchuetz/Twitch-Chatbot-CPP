#include "twitchbot/rate_limiter.hpp"

#include <algorithm>
#include <string>

namespace twitchbot {

void RateLimiter::prune(std::deque<Clock::time_point>& entries, const Clock::time_point cutoff) {
    while (!entries.empty() && entries.front() <= cutoff) {
        entries.pop_front();
    }
}

RateLimiter::Clock::time_point RateLimiter::next_allowed(const OutboundKind kind,
                                                         const std::string_view channel,
                                                         const Clock::time_point now) {
    if (kind == OutboundKind::system) {
        return now;
    }
    if (kind == OutboundKind::join) {
        prune(joins_, now - std::chrono::seconds(10));
        return joins_.size() < 20U ? now : joins_.front() + std::chrono::seconds(10);
    }

    prune(global_chat_, now - std::chrono::seconds(30));
    auto allowed =
        global_chat_.size() < 20U ? now : global_chat_.front() + std::chrono::seconds(30);
    if (const auto iterator = channel_chat_.find(std::string(channel));
        iterator != channel_chat_.end()) {
        allowed = std::max(allowed, iterator->second + std::chrono::seconds(1));
    }
    return allowed;
}

void RateLimiter::record(const OutboundKind kind, const std::string_view channel,
                         const Clock::time_point sent_at) {
    if (kind == OutboundKind::join) {
        joins_.push_back(sent_at);
    } else if (kind == OutboundKind::chat) {
        global_chat_.push_back(sent_at);
        channel_chat_.insert_or_assign(std::string(channel), sent_at);
    }
}

void RateLimiter::reset() {
    global_chat_.clear();
    joins_.clear();
    channel_chat_.clear();
}

} // namespace twitchbot

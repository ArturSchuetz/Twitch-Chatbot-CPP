#pragma once

#include "twitchbot/outbound.hpp"

#include <chrono>
#include <deque>
#include <string_view>
#include <unordered_map>

namespace twitchbot {

class RateLimiter {
  public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] Clock::time_point next_allowed(OutboundKind kind, std::string_view channel,
                                                 Clock::time_point now);
    void record(OutboundKind kind, std::string_view channel, Clock::time_point sent_at);
    void reset();

  private:
    static void prune(std::deque<Clock::time_point>& entries, Clock::time_point cutoff);

    std::deque<Clock::time_point> global_chat_;
    std::deque<Clock::time_point> joins_;
    std::unordered_map<std::string, Clock::time_point> channel_chat_;
};

} // namespace twitchbot

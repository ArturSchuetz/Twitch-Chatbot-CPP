#pragma once

#include "twitchbot/config.hpp"
#include "twitchbot/irc.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace twitchbot {

struct ScheduledChatMessage {
    std::string channel;
    std::string text;
    std::chrono::steady_clock::time_point due;
};

class AutomationEngine {
  public:
    explicit AutomationEngine(BotSettings settings);

    [[nodiscard]] std::vector<ScheduledChatMessage>
    handle(const IrcMessage& message, std::chrono::steady_clock::time_point now);
    void reset_session() noexcept;

  private:
    [[nodiscard]] std::vector<ScheduledChatMessage>
    handle_privmsg(const IrcMessage& message, std::chrono::steady_clock::time_point now);
    [[nodiscard]] std::vector<ScheduledChatMessage>
    handle_usernotice(const IrcMessage& message, std::chrono::steady_clock::time_point now);

    BotSettings settings_;
    std::vector<std::optional<std::chrono::steady_clock::time_point>> rule_last_sent_;
    std::optional<std::chrono::steady_clock::time_point> hype_last_sent_;
    std::size_t subscription_index_{0};
    std::size_t gift_index_{0};
};

} // namespace twitchbot

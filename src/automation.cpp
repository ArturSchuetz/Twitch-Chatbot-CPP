#include "twitchbot/automation.hpp"

#include <algorithm>

namespace twitchbot {
namespace {

std::optional<std::string> sender_from_prefix(const std::string& prefix) {
    const auto delimiter = prefix.find('!');
    if (delimiter == std::string::npos || delimiter == 0U) {
        return std::nullopt;
    }
    try {
        return normalize_login(std::string_view(prefix).substr(0U, delimiter));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> channel_from_message(const IrcMessage& message) {
    if (message.parameters.empty()) {
        return std::nullopt;
    }
    try {
        return normalize_channel(message.parameters.front());
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

AutomationEngine::AutomationEngine(BotSettings settings)
    : settings_(std::move(settings)), rule_last_sent_(settings_.message_rules.size()) {}

std::vector<ScheduledChatMessage>
AutomationEngine::handle(const IrcMessage& message,
                         const std::chrono::steady_clock::time_point now) {
    if (message.command == "PRIVMSG") {
        return handle_privmsg(message, now);
    }
    if (message.command == "USERNOTICE") {
        return handle_usernotice(message, now);
    }
    return {};
}

std::vector<ScheduledChatMessage>
AutomationEngine::handle_privmsg(const IrcMessage& message,
                                 const std::chrono::steady_clock::time_point now) {
    if (message.parameters.size() < 2U) {
        return {};
    }
    const auto sender = sender_from_prefix(message.prefix);
    const auto channel = channel_from_message(message);
    if (!sender || !channel) {
        return {};
    }

    std::vector<ScheduledChatMessage> output;
    const auto& text = message.parameters.back();
    for (std::size_t index = 0; index < settings_.message_rules.size(); ++index) {
        const auto& rule = settings_.message_rules[index];
        if (!rule.enabled || rule.sender != *sender ||
            (rule.channel && *rule.channel != *channel) ||
            text.find(rule.contains) == std::string::npos) {
            continue;
        }
        const auto last_sent = rule_last_sent_[index];
        if (last_sent && now - *last_sent < rule.cooldown) {
            continue;
        }
        rule_last_sent_[index] = now;
        output.push_back({*channel, rule.response, now + rule.delay});
    }
    return output;
}

std::vector<ScheduledChatMessage>
AutomationEngine::handle_usernotice(const IrcMessage& message,
                                    const std::chrono::steady_clock::time_point now) {
    const auto& hype = settings_.hype;
    if (!hype.enabled || message.parameters.empty()) {
        return {};
    }
    const auto channel = channel_from_message(message);
    if (!channel || *channel != hype.channel) {
        return {};
    }
    const auto event = message.tags.find("msg-id");
    if (event == message.tags.end()) {
        return {};
    }
    if (hype_last_sent_ && now - *hype_last_sent_ < hype.cooldown) {
        return {};
    }

    std::string response;
    if (event->second == "sub" || event->second == "resub") {
        response = hype.subscription_messages.at(subscription_index_);
        subscription_index_ = (subscription_index_ + 1U) % hype.subscription_messages.size();
    } else if (event->second == "subgift" || event->second == "submysterygift") {
        response = hype.gift_messages.at(gift_index_);
        gift_index_ = (gift_index_ + 1U) % hype.gift_messages.size();
    } else {
        return {};
    }
    hype_last_sent_ = now;
    return {{*channel, std::move(response), now + hype.delay}};
}

void AutomationEngine::reset_session() noexcept {
    std::fill(rule_last_sent_.begin(), rule_last_sent_.end(), std::nullopt);
    hype_last_sent_.reset();
    subscription_index_ = 0;
    gift_index_ = 0;
}

} // namespace twitchbot

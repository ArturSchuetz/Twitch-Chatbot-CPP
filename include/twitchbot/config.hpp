#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace twitchbot {

using Environment = std::unordered_map<std::string, std::string>;

struct MessageRule {
    bool enabled{false};
    std::string sender;
    std::optional<std::string> channel;
    std::string contains;
    std::string response;
    std::chrono::seconds delay{0};
    std::chrono::seconds cooldown{0};
};

struct HypeSettings {
    bool enabled{false};
    std::string channel;
    std::vector<std::string> subscription_messages;
    std::vector<std::string> gift_messages;
    std::chrono::seconds delay{1};
    std::chrono::seconds cooldown{2};
};

struct BotSettings {
    std::vector<MessageRule> message_rules;
    HypeSettings hype;
};

struct RuntimeConfig {
    std::string username;
    std::string oauth_token;
    std::vector<std::string> channels;
    bool log_raw_irc{false};
    BotSettings bot;
};

[[nodiscard]] Environment load_env_file(const std::filesystem::path& path);
[[nodiscard]] Environment overlay_process_environment(Environment base);
[[nodiscard]] RuntimeConfig parse_runtime_config(const Environment& environment,
                                                 std::string_view bot_json);

[[nodiscard]] std::string normalize_login(std::string_view value);
[[nodiscard]] std::string normalize_channel(std::string_view value);
[[nodiscard]] std::string normalize_oauth_token(std::string_view value);

} // namespace twitchbot

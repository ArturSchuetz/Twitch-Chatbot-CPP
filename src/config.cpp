#include "twitchbot/config.hpp"

#include "twitchbot/irc.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace twitchbot {
namespace {

using json = nlohmann::json;

std::string trim(std::string value) {
    const auto is_space = [](const unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

std::string require_environment(const Environment& environment, const std::string& key) {
    const auto iterator = environment.find(key);
    if (iterator == environment.end() || trim(iterator->second).empty()) {
        throw std::runtime_error("Missing required environment variable: " + key);
    }
    return trim(iterator->second);
}

bool parse_bool(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    throw std::runtime_error("TWITCH_LOG_RAW_IRC must be true or false");
}

// The field name labels the value in a validation error; their order is intentional.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void reject_line_breaks(std::string_view field, std::string_view value) {
    if (value.find('\r') != std::string_view::npos || value.find('\n') != std::string_view::npos) {
        throw std::runtime_error(std::string(field) + " must not contain CR or LF");
    }
}

// The two integer parameters represent distinct configuration bounds.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::chrono::seconds read_seconds(const json& object, const char* key, const int default_value,
                                  const int maximum) {
    const auto value = object.value(key, default_value);
    if (value < 0 || value > maximum) {
        throw std::runtime_error(std::string(key) + " is outside the allowed range");
    }
    return std::chrono::seconds(value);
}

std::string read_message(const json& value, const std::string& field) {
    if (!value.is_string()) {
        throw std::runtime_error(field + " must be a string");
    }
    auto result = value.get<std::string>();
    reject_line_breaks(field, result);
    if (result.empty() || result.size() > 450U) {
        throw std::runtime_error(field + " must contain 1 to 450 bytes");
    }
    return result;
}

std::vector<std::string> read_messages(const json& object, const char* key) {
    std::vector<std::string> messages;
    if (!object.contains(key)) {
        return messages;
    }
    if (!object.at(key).is_array()) {
        throw std::runtime_error(std::string(key) + " must be an array");
    }
    for (const auto& value : object.at(key)) {
        messages.push_back(read_message(value, key));
    }
    return messages;
}

BotSettings parse_bot_settings(std::string_view bot_json) {
    json root;
    try {
        root = json::parse(bot_json);
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string("Invalid bot configuration JSON: ") + error.what());
    }
    if (!root.is_object()) {
        throw std::runtime_error("Bot configuration root must be an object");
    }

    BotSettings settings;
    if (root.contains("messageRules")) {
        if (!root.at("messageRules").is_array()) {
            throw std::runtime_error("messageRules must be an array");
        }
        for (const auto& item : root.at("messageRules")) {
            if (!item.is_object()) {
                throw std::runtime_error("Every message rule must be an object");
            }
            MessageRule rule;
            rule.enabled = item.value("enabled", false);
            rule.sender = normalize_login(item.value("sender", ""));
            if (item.contains("channel") && !item.at("channel").is_null()) {
                rule.channel = normalize_channel(item.at("channel").get<std::string>());
            }
            rule.contains = item.value("contains", "");
            reject_line_breaks("messageRules.contains", rule.contains);
            if (rule.contains.empty() || rule.contains.size() > 400U) {
                throw std::runtime_error("messageRules.contains must contain 1 to 400 bytes");
            }
            rule.response = read_message(item.at("response"), "messageRules.response");
            rule.delay = read_seconds(item, "delaySeconds", 0, 3'600);
            rule.cooldown = read_seconds(item, "cooldownSeconds", 0, 86'400);
            settings.message_rules.push_back(std::move(rule));
        }
    }

    if (root.contains("hype")) {
        const auto& hype = root.at("hype");
        if (!hype.is_object()) {
            throw std::runtime_error("hype must be an object");
        }
        settings.hype.enabled = hype.value("enabled", false);
        const auto channel = hype.value("channel", "");
        if (!channel.empty()) {
            settings.hype.channel = normalize_channel(channel);
        }
        settings.hype.subscription_messages = read_messages(hype, "subscriptionMessages");
        settings.hype.gift_messages = read_messages(hype, "giftMessages");
        settings.hype.delay = read_seconds(hype, "delaySeconds", 1, 3'600);
        settings.hype.cooldown = read_seconds(hype, "cooldownSeconds", 2, 86'400);

        if (settings.hype.enabled &&
            (settings.hype.channel.empty() || settings.hype.subscription_messages.empty() ||
             settings.hype.gift_messages.empty())) {
            throw std::runtime_error(
                "Enabled hype requires a channel plus non-empty subscription and gift messages");
        }
    }
    return settings;
}

} // namespace

Environment load_env_file(const std::filesystem::path& path) {
    Environment environment;
    std::ifstream input(path);
    if (!input.is_open()) {
        return environment;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        auto value = trim(line);
        if (value.empty() || value.front() == '#') {
            continue;
        }
        constexpr std::string_view export_prefix = "export ";
        if (value.starts_with(export_prefix)) {
            value = trim(value.substr(export_prefix.size()));
        }
        const auto equals = value.find('=');
        if (equals == std::string::npos || equals == 0U) {
            throw std::runtime_error("Invalid .env entry at line " + std::to_string(line_number));
        }
        auto key = trim(value.substr(0, equals));
        auto entry = trim(value.substr(equals + 1U));
        if (entry.size() >= 2U && ((entry.front() == '"' && entry.back() == '"') ||
                                   (entry.front() == '\'' && entry.back() == '\''))) {
            entry = entry.substr(1U, entry.size() - 2U);
        }
        environment.insert_or_assign(std::move(key), std::move(entry));
    }
    return environment;
}

Environment overlay_process_environment(Environment base) {
    static constexpr const char* keys[] = {"TWITCH_BOT_USERNAME", "TWITCH_OAUTH_TOKEN",
                                           "TWITCH_CHANNELS", "TWITCH_CONFIG_FILE",
                                           "TWITCH_LOG_RAW_IRC"};
    for (const auto* key : keys) {
#ifdef _WIN32
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, key) == 0 && value != nullptr) {
            base.insert_or_assign(key, value);
        }
        std::free(value);
#else
        if (const auto* value = std::getenv(key); value != nullptr) {
            base.insert_or_assign(key, value);
        }
#endif
    }
    return base;
}

RuntimeConfig parse_runtime_config(const Environment& environment,
                                   const std::string_view bot_json) {
    RuntimeConfig config;
    config.username = normalize_login(require_environment(environment, "TWITCH_BOT_USERNAME"));
    config.oauth_token =
        normalize_oauth_token(require_environment(environment, "TWITCH_OAUTH_TOKEN"));

    std::set<std::string> seen_channels;
    std::stringstream stream(require_environment(environment, "TWITCH_CHANNELS"));
    std::string item;
    while (std::getline(stream, item, ',')) {
        const auto channel = normalize_channel(item);
        if (seen_channels.insert(channel).second) {
            config.channels.push_back(channel);
        }
    }
    if (config.channels.empty() || config.channels.size() > 100U) {
        throw std::runtime_error("TWITCH_CHANNELS must contain between 1 and 100 unique channels");
    }

    if (const auto iterator = environment.find("TWITCH_LOG_RAW_IRC");
        iterator != environment.end() && !trim(iterator->second).empty()) {
        config.log_raw_irc = parse_bool(trim(iterator->second));
    }
    config.bot = parse_bot_settings(bot_json);
    return config;
}

std::string normalize_login(std::string_view value) {
    auto result = trim(std::string(value));
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    static const std::regex login_pattern("^[a-z0-9_]{1,25}$");
    if (!std::regex_match(result, login_pattern)) {
        throw std::runtime_error(
            "Twitch login names may only contain a-z, 0-9, and _ (1-25 chars)");
    }
    return result;
}

std::string normalize_channel(std::string_view value) {
    auto result = trim(std::string(value));
    if (result.starts_with('#')) {
        result.erase(result.begin());
    }
    return normalize_login(result);
}

std::string normalize_oauth_token(std::string_view value) {
    auto token = trim(std::string(value));
    reject_line_breaks("TWITCH_OAUTH_TOKEN", token);
    constexpr std::string_view prefix = "oauth:";
    if (token.starts_with(prefix)) {
        token.erase(0U, prefix.size());
    }
    if (token == "replace_me" || token == "your_user_access_token" || token.size() < 10U) {
        throw std::runtime_error("TWITCH_OAUTH_TOKEN is missing or still contains a placeholder");
    }
    if (!std::all_of(token.begin(), token.end(), [](const unsigned char character) {
            return std::isalnum(character) != 0 || character == '_' || character == '-';
        })) {
        throw std::runtime_error("TWITCH_OAUTH_TOKEN contains invalid characters");
    }
    auto normalized = std::string(prefix) + token;
    static_cast<void>(encode_irc_line("PASS " + normalized));
    return normalized;
}

} // namespace twitchbot

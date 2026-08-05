#include "twitchbot/automation.hpp"
#include "twitchbot/config.hpp"
#include "twitchbot/irc.hpp"
#include "twitchbot/rate_limiter.hpp"
#include "twitchbot/session_protocol.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;

int failures = 0;

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "  FAIL: " << message << '\n';
    }
}

template <typename Function> void check_throws(Function&& function, const std::string& message) {
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const std::exception&) {
        check(true, message);
    }
}

void run(const std::string& name, const std::function<void()>& test) {
    std::cout << "RUN  " << name << '\n';
    const auto before = failures;
    try {
        test();
    } catch (const std::exception& exception) {
        ++failures;
        std::cerr << "  EXCEPTION: " << exception.what() << '\n';
    }
    std::cout << (failures == before ? "PASS " : "FAIL ") << name << '\n';
}

twitchbot::Environment valid_environment() {
    return {{"TWITCH_BOT_USERNAME", "Example_Bot"},
            {"TWITCH_OAUTH_TOKEN", "unit_test_token_value"},
            {"TWITCH_CHANNELS", "#One, two,ONE"}};
}

twitchbot::RuntimeConfig valid_config() {
    return twitchbot::parse_runtime_config(valid_environment(), R"({})");
}

twitchbot::IrcMessage parse_or_throw(const std::string& line) {
    std::string error;
    auto message = twitchbot::parse_irc_message(line, &error);
    if (!message) {
        throw std::runtime_error(error);
    }
    return *message;
}

void parser_tests() {
    const auto message =
        parse_or_throw("@badge-info=subscriber\\s12;display-name=Artur\\sSchuetz;empty= "
                       ":viewer!viewer@example PRIVMSG #channel :hello world");
    check(message.command == "PRIVMSG", "command is parsed");
    check(message.prefix == "viewer!viewer@example", "prefix is parsed");
    check(message.parameters.size() == 2U, "parameters include channel and trailing text");
    check(message.parameters[0] == "#channel", "channel is preserved");
    check(message.parameters[1] == "hello world", "trailing text is preserved");
    check(message.tags.at("badge-info") == "subscriber 12", "tag spaces are unescaped");
    check(message.tags.at("display-name") == "Artur Schuetz", "display tag is unescaped");
    check(message.tags.at("empty").empty(), "empty tag value is supported");

    check(!twitchbot::parse_irc_message(""), "empty line is rejected");
    check(!twitchbot::parse_irc_message("@tag=value"), "tag-only line is rejected");
    check(!twitchbot::parse_irc_message("BAD-COMMAND value"), "invalid command is rejected");
    check(twitchbot::parse_irc_message("PING").has_value(), "parameterless PING parses safely");

    std::mt19937 generator(42U);
    std::uniform_int_distribution<int> length_distribution(0, 128);
    std::uniform_int_distribution<int> byte_distribution(1, 255);
    for (int iteration = 0; iteration < 1'000; ++iteration) {
        std::string bytes;
        const auto length = length_distribution(generator);
        bytes.reserve(static_cast<std::size_t>(length));
        for (int index = 0; index < length; ++index) {
            bytes.push_back(static_cast<char>(byte_distribution(generator)));
        }
        static_cast<void>(twitchbot::parse_irc_message(bytes));
    }
    check(true, "random parser inputs do not throw");
}

void stream_tests() {
    twitchbot::IrcStreamAssembler assembler;
    check(assembler.push("PING :one\r").empty(), "partial frame is retained");
    const auto completed = assembler.push("\nPING :two\r\n");
    check(completed.size() == 2U, "two complete frames are emitted");
    check(completed[0] == "PING :one", "fragmented frame is reconstructed");
    check(completed[1] == "PING :two", "second frame is reconstructed");
    check(assembler.buffered_bytes() == 0U, "complete frames leave no tail");

    const std::string oversized(twitchbot::max_irc_line_bytes + 1U, 'x');
    check_throws([&]() { static_cast<void>(assembler.push(oversized)); },
                 "oversized incomplete frame is rejected");
    assembler.reset();
    check(assembler.buffered_bytes() == 0U, "assembler reset clears state");
}

void output_tests() {
    check(twitchbot::encode_irc_line("PING :abc") == "PING :abc\r\n", "encoder uses exact CRLF");
    check(twitchbot::make_privmsg("channel", "hello") == "PRIVMSG #channel :hello\r\n",
          "PRIVMSG is encoded");
    check_throws([]() { static_cast<void>(twitchbot::encode_irc_line("PING\nJOIN #bad")); },
                 "CRLF injection is rejected");
    check_throws(
        []() { static_cast<void>(twitchbot::make_privmsg("channel", "hello\r\nJOIN #bad")); },
        "message injection is rejected");
    check_throws([]() { static_cast<void>(twitchbot::encode_irc_line(std::string(511U, 'x'))); },
                 "outgoing line length is bounded");
}

void config_tests() {
    const auto config = valid_config();
    check(config.username == "example_bot", "username is normalized");
    check(config.channels == std::vector<std::string>({"one", "two"}),
          "channels are normalized and deduplicated");
    check(config.oauth_token.starts_with("oauth:"), "raw token receives IRC prefix");
    check(!config.log_raw_irc, "raw logging defaults off");

    auto placeholder = valid_environment();
    placeholder["TWITCH_OAUTH_TOKEN"] = "replace_me";
    check_throws(
        [&]() { static_cast<void>(twitchbot::parse_runtime_config(placeholder, R"({})")); },
        "placeholder token is rejected");

    auto maximum_token = valid_environment();
    maximum_token["TWITCH_OAUTH_TOKEN"] = std::string(499U, 'a');
    const auto maximum_config = twitchbot::parse_runtime_config(maximum_token, R"({})");
    check(maximum_config.oauth_token.size() == 505U,
          "maximum token produces an exactly 510-byte PASS payload");
    auto oversized_token = valid_environment();
    oversized_token["TWITCH_OAUTH_TOKEN"] = std::string(500U, 'a');
    check_throws(
        [&]() { static_cast<void>(twitchbot::parse_runtime_config(oversized_token, R"({})")); },
        "token that cannot fit in PASS is rejected during configuration validation");

    auto no_channels = valid_environment();
    no_channels["TWITCH_CHANNELS"] = " , ";
    check_throws(
        [&]() { static_cast<void>(twitchbot::parse_runtime_config(no_channels, R"({})")); },
        "empty channel list is rejected");

    check_throws(
        [&]() {
            static_cast<void>(twitchbot::parse_runtime_config(
                valid_environment(),
                R"({"hype":{"enabled":true,"channel":"one","subscriptionMessages":[],"giftMessages":[]}})"));
        },
        "enabled hype requires messages");

    const auto configured = twitchbot::parse_runtime_config(valid_environment(),
                                                            R"({
          "messageRules": [{
            "enabled": true,
            "sender": "Exact_User",
            "channel": "#One",
            "contains": "trigger",
            "response": "response",
            "delaySeconds": 2,
            "cooldownSeconds": 5
          }],
          "hype": {
            "enabled": true,
            "channel": "two",
            "subscriptionMessages": ["sub-a", "sub-b"],
            "giftMessages": ["gift-a"],
            "delaySeconds": 1,
            "cooldownSeconds": 2
          }
        })");
    check(configured.bot.message_rules[0].sender == "exact_user", "rule sender is normalized");
    check(configured.bot.hype.gift_messages.size() == 1U,
          "unequal hype vectors are accepted safely");
}

void protocol_tests() {
    const auto config = valid_config();
    twitchbot::SessionProtocol protocol(config);
    const auto initial = protocol.begin();
    check(initial.size() == 3U, "PASS, NICK, and CAP are queued");
    check(protocol.state() == twitchbot::SessionState::authenticating,
          "session enters authenticating state");
    check(initial[0].data.starts_with("PASS oauth:"), "PASS uses normalized token");
    check(initial[0].data.ends_with("\r\n"), "initial commands use CRLF");

    check(protocol.handle(parse_or_throw(":tmi.twitch.tv 001 example_bot :Welcome")).empty(),
          "authentication alone does not join");
    const auto ready = protocol.handle(parse_or_throw(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/membership twitch.tv/tags twitch.tv/commands"));
    check(protocol.ready(), "session becomes ready after auth and capability ACK");
    check(ready.size() == 3U, "ready action plus two joins are emitted");
    check(ready[1].outbound_kind == twitchbot::OutboundKind::join, "JOIN is rate-limit classified");

    twitchbot::SessionProtocol partial(valid_config());
    static_cast<void>(partial.begin());
    static_cast<void>(partial.handle(parse_or_throw(":tmi.twitch.tv 001 example_bot :Welcome")));
    static_cast<void>(partial.handle(
        parse_or_throw(":tmi.twitch.tv CAP * ACK :twitch.tv/membership twitch.tv/tags")));
    check(!partial.ready(), "partial capability ACK cannot become ready");
    const auto finally_ready =
        partial.handle(parse_or_throw(":tmi.twitch.tv CAP * ACK :twitch.tv/commands"));
    check(!finally_ready.empty() && partial.ready(), "split capability ACKs are accumulated");

    twitchbot::SessionProtocol rejected(valid_config());
    static_cast<void>(rejected.begin());
    const auto fatal_cap =
        rejected.handle(parse_or_throw(":tmi.twitch.tv CAP * NAK :twitch.tv/tags"));
    check(fatal_cap.size() == 1U && fatal_cap[0].kind == twitchbot::ProtocolActionKind::fatal,
          "capability NAK is fatal");

    twitchbot::SessionProtocol bad_login(valid_config());
    static_cast<void>(bad_login.begin());
    const auto fatal_login =
        bad_login.handle(parse_or_throw(":tmi.twitch.tv NOTICE * :Login authentication failed"));
    check(fatal_login.size() == 1U && fatal_login[0].kind == twitchbot::ProtocolActionKind::fatal,
          "authentication failure is fatal");

    twitchbot::SessionProtocol keepalive(valid_config());
    const auto pong = keepalive.handle(parse_or_throw("PING :payload with spaces"));
    check(pong.size() == 1U && pong[0].data == "PONG :payload with spaces\r\n",
          "PONG preserves the exact payload");
    check(keepalive.handle(parse_or_throw("PING")).empty(), "malformed PING is ignored safely");
    const auto reconnect = keepalive.handle(parse_or_throw(":tmi.twitch.tv RECONNECT"));
    check(reconnect.size() == 1U && reconnect[0].kind == twitchbot::ProtocolActionKind::reconnect,
          "RECONNECT requests a new session");
}

void automation_tests() {
    twitchbot::BotSettings settings;
    settings.message_rules.push_back(
        {true, "exact_user", std::string("channel"), "trigger", "rule-response", 2s, 5s});
    settings.hype = {true, "channel", {"sub-a", "sub-b"}, {"gift-a"}, 1s, 2s};
    twitchbot::AutomationEngine engine(settings);
    const auto now = std::chrono::steady_clock::time_point(100s);

    auto rule_message = parse_or_throw(":exact_user!u@h PRIVMSG #channel :a trigger appears");
    const auto first = engine.handle(rule_message, now);
    check(first.size() == 1U && first[0].text == "rule-response", "exact rule triggers");
    check(first[0].due == now + 2s, "rule delay is scheduled with steady time");
    check(engine.handle(rule_message, now + 1s).empty(), "rule cooldown suppresses a burst");

    auto spoof = parse_or_throw(":not_exact_user!u@h PRIVMSG #channel :trigger");
    check(engine.handle(spoof, now + 10s).empty(),
          "substring username does not match exact sender");
    auto wrong_channel = parse_or_throw(":exact_user!u@h PRIVMSG #other :trigger");
    check(engine.handle(wrong_channel, now + 10s).empty(), "channel allowlist is exact");

    auto sub = parse_or_throw("@msg-id=sub :tmi.twitch.tv USERNOTICE #channel :notice");
    auto resub = parse_or_throw("@msg-id=resub :tmi.twitch.tv USERNOTICE #channel :notice");
    auto gift = parse_or_throw("@msg-id=subgift :tmi.twitch.tv USERNOTICE #channel :notice");
    const auto sub_a = engine.handle(sub, now + 10s);
    const auto sub_b = engine.handle(resub, now + 13s);
    const auto gift_a = engine.handle(gift, now + 16s);
    check(sub_a[0].text == "sub-a" && sub_b[0].text == "sub-b", "subscription messages rotate");
    check(gift_a[0].text == "gift-a", "gift vector uses its own size");
    check(engine.handle(gift, now + 17s).empty(), "hype cooldown suppresses a burst");
}

void rate_limit_tests() {
    twitchbot::RateLimiter limiter;
    const auto start = std::chrono::steady_clock::time_point(100s);
    check(limiter.next_allowed(twitchbot::OutboundKind::system, "", start) == start,
          "system messages are immediate");
    limiter.record(twitchbot::OutboundKind::chat, "one", start);
    check(limiter.next_allowed(twitchbot::OutboundKind::chat, "one", start) == start + 1s,
          "per-channel chat interval is one second");

    limiter.reset();
    for (int index = 0; index < 20; ++index) {
        limiter.record(twitchbot::OutboundKind::chat, "channel-" + std::to_string(index), start);
    }
    check(limiter.next_allowed(twitchbot::OutboundKind::chat, "new", start) == start + 30s,
          "global chat bucket limits 20 messages per 30 seconds");

    limiter.reset();
    for (int index = 0; index < 20; ++index) {
        limiter.record(twitchbot::OutboundKind::join, "", start);
    }
    check(limiter.next_allowed(twitchbot::OutboundKind::join, "", start) == start + 10s,
          "JOIN bucket limits 20 attempts per 10 seconds");
}

} // namespace

int main() noexcept {
    try {
        run("IRC parser and tag decoding", parser_tests);
        run("TCP stream assembly", stream_tests);
        run("IRC output safety", output_tests);
        run("configuration validation", config_tests);
        run("authentication and capabilities", protocol_tests);
        run("legacy automation reconstruction", automation_tests);
        run("Twitch rate limits", rate_limit_tests);
        if (failures != 0) {
            std::cerr << failures << " assertion(s) failed.\n";
            return 1;
        }
        std::cout << "All assertions passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "Unexpected test-runner failure: %s\n", exception.what());
        return 1;
    } catch (...) {
        std::fputs("Unexpected non-standard test-runner failure.\n", stderr);
        return 1;
    }
}

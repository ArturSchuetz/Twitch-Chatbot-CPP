#include "twitchbot/session_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace twitchbot {
namespace {

constexpr std::string_view required_capabilities[] = {"twitch.tv/membership", "twitch.tv/tags",
                                                      "twitch.tv/commands"};

bool contains_login_failure(const IrcMessage& message) {
    if (message.command != "NOTICE" || message.parameters.empty()) {
        return false;
    }
    auto text = message.parameters.back();
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text.find("login authentication failed") != std::string::npos ||
           text.find("improperly formatted auth") != std::string::npos;
}

} // namespace

SessionProtocol::SessionProtocol(const RuntimeConfig& config) : config_(config) {}

std::vector<ProtocolAction> SessionProtocol::begin() {
    if (state_ != SessionState::disconnected && state_ != SessionState::tls_connected) {
        return {};
    }
    state_ = SessionState::authenticating;
    return {{ProtocolActionKind::send,
             encode_irc_line("PASS " + config_.oauth_token),
             OutboundKind::system,
             {}},
            {ProtocolActionKind::send,
             encode_irc_line("NICK " + config_.username),
             OutboundKind::system,
             {}},
            {ProtocolActionKind::send,
             encode_irc_line("CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands"),
             OutboundKind::system,
             {}}};
}

std::vector<ProtocolAction> SessionProtocol::handle(const IrcMessage& message) {
    if (message.command == "PING") {
        if (message.parameters.empty()) {
            return {};
        }
        return {{ProtocolActionKind::send,
                 encode_irc_line("PONG :" + message.parameters.front()),
                 OutboundKind::system,
                 {}}};
    }
    if (message.command == "RECONNECT") {
        state_ = SessionState::reconnect_requested;
        return {{ProtocolActionKind::reconnect,
                 "Twitch requested reconnect",
                 OutboundKind::system,
                 {}}};
    }
    if (contains_login_failure(message)) {
        state_ = SessionState::failed;
        return {{ProtocolActionKind::fatal, "Twitch rejected the login", OutboundKind::system, {}}};
    }
    if (message.command == "CAP" && message.parameters.size() >= 2U) {
        const auto& status = message.parameters[1U];
        if (status == "NAK") {
            state_ = SessionState::failed;
            return {{ProtocolActionKind::fatal,
                     "Twitch rejected required IRC capabilities",
                     OutboundKind::system,
                     {}}};
        }
        if (status == "ACK") {
            std::stringstream capabilities(message.parameters.back());
            std::string capability;
            while (capabilities >> capability) {
                if (capability.starts_with('-')) {
                    acknowledged_capabilities_.erase(capability.substr(1U));
                } else {
                    acknowledged_capabilities_.insert(capability);
                }
            }
            state_ = SessionState::capability_negotiation;
        }
    }
    if (message.command == "001" || message.command == "GLOBALUSERSTATE") {
        authenticated_ = true;
    }
    return become_ready_if_possible();
}

std::vector<ProtocolAction> SessionProtocol::become_ready_if_possible() {
    if (state_ == SessionState::ready || state_ == SessionState::failed ||
        state_ == SessionState::reconnect_requested || !authenticated_) {
        return {};
    }
    const bool capabilities_ready =
        std::all_of(std::begin(required_capabilities), std::end(required_capabilities),
                    [this](const std::string_view capability) {
                        return acknowledged_capabilities_.contains(std::string(capability));
                    });
    if (!capabilities_ready) {
        return {};
    }

    state_ = SessionState::ready;
    std::vector<ProtocolAction> actions;
    actions.push_back({ProtocolActionKind::ready, "Session ready", OutboundKind::system, {}});
    for (const auto& channel : config_.channels) {
        actions.push_back({ProtocolActionKind::send, encode_irc_line("JOIN #" + channel),
                           OutboundKind::join, channel});
    }
    return actions;
}

SessionState SessionProtocol::state() const noexcept { return state_; }

bool SessionProtocol::ready() const noexcept { return state_ == SessionState::ready; }

} // namespace twitchbot

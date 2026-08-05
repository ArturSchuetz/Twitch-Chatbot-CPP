#pragma once

#include "twitchbot/config.hpp"
#include "twitchbot/irc.hpp"
#include "twitchbot/outbound.hpp"

#include <set>
#include <string>
#include <vector>

namespace twitchbot {

enum class SessionState {
    disconnected,
    tls_connected,
    authenticating,
    capability_negotiation,
    ready,
    reconnect_requested,
    failed
};

enum class ProtocolActionKind { send, ready, reconnect, fatal };

struct ProtocolAction {
    ProtocolActionKind kind{ProtocolActionKind::send};
    std::string data;
    OutboundKind outbound_kind{OutboundKind::system};
    std::string channel;
};

class SessionProtocol {
  public:
    explicit SessionProtocol(const RuntimeConfig& config);

    [[nodiscard]] std::vector<ProtocolAction> begin();
    [[nodiscard]] std::vector<ProtocolAction> handle(const IrcMessage& message);
    [[nodiscard]] SessionState state() const noexcept;
    [[nodiscard]] bool ready() const noexcept;

  private:
    [[nodiscard]] std::vector<ProtocolAction> become_ready_if_possible();

    RuntimeConfig config_;
    SessionState state_{SessionState::disconnected};
    bool authenticated_{false};
    std::set<std::string> acknowledged_capabilities_;
};

} // namespace twitchbot

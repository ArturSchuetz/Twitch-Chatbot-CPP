#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace twitchbot {

enum class OutboundKind { system, join, chat };

struct OutboundMessage {
    std::string wire_data;
    OutboundKind kind{OutboundKind::system};
    std::string channel;
    std::chrono::steady_clock::time_point due{std::chrono::steady_clock::now()};
    std::uint64_t sequence{0};
};

} // namespace twitchbot

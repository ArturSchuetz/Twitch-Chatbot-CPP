#pragma once

#include "twitchbot/config.hpp"

#include <boost/asio/io_context.hpp>

#include <memory>

namespace twitchbot {

class TwitchIrcClient {
  public:
    TwitchIrcClient(boost::asio::io_context& io, RuntimeConfig config);
    ~TwitchIrcClient();

    TwitchIrcClient(const TwitchIrcClient&) = delete;
    TwitchIrcClient& operator=(const TwitchIrcClient&) = delete;

    void start();
    void stop();
    [[nodiscard]] int exit_code() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace twitchbot

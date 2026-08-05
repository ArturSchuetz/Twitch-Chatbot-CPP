#include "twitchbot/twitch_irc_client.hpp"

#include "twitchbot/automation.hpp"
#include "twitchbot/irc.hpp"
#include "twitchbot/outbound.hpp"
#include "twitchbot/rate_limiter.hpp"
#include "twitchbot/session_protocol.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>

#include <openssl/ssl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace twitchbot {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

constexpr std::string_view twitch_host = "irc.chat.twitch.tv";
constexpr std::string_view twitch_port = "6697";
constexpr std::size_t max_outbound_queue = 1'000;

enum class SessionEndReason : std::uint8_t { network_error, reconnect_requested, fatal, stopped };

class Session final : public std::enable_shared_from_this<Session> {
  public:
    using EndHandler = std::function<void(SessionEndReason, const std::string&)>;
    using ReadyHandler = std::function<void()>;

    Session(asio::io_context& io, ssl::context& ssl_context, const RuntimeConfig& config,
            RateLimiter& limiter, EndHandler end_handler, ReadyHandler ready_handler)
        : resolver_(io), stream_(io, ssl_context), write_timer_(io), auth_timer_(io),
          config_(config), protocol_(config), automation_(config.bot), limiter_(limiter),
          end_handler_(std::move(end_handler)), ready_handler_(std::move(ready_handler)) {}

    void start() {
        auto self = shared_from_this();
        resolver_.async_resolve(twitch_host, twitch_port,
                                [self](const boost::system::error_code& error,
                                       const tcp::resolver::results_type& results) {
                                    if (error) {
                                        self->finish(SessionEndReason::network_error,
                                                     "DNS resolution failed: " + error.message());
                                        return;
                                    }
                                    self->connect(results);
                                });
    }

    void stop() { finish(SessionEndReason::stopped, "Stopped"); }

  private:
    using TlsStream = beast::ssl_stream<beast::tcp_stream>;

    void connect(const tcp::resolver::results_type& results) {
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));
        auto self = shared_from_this();
        beast::get_lowest_layer(stream_).async_connect(
            results, [self](const boost::system::error_code& error,
                            const tcp::resolver::results_type::endpoint_type&) {
                if (error) {
                    self->finish(SessionEndReason::network_error,
                                 "TCP connection failed: " + error.message());
                    return;
                }
                self->handshake();
            });
    }

    void handshake() {
        if (SSL_set_tlsext_host_name(stream_.native_handle(), twitch_host.data()) != 1) {
            finish(SessionEndReason::network_error, "Could not configure TLS SNI");
            return;
        }
        stream_.set_verify_mode(ssl::verify_peer);
        stream_.set_verify_callback(ssl::host_name_verification(std::string(twitch_host)));
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));
        auto self = shared_from_this();
        stream_.async_handshake(ssl::stream_base::client,
                                [self](const boost::system::error_code& error) {
                                    if (error) {
                                        self->finish(SessionEndReason::network_error,
                                                     "TLS handshake failed: " + error.message());
                                        return;
                                    }
                                    beast::get_lowest_layer(self->stream_).expires_never();
                                    std::cout << "TLS connection established; authenticating...\n";
                                    for (const auto& action : self->protocol_.begin()) {
                                        self->apply_action(action);
                                    }
                                    self->start_auth_timeout();
                                    self->read();
                                });
    }

    void start_auth_timeout() {
        auth_timer_.expires_after(std::chrono::seconds(20));
        auto self = shared_from_this();
        auth_timer_.async_wait([self](const boost::system::error_code& error) {
            if (!error && !self->protocol_.ready()) {
                self->finish(SessionEndReason::fatal,
                             "Timed out waiting for Twitch authentication");
            }
        });
    }

    void read() {
        if (closed_) {
            return;
        }
        auto self = shared_from_this();
        stream_.async_read_some(
            asio::buffer(read_buffer_),
            [self](const boost::system::error_code& error, const std::size_t bytes_read) {
                if (error) {
                    self->finish(SessionEndReason::network_error,
                                 "IRC read failed: " + error.message());
                    return;
                }
                try {
                    const auto lines = self->assembler_.push(
                        std::string_view(self->read_buffer_.data(), bytes_read));
                    for (const auto& line : lines) {
                        self->handle_line(line);
                        if (self->closed_) {
                            return;
                        }
                    }
                } catch (const std::exception& exception) {
                    self->finish(SessionEndReason::network_error, exception.what());
                    return;
                }
                self->read();
            });
    }

    void handle_line(const std::string& line) {
        if (config_.log_raw_irc) {
            std::cout << "< " << line << '\n';
        }
        std::string parse_error;
        const auto message = parse_irc_message(line, &parse_error);
        if (!message) {
            std::cerr << "Ignoring malformed IRC line: " << parse_error << '\n';
            return;
        }
        for (const auto& action : protocol_.handle(*message)) {
            apply_action(action);
            if (closed_) {
                return;
            }
        }
        if (!protocol_.ready()) {
            return;
        }
        for (const auto& chat : automation_.handle(*message, std::chrono::steady_clock::now())) {
            try {
                enqueue({make_privmsg(chat.channel, chat.text), OutboundKind::chat, chat.channel,
                         chat.due, next_sequence_++});
            } catch (const std::exception& exception) {
                std::cerr << "Dropping invalid automated message: " << exception.what() << '\n';
            }
        }
    }

    void apply_action(const ProtocolAction& action) {
        switch (action.kind) {
        case ProtocolActionKind::send:
            enqueue({action.data, action.outbound_kind, action.channel,
                     std::chrono::steady_clock::now(), next_sequence_++});
            break;
        case ProtocolActionKind::ready:
            auth_timer_.cancel();
            std::cout << "Authenticated; joining " << config_.channels.size() << " channel(s)."
                      << '\n';
            ready_handler_();
            break;
        case ProtocolActionKind::reconnect:
            finish(SessionEndReason::reconnect_requested, action.data);
            break;
        case ProtocolActionKind::fatal:
            finish(SessionEndReason::fatal, action.data);
            break;
        }
    }

    void enqueue(OutboundMessage message) {
        if (closed_) {
            return;
        }
        if (outbound_.size() >= max_outbound_queue) {
            if (message.kind == OutboundKind::chat) {
                std::cerr << "Dropping chat message because the outbound queue is full.\n";
                return;
            }
            finish(SessionEndReason::network_error, "Outbound queue exhausted");
            return;
        }
        outbound_.push_back(std::move(message));
        schedule_write();
    }

    void schedule_write() {
        if (closed_ || write_in_progress_ || outbound_.empty()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        auto selected = outbound_.begin();
        auto selected_time =
            std::max(selected->due, limiter_.next_allowed(selected->kind, selected->channel, now));
        for (auto iterator = std::next(outbound_.begin()); iterator != outbound_.end();
             ++iterator) {
            const auto allowed = std::max(
                iterator->due, limiter_.next_allowed(iterator->kind, iterator->channel, now));
            if (allowed < selected_time ||
                (allowed == selected_time && iterator->sequence < selected->sequence)) {
                selected = iterator;
                selected_time = allowed;
            }
        }
        if (selected_time > now) {
            write_timer_.expires_at(selected_time);
            auto self = shared_from_this();
            write_timer_.async_wait([self](const boost::system::error_code& error) {
                if (!error) {
                    self->schedule_write();
                }
            });
            return;
        }

        current_write_ = std::move(*selected);
        outbound_.erase(selected);
        write_in_progress_ = true;
        auto self = shared_from_this();
        asio::async_write(stream_, asio::buffer(current_write_->wire_data),
                          [self](const boost::system::error_code& error, const std::size_t) {
                              if (error) {
                                  self->finish(SessionEndReason::network_error,
                                               "IRC write failed: " + error.message());
                                  return;
                              }
                              const auto sent_at = std::chrono::steady_clock::now();
                              self->limiter_.record(self->current_write_->kind,
                                                    self->current_write_->channel, sent_at);
                              self->current_write_.reset();
                              self->write_in_progress_ = false;
                              self->schedule_write();
                          });
    }

    void finish(const SessionEndReason reason, const std::string& message) {
        if (closed_) {
            return;
        }
        closed_ = true;
        resolver_.cancel();
        write_timer_.cancel();
        auth_timer_.cancel();
        boost::system::error_code ignored;
        [[maybe_unused]] const auto cancel_error =
            beast::get_lowest_layer(stream_).socket().cancel(ignored);
        [[maybe_unused]] const auto shutdown_error =
            beast::get_lowest_layer(stream_).socket().shutdown(tcp::socket::shutdown_both, ignored);
        [[maybe_unused]] const auto close_error =
            beast::get_lowest_layer(stream_).socket().close(ignored);
        end_handler_(reason, message);
    }

    tcp::resolver resolver_;
    TlsStream stream_;
    asio::steady_timer write_timer_;
    asio::steady_timer auth_timer_;
    const RuntimeConfig& config_;
    SessionProtocol protocol_;
    AutomationEngine automation_;
    RateLimiter& limiter_;
    IrcStreamAssembler assembler_;
    std::array<char, 4'096> read_buffer_{};
    std::vector<OutboundMessage> outbound_;
    std::optional<OutboundMessage> current_write_;
    EndHandler end_handler_;
    ReadyHandler ready_handler_;
    std::uint64_t next_sequence_{0};
    bool write_in_progress_{false};
    bool closed_{false};
};

} // namespace

class TwitchIrcClient::Impl {
  public:
    Impl(asio::io_context& io, RuntimeConfig config)
        : io_(io), config_(std::move(config)), ssl_context_(ssl::context::tls_client),
          reconnect_timer_(io), random_(std::random_device{}()) {
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(ssl::verify_peer);
        if (SSL_CTX_set_min_proto_version(ssl_context_.native_handle(), TLS1_2_VERSION) != 1) {
            throw std::runtime_error("Could not require TLS 1.2 or newer");
        }
    }

    void start() { schedule_connection(std::chrono::milliseconds(0)); }

    void stop() {
        if (stopping_) {
            return;
        }
        stopping_ = true;
        reconnect_timer_.cancel();
        if (session_) {
            session_->stop();
            session_.reset();
        }
    }

    [[nodiscard]] int exit_code() const noexcept { return exit_code_; }

  private:
    void schedule_connection(const std::chrono::milliseconds delay) {
        if (stopping_) {
            return;
        }
        reconnect_timer_.expires_after(delay);
        reconnect_timer_.async_wait([this](const boost::system::error_code& error) {
            if (!error && !stopping_) {
                connect();
            }
        });
    }

    void connect() {
        session_ = std::make_shared<Session>(
            io_, ssl_context_, config_, limiter_,
            [this](const SessionEndReason reason, const std::string& message) {
                handle_session_end(reason, message);
            },
            [this]() { reconnect_attempt_ = 0U; });
        session_->start();
    }

    void handle_session_end(const SessionEndReason reason, const std::string& message) {
        session_.reset();
        if (stopping_ || reason == SessionEndReason::stopped) {
            return;
        }
        if (reason == SessionEndReason::fatal) {
            std::cerr << "Fatal Twitch session error: " << message << '\n';
            exit_code_ = 1;
            stopping_ = true;
            reconnect_timer_.cancel();
            io_.stop();
            return;
        }

        std::cerr << message << "; reconnecting.\n";
        const auto exponent = std::min<std::size_t>(reconnect_attempt_, 6U);
        const auto seconds = static_cast<int>(1U << exponent);
        ++reconnect_attempt_;
        std::uniform_int_distribution<int> jitter(0, 500);
        schedule_connection(std::chrono::seconds(seconds) +
                            std::chrono::milliseconds(jitter(random_)));
    }

    asio::io_context& io_;
    RuntimeConfig config_;
    ssl::context ssl_context_;
    asio::steady_timer reconnect_timer_;
    RateLimiter limiter_;
    std::shared_ptr<Session> session_;
    std::mt19937 random_;
    std::size_t reconnect_attempt_{0};
    int exit_code_{0};
    bool stopping_{false};
};

TwitchIrcClient::TwitchIrcClient(asio::io_context& io, RuntimeConfig config)
    : impl_(std::make_unique<Impl>(io, std::move(config))) {}

TwitchIrcClient::~TwitchIrcClient() = default;

void TwitchIrcClient::start() { impl_->start(); }

void TwitchIrcClient::stop() { impl_->stop(); }

int TwitchIrcClient::exit_code() const noexcept { return impl_->exit_code(); }

} // namespace twitchbot

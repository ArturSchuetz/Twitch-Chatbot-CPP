#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace twitchbot {

inline constexpr std::size_t max_irc_line_bytes = 8'192;
inline constexpr std::size_t max_irc_buffer_bytes = 65'536;
inline constexpr std::size_t max_outgoing_payload_bytes = 510;

struct IrcMessage {
    std::map<std::string, std::string> tags;
    std::string prefix;
    std::string command;
    std::vector<std::string> parameters;
};

[[nodiscard]] std::optional<IrcMessage> parse_irc_message(std::string_view line,
                                                          std::string* error = nullptr) noexcept;
[[nodiscard]] std::string unescape_irc_tag(std::string_view value);
[[nodiscard]] std::string encode_irc_line(std::string_view line);
[[nodiscard]] std::string make_privmsg(std::string_view channel, std::string_view text);

class IrcStreamAssembler {
  public:
    [[nodiscard]] std::vector<std::string> push(std::string_view bytes);
    void reset() noexcept;
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;

  private:
    std::string buffer_;
};

} // namespace twitchbot

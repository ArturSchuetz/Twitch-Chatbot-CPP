#include "twitchbot/irc.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace twitchbot {
namespace {

void assign_error(std::string* error, std::string value) {
    if (error != nullptr) {
        *error = std::move(value);
    }
}

bool contains_line_break(std::string_view value) {
    return value.find('\r') != std::string_view::npos || value.find('\n') != std::string_view::npos;
}

} // namespace

std::string unescape_irc_tag(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool escaped = false;
    for (const char character : value) {
        if (!escaped && character == '\\') {
            escaped = true;
            continue;
        }
        if (escaped) {
            switch (character) {
            case 's':
                result.push_back(' ');
                break;
            case ':':
                result.push_back(';');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case '\\':
                result.push_back('\\');
                break;
            default:
                result.push_back(character);
                break;
            }
            escaped = false;
            continue;
        }
        result.push_back(character);
    }
    if (escaped) {
        result.push_back('\\');
    }
    return result;
}

std::optional<IrcMessage> parse_irc_message(const std::string_view line,
                                            std::string* error) noexcept {
    try {
        if (line.empty()) {
            assign_error(error, "IRC line is empty");
            return std::nullopt;
        }
        if (line.size() > max_irc_line_bytes || contains_line_break(line)) {
            assign_error(error, "IRC line is oversized or contains a line break");
            return std::nullopt;
        }

        IrcMessage message;
        std::size_t position = 0;
        if (line[position] == '@') {
            const auto end = line.find(' ', position);
            if (end == std::string_view::npos) {
                assign_error(error, "IRC tag section has no command");
                return std::nullopt;
            }
            const auto tag_section = line.substr(1U, end - 1U);
            std::size_t tag_start = 0;
            while (tag_start <= tag_section.size()) {
                const auto separator = tag_section.find(';', tag_start);
                const auto raw_tag = tag_section.substr(
                    tag_start, separator == std::string_view::npos ? std::string_view::npos
                                                                   : separator - tag_start);
                const auto equals = raw_tag.find('=');
                const auto key = raw_tag.substr(0U, equals);
                if (key.empty()) {
                    assign_error(error, "IRC tag key is empty");
                    return std::nullopt;
                }
                const auto raw_value = equals == std::string_view::npos
                                           ? std::string_view{}
                                           : raw_tag.substr(equals + 1U);
                message.tags.insert_or_assign(std::string(key), unescape_irc_tag(raw_value));
                if (separator == std::string_view::npos) {
                    break;
                }
                tag_start = separator + 1U;
            }
            position = end + 1U;
        }

        while (position < line.size() && line[position] == ' ') {
            ++position;
        }
        if (position < line.size() && line[position] == ':') {
            const auto end = line.find(' ', position);
            if (end == std::string_view::npos || end == position + 1U) {
                assign_error(error, "IRC prefix has no command");
                return std::nullopt;
            }
            message.prefix = std::string(line.substr(position + 1U, end - position - 1U));
            position = end + 1U;
        }

        while (position < line.size() && line[position] == ' ') {
            ++position;
        }
        const auto command_end = line.find(' ', position);
        const auto command_view =
            line.substr(position, command_end == std::string_view::npos ? std::string_view::npos
                                                                        : command_end - position);
        if (command_view.empty()) {
            assign_error(error, "IRC command is empty");
            return std::nullopt;
        }
        message.command.assign(command_view);
        std::transform(message.command.begin(), message.command.end(), message.command.begin(),
                       [](const unsigned char character) {
                           return static_cast<char>(std::toupper(character));
                       });
        if (!std::all_of(
                message.command.begin(), message.command.end(),
                [](const unsigned char character) { return std::isalnum(character) != 0; })) {
            assign_error(error, "IRC command contains invalid characters");
            return std::nullopt;
        }

        if (command_end == std::string_view::npos) {
            return message;
        }
        position = command_end + 1U;
        while (position < line.size()) {
            while (position < line.size() && line[position] == ' ') {
                ++position;
            }
            if (position >= line.size()) {
                break;
            }
            if (line[position] == ':') {
                message.parameters.emplace_back(line.substr(position + 1U));
                break;
            }
            const auto end = line.find(' ', position);
            message.parameters.emplace_back(line.substr(
                position, end == std::string_view::npos ? std::string_view::npos : end - position));
            if (message.parameters.size() > 32U) {
                assign_error(error, "IRC message has too many parameters");
                return std::nullopt;
            }
            if (end == std::string_view::npos) {
                break;
            }
            position = end + 1U;
        }
        return message;
    } catch (const std::exception& exception) {
        assign_error(error, std::string("IRC parse error: ") + exception.what());
        return std::nullopt;
    } catch (...) {
        assign_error(error, "Unknown IRC parse error");
        return std::nullopt;
    }
}

std::string encode_irc_line(const std::string_view line) {
    if (line.empty() || contains_line_break(line)) {
        throw std::runtime_error("Outgoing IRC line is empty or contains CR/LF");
    }
    if (line.size() > max_outgoing_payload_bytes) {
        throw std::runtime_error("Outgoing IRC line exceeds 510 bytes");
    }
    return std::string(line) + "\r\n";
}

std::string make_privmsg(const std::string_view channel, const std::string_view text) {
    if (channel.empty() || text.empty() || contains_line_break(channel) ||
        contains_line_break(text)) {
        throw std::runtime_error("PRIVMSG channel and text must be non-empty and contain no CR/LF");
    }
    const std::string normalized_channel =
        channel.starts_with('#') ? std::string(channel) : "#" + std::string(channel);
    return encode_irc_line("PRIVMSG " + normalized_channel + " :" + std::string(text));
}

std::vector<std::string> IrcStreamAssembler::push(const std::string_view bytes) {
    if (buffer_.size() + bytes.size() > max_irc_buffer_bytes) {
        throw std::runtime_error("IRC receive buffer exceeded its maximum size");
    }
    buffer_.append(bytes);
    std::vector<std::string> lines;
    while (true) {
        const auto delimiter = buffer_.find("\r\n");
        if (delimiter == std::string::npos) {
            if (buffer_.size() > max_irc_line_bytes) {
                throw std::runtime_error("IRC line exceeded its maximum size");
            }
            break;
        }
        if (delimiter > max_irc_line_bytes) {
            throw std::runtime_error("IRC line exceeded its maximum size");
        }
        lines.push_back(buffer_.substr(0U, delimiter));
        buffer_.erase(0U, delimiter + 2U);
    }
    return lines;
}

void IrcStreamAssembler::reset() noexcept { buffer_.clear(); }

std::size_t IrcStreamAssembler::buffered_bytes() const noexcept { return buffer_.size(); }

} // namespace twitchbot

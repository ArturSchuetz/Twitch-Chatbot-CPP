#include "twitchbot/config.hpp"
#include "twitchbot/twitch_irc_client.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path env_file{".env"};
    std::filesystem::path config_file;
    bool check_config{false};
    bool help{false};
};

Options parse_options(const std::span<char*> arguments) {
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument(arguments[index]);
        if ((argument == "--env" || argument == "--config") && index + 1U >= arguments.size()) {
            throw std::runtime_error(std::string(argument) + " requires a path");
        }
        if (argument == "--env") {
            options.env_file = arguments[++index];
        } else if (argument == "--config") {
            options.config_file = arguments[++index];
        } else if (argument == "--check-config") {
            options.check_config = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            throw std::runtime_error("Unknown argument: " + std::string(argument));
        }
    }
    return options;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open configuration file: " + path.string());
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

} // namespace

int main(const int argc, char** argv) noexcept {
    try {
        const auto options = parse_options(std::span<char*>(argv, static_cast<std::size_t>(argc)));
        if (options.help) {
            std::cout << "Usage: twitch-chatbot [--env PATH] [--config PATH] [--check-config]\n";
            return 0;
        }
        auto environment =
            twitchbot::overlay_process_environment(twitchbot::load_env_file(options.env_file));

        auto config_path = options.config_file;
        if (config_path.empty()) {
            if (const auto iterator = environment.find("TWITCH_CONFIG_FILE");
                iterator != environment.end() && !iterator->second.empty()) {
                config_path = iterator->second;
            } else {
                config_path = std::filesystem::path("config") / "bot.json";
            }
        }

        auto config = twitchbot::parse_runtime_config(environment, read_file(config_path));
        std::cout << "Configuration valid for bot '" << config.username << "' and "
                  << config.channels.size() << " channel(s).\n";
        if (options.check_config) {
            return 0;
        }

        boost::asio::io_context io;
        twitchbot::TwitchIrcClient client(io, std::move(config));
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&client](const boost::system::error_code& error, const int) {
            if (!error) {
                client.stop();
            }
        });
        client.start();
        io.run();
        return client.exit_code();
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "Startup failed: %s\n", exception.what());
        return 1;
    } catch (...) {
        std::fputs("Startup failed: unknown exception\n", stderr);
        return 1;
    }
}

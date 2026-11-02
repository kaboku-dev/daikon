#include "config.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "daikon/version.h"
#include "logger.h"

namespace daikon {
namespace {

void PrintHelp(std::string_view executable_name) {
    std::cout << std::format(
        R"(Redis-compatible key-value store server.

Usage: {} [options]

Options:
    -h, --help             Show this help message and exit
    -v, --version          Show application version and exit
    -b, --bind ADDRESS     Listen on this IPv4 or IPv6 address; repeat to
                           listen on several (default: all interfaces)
    -p, --port PORT        Specify TCP port (default: 6379)
    -l, --log-level LEVEL  Specify log level (default: info)
                           Possible values: debug, info, notice, warning, error
)",
        executable_name);
}

void PrintVersion() { std::cout << 'v' << kVersion << '\n'; }

[[nodiscard]]
std::uint16_t ParsePort(std::string_view str) {
    std::uint16_t port = 0;
    const char* const first = str.data();
    const char* const last = str.data() + str.size();
    const auto [ptr, ec] = std::from_chars(first, last, port);

    if (ec != std::errc{} || ptr != last || port == 0) {
        throw std::invalid_argument(
            std::format("invalid port '{}': expected 1-65535", str));
    }
    return port;
}

[[nodiscard]]
std::string ParseBindAddress(std::string_view str) {
    std::string address{str};
    in6_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1 &&
        inet_pton(AF_INET6, address.c_str(), &parsed) != 1) {
        throw std::invalid_argument(std::format(
            "invalid bind address '{}': expected an IPv4 or IPv6 address",
            str));
    }
    return address;
}

[[nodiscard]]
LogLevel ParseLogLevelArg(std::string_view str) {
    const auto level = ParseLogLevel(str);
    if (!level) {
        throw std::invalid_argument(std::format(
            "invalid log level '{}': expected debug, info, notice, warning "
            "or error",
            str));
    }
    return *level;
}

}  // namespace

std::optional<Config> ParseArgs(std::span<const std::string_view> args) {
    Config config;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];

        if (arg == "-h" || arg == "--help") {
            PrintHelp(args[0]);
            return std::nullopt;
        }
        if (arg == "-v" || arg == "--version") {
            PrintVersion();
            return std::nullopt;
        }

        const bool takes_value = arg == "-b" || arg == "--bind" ||
                                 arg == "-p" || arg == "--port" ||
                                 arg == "-l" || arg == "--log-level";
        if (!takes_value) {
            throw std::invalid_argument(
                std::format("unknown option '{}' (see --help)", arg));
        }
        if (i + 1 == args.size()) {
            throw std::invalid_argument(
                std::format("option '{}' requires a value", arg));
        }

        ++i;
        const std::string_view value = args[i];
        if (arg == "-b" || arg == "--bind") {
            config.bind_addresses.push_back(ParseBindAddress(value));
        } else if (arg == "-p" || arg == "--port") {
            config.port = ParsePort(value);
        } else {
            config.log_level = ParseLogLevelArg(value);
        }
    }

    return config;
}

}  // namespace daikon

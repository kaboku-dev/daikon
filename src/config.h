#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "logger.h"

namespace daikon {

struct Config {
    LogLevel log_level = kDefaultLogLevel;
    // Numeric IPv4 or IPv6 addresses. Empty means all IPv4 interfaces, plus
    // all IPv6 interfaces if the system supports IPv6.
    std::vector<std::string> bind_addresses;
    // Shared by all bind addresses. 0 lets the OS pick a free port. ParseArgs
    // never produces it; tests use it to avoid port clashes.
    std::uint16_t port = 6379;
};

// Parses command-line arguments, where args[0] is the executable name.
// Returns std::nullopt when the program should exit successfully without
// starting the server (--help, --version). Throws std::invalid_argument on
// invalid arguments.
[[nodiscard]]
std::optional<Config> ParseArgs(std::span<const std::string_view> args);

}  // namespace daikon

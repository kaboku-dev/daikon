#include "logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <format>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace daikon {
namespace {

struct LevelName {
    LogLevel level;
    std::string_view name;
};

constexpr std::array<LevelName, 5> kLevelNames{{
    {.level = LogLevel::kDebug, .name = "debug"},
    {.level = LogLevel::kInfo, .name = "info"},
    {.level = LogLevel::kNotice, .name = "notice"},
    {.level = LogLevel::kWarning, .name = "warning"},
    {.level = LogLevel::kErr, .name = "error"},
}};

// Atomic so the level can be changed at runtime at any moment.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<LogLevel> g_log_level{kDefaultLogLevel};

[[nodiscard]]
std::string_view LevelToName(LogLevel level) noexcept {
    const auto* it = std::ranges::find(kLevelNames, level, &LevelName::level);
    return it != kLevelNames.end() ? it->name : "unknown";
}

}  // namespace

void SetLogLevel(LogLevel level) noexcept {
    g_log_level.store(level, std::memory_order_relaxed);
}

bool IsLogEnabled(LogLevel level) noexcept {
    return level >= g_log_level.load(std::memory_order_relaxed);
}

std::optional<LogLevel> ParseLogLevel(std::string_view name) noexcept {
    const auto* it = std::ranges::find(kLevelNames, name, &LevelName::name);
    if (it == kLevelNames.end()) {
        return std::nullopt;
    }
    return it->level;
}

namespace detail {

void VLog(LogLevel level, std::string_view fmt,
          std::format_args args) noexcept {
    try {
        const auto now = std::chrono::floor<std::chrono::milliseconds>(
            std::chrono::system_clock::now());

        std::string line =
            std::format("{:%FT%TZ} [{}] ", now, LevelToName(level));
        std::vformat_to(std::back_inserter(line), fmt, args);
        line += '\n';

        // A single write per line keeps lines from different threads intact.
        std::cerr << line;
    } catch (...) {
        // Logging must never throw an exception: fall back to a fixed message,
        // which needs no allocation. If even that fails, there is nowhere left
        // to report it.
        constexpr std::string_view kFallback =
            "(failed to format log message)\n";

        static_cast<void>(
            std::fwrite(kFallback.data(), 1, kFallback.size(), stderr));
    }
}

}  // namespace detail

}  // namespace daikon

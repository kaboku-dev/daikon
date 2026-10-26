#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string_view>

namespace daikon {

enum class LogLevel : std::uint8_t { kDebug, kInfo, kNotice, kWarning, kErr };

constexpr LogLevel kDefaultLogLevel = LogLevel::kInfo;

void SetLogLevel(LogLevel level) noexcept;

[[nodiscard]]
bool IsLogEnabled(LogLevel level) noexcept;

// Accepts the names listed in --help: debug, info, notice, warning, error.
[[nodiscard]]
std::optional<LogLevel> ParseLogLevel(std::string_view name) noexcept;

namespace detail {

// Non-template to keep Log() thin. Does not filter by level: that's Log()'s job
void VLog(LogLevel level, std::string_view fmt, std::format_args args) noexcept;

}  // namespace detail

// Writes one line to stderr. The message must not end with a newline.
template <typename... Args>
void Log(LogLevel level, std::format_string<Args...> fmt,
         const Args&... args) noexcept {

    if (!IsLogEnabled(level)) return;

    detail::VLog(level, fmt.get(), std::make_format_args(args...));
}

template <typename... Args>
void LogDebug(std::format_string<Args...> fmt, const Args&... args) noexcept {
    Log(LogLevel::kDebug, fmt, args...);
}

template <typename... Args>
void LogInfo(std::format_string<Args...> fmt, const Args&... args) noexcept {
    Log(LogLevel::kInfo, fmt, args...);
}

template <typename... Args>
void LogNotice(std::format_string<Args...> fmt, const Args&... args) noexcept {
    Log(LogLevel::kNotice, fmt, args...);
}

template <typename... Args>
void LogWarning(std::format_string<Args...> fmt, const Args&... args) noexcept {
    Log(LogLevel::kWarning, fmt, args...);
}

template <typename... Args>
void LogError(std::format_string<Args...> fmt, const Args&... args) noexcept {
    Log(LogLevel::kErr, fmt, args...);
}

}  // namespace daikon

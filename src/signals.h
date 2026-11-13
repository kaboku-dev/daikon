#pragma once

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "unique_fd.h"

namespace daikon {

// What a handled signal asks the server to do.
enum class SignalEvent : std::uint8_t { kShutdown };

// Routes the signals in kHandled to fd() instead of their default action, so
// the server can notice them in its event loop and react gracefully, and
// ignores the signals in kIgnored.
//
// Create it in the main thread before any other thread starts: the handled
// signals are blocked only in the calling thread, and new threads inherit that
// mask.
//
// Signal handling is process-wide, so at most one instance may be alive.
class SignalManager {
public:
    // Throws std::logic_error if another instance is alive and
    // std::system_error on failure.
    SignalManager();
    // Discards pending handled signals, then restores the previous signal mask
    // and dispositions.
    ~SignalManager();

    SignalManager(const SignalManager&) = delete;
    SignalManager(SignalManager&&) = delete;
    SignalManager& operator=(const SignalManager&) = delete;
    SignalManager& operator=(SignalManager&&) = delete;

    // Readable while a handled signal is pending.
    [[nodiscard]]
    int fd() const noexcept {
        return fd_.get();
    }

    // Consumes one pending handled signal and returns its event, or
    // std::nullopt if none is pending. Throws std::system_error on failure.
    [[nodiscard]]
    std::optional<SignalEvent> ReadEvent();

private:
    struct HandledSignal {
        int signal;
        SignalEvent event;
    };

    // Add new handled signals here.
    static constexpr std::array kHandled = {
        HandledSignal{.signal = SIGINT, .event = SignalEvent::kShutdown},
        HandledSignal{.signal = SIGTERM, .event = SignalEvent::kShutdown},
    };

    // SIGPIPE: writing to a socket whose peer has gone fails with EPIPE instead
    // of killing the process.
    // SIGHUP: its default action would kill the server when the terminal closes
    static constexpr std::array kIgnored = {SIGPIPE, SIGHUP};

    [[nodiscard]]
    static sigset_t MakeHandledSet() noexcept;

    void Install();
    void RestoreDispositions(std::size_t count) noexcept;

    sigset_t previous_mask_{};
    std::array<struct sigaction, kIgnored.size()> previous_actions_{};
    UniqueFd fd_;
};

}  // namespace daikon

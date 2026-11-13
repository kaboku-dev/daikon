#include "signals.h"

#include <sys/signalfd.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <optional>
#include <stdexcept>

#include "errors.h"
#include "unique_fd.h"

namespace daikon {
namespace {

// The guard against a second instance has to be process-wide.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_instance_alive{false};

}  // namespace

SignalManager::SignalManager() {
    if (g_instance_alive.exchange(true)) {
        throw std::logic_error("only one SignalManager may be alive at a time");
    }

    try {
        Install();
    } catch (...) {
        g_instance_alive.store(false);
        throw;
    }
}

SignalManager::~SignalManager() {
    // Unblocking a still-pending handled signal would deliver it with its
    // default action and could kill the process during a graceful shutdown.
    // Consume it first: whatever it asked for does not matter anymore.
    signalfd_siginfo info{};
    while (read(fd_.get(), &info, sizeof(info)) == sizeof(info)) {
    }

    pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
    RestoreDispositions(kIgnored.size());
    g_instance_alive.store(false);
}

std::optional<SignalEvent> SignalManager::ReadEvent() {
    signalfd_siginfo info{};
    if (read(fd_.get(), &info, sizeof(info)) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::nullopt;
        }
        ThrowSystemError(errno, "read(signalfd)");
    }

    // A signalfd only reports signals from its own mask, i.e. from kHandled.
    for (const HandledSignal& handled : kHandled) {
        if (static_cast<int>(info.ssi_signo) == handled.signal) {
            return handled.event;
        }
    }

    throw std::logic_error("signalfd reported a signal that is not handled");
}

sigset_t SignalManager::MakeHandledSet() noexcept {
    sigset_t set{};
    sigemptyset(&set);
    for (const HandledSignal& handled : kHandled) {
        sigaddset(&set, handled.signal);
    }
    return set;
}

// On failure, undoes what it has already done; fd_ is closed by its own
// destructor.
void SignalManager::Install() {
    const sigset_t handled = MakeHandledSet();

    fd_ = UniqueFd{signalfd(-1, &handled, SFD_NONBLOCK | SFD_CLOEXEC)};
    if (!fd_.valid()) {
        ThrowSystemError(errno, "signalfd");
    }

    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    for (std::size_t i = 0; i < kIgnored.size(); ++i) {
        if (sigaction(kIgnored.at(i), &ignore, &previous_actions_.at(i)) != 0) {
            const int error = errno;
            RestoreDispositions(i);
            ThrowSystemError(error, "sigaction");
        }
    }

    const int error = pthread_sigmask(SIG_BLOCK, &handled, &previous_mask_);
    if (error) {
        RestoreDispositions(kIgnored.size());
        ThrowSystemError(error, "pthread_sigmask");
    }
}

// Restores the dispositions of the first `count` ignored signals.
void SignalManager::RestoreDispositions(std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        sigaction(kIgnored.at(i), &previous_actions_.at(i), nullptr);
    }
}

}  // namespace daikon

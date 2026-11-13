#include "signals.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>

#include "config.h"
#include "server.h"
#include "unique_fd.h"

namespace daikon {
namespace {

// kill() rather than raise(): raise() targets the calling thread, while real
// signals (Ctrl+C, systemd, docker stop) target the process. For a signal
// blocked in every thread, it is pending by the time kill() returns, so no
// test has to wait for it.
void SendToSelf(int signal_number) {
    ASSERT_EQ(kill(getpid(), signal_number), 0);
}

[[nodiscard]]
bool IsReadable(int fd) {
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    return poll(&pfd, 1, 0) == 1;
}

[[nodiscard]]
bool IsBlocked(int signal_number) {
    sigset_t mask{};
    pthread_sigmask(0, nullptr, &mask);
    return sigismember(&mask, signal_number) == 1;
}

using SignalHandler = void (*)(int);

[[nodiscard]]
SignalHandler Disposition(int signal_number) {
    struct sigaction action{};
    sigaction(signal_number, nullptr, &action);
    return action.sa_handler;
}

void NoOpHandler(int /*signal_number*/) {}

// Installs a handler for the scope's lifetime, so that a test can tell a
// restored previous disposition from a reset to the default.
class ScopedHandler {
public:
    ScopedHandler(int signal_number, SignalHandler handler)
        : signal_number_{signal_number} {
        struct sigaction action{};
        action.sa_handler = handler;
        sigemptyset(&action.sa_mask);
        sigaction(signal_number_, &action, &previous_);
    }
    ~ScopedHandler() { sigaction(signal_number_, &previous_, nullptr); }

    ScopedHandler(const ScopedHandler&) = delete;
    ScopedHandler(ScopedHandler&&) = delete;
    ScopedHandler& operator=(const ScopedHandler&) = delete;
    ScopedHandler& operator=(ScopedHandler&&) = delete;

private:
    int signal_number_;
    struct sigaction previous_{};
};

// Blocks a signal in the calling thread for the scope's lifetime.
class ScopedBlock {
public:
    explicit ScopedBlock(int signal_number) {
        sigemptyset(&set_);
        sigaddset(&set_, signal_number);
        pthread_sigmask(SIG_BLOCK, &set_, nullptr);
    }
    ~ScopedBlock() { pthread_sigmask(SIG_UNBLOCK, &set_, nullptr); }

    ScopedBlock(const ScopedBlock&) = delete;
    ScopedBlock(ScopedBlock&&) = delete;
    ScopedBlock& operator=(const ScopedBlock&) = delete;
    ScopedBlock& operator=(ScopedBlock&&) = delete;

private:
    sigset_t set_{};
};

// Runs once for each handled signal.
class HandledSignalTest : public testing::TestWithParam<int> {};

INSTANTIATE_TEST_SUITE_P(
    ShutdownSignals, HandledSignalTest, testing::Values(SIGINT, SIGTERM),
    [](const testing::TestParamInfo<int>& info) -> std::string {
        return info.param == SIGINT ? "Sigint" : "Sigterm";
    });

TEST_P(HandledSignalTest, ProducesShutdownEvent) {
    SignalManager manager;
    ASSERT_FALSE(IsReadable(manager.fd()));

    ASSERT_NO_FATAL_FAILURE(SendToSelf(GetParam()));

    EXPECT_TRUE(IsReadable(manager.fd()));
    EXPECT_EQ(manager.ReadEvent(), SignalEvent::kShutdown);
}

TEST_P(HandledSignalTest, ReadEventConsumesTheSignal) {
    SignalManager manager;
    ASSERT_NO_FATAL_FAILURE(SendToSelf(GetParam()));
    ASSERT_EQ(manager.ReadEvent(), SignalEvent::kShutdown);

    EXPECT_FALSE(IsReadable(manager.fd()));
    EXPECT_EQ(manager.ReadEvent(), std::nullopt);
}

TEST_P(HandledSignalTest, StopsServer) {
    const SignalManager manager;
    Config config;
    config.bind_addresses = {"127.0.0.1"};
    config.port = 0;
    Server server{config};
    // Created after SignalManager, so the thread inherits the blocked mask.
    auto running =
        std::async(std::launch::async, [&] { server.Run(manager.fd()); });

    ASSERT_NO_FATAL_FAILURE(SendToSelf(GetParam()));

    running.get();
}

TEST_P(HandledSignalTest, IsBlockedOnlyWhileAlive) {
    ASSERT_FALSE(IsBlocked(GetParam()));
    {
        const SignalManager manager;
        EXPECT_TRUE(IsBlocked(GetParam()));
    }
    EXPECT_FALSE(IsBlocked(GetParam()));
}

TEST_P(HandledSignalTest, PendingSignalIsDiscardedOnDestruction) {
    {
        const SignalManager manager;
        ASSERT_NO_FATAL_FAILURE(SendToSelf(GetParam()));
    }
    // Reaching this point means the pending signal was not delivered when the
    // mask was restored; otherwise it would have killed the process.
    EXPECT_FALSE(IsBlocked(GetParam()));
}

TEST(SignalManagerTest, ReadEventReturnsNulloptWhenNothingIsPending) {
    SignalManager manager;

    EXPECT_FALSE(IsReadable(manager.fd()));
    EXPECT_EQ(manager.ReadEvent(), std::nullopt);
}

TEST(SignalManagerTest, RestoresPreviousMaskOnDestruction) {
    const ScopedBlock already_blocked{SIGINT};
    {
        const SignalManager manager;
        EXPECT_TRUE(IsBlocked(SIGINT));
        EXPECT_TRUE(IsBlocked(SIGTERM));
    }
    EXPECT_TRUE(IsBlocked(SIGINT));
    EXPECT_FALSE(IsBlocked(SIGTERM));
}

TEST(SignalManagerTest, WriteToClosedPipeFailsWithEpipe) {
    const SignalManager manager;
    std::array<int, 2> fds{};
    ASSERT_EQ(pipe2(fds.data(), O_CLOEXEC), 0);
    UniqueFd read_end{fds[0]};
    const UniqueFd write_end{fds[1]};
    read_end = UniqueFd{};

    const ssize_t written = write(write_end.get(), "x", 1);
    const int error = errno;

    EXPECT_EQ(written, -1);
    EXPECT_EQ(error, EPIPE);
}

TEST(SignalManagerTest, SighupIsIgnored) {
    const SignalManager manager;

    ASSERT_NO_FATAL_FAILURE(SendToSelf(SIGHUP));

    // Reaching this point means SIGHUP did not kill the process. It does not
    // reach the fd either: it is ignored, not handled.
    EXPECT_FALSE(IsReadable(manager.fd()));
}

TEST(SignalManagerTest, RestoresPreviousDispositionsOnDestruction) {
    const ScopedHandler sigpipe{SIGPIPE, NoOpHandler};
    const ScopedHandler sighup{SIGHUP, NoOpHandler};
    {
        const SignalManager manager;
    }
    EXPECT_EQ(Disposition(SIGPIPE), &NoOpHandler);
    EXPECT_EQ(Disposition(SIGHUP), &NoOpHandler);
}

TEST(SignalManagerTest, ThrowsIfAnotherInstanceIsAlive) {
    SignalManager first;

    EXPECT_THROW(SignalManager{}, std::logic_error);

    // The failed attempt must not have disturbed the live instance.
    EXPECT_TRUE(IsBlocked(SIGTERM));
    ASSERT_NO_FATAL_FAILURE(SendToSelf(SIGTERM));
    EXPECT_EQ(first.ReadEvent(), SignalEvent::kShutdown);
}

TEST(SignalManagerTest, CanBeCreatedAgainAfterDestruction) {
    {
        const SignalManager first;
    }

    EXPECT_NO_THROW(SignalManager{});
}

}  // namespace
}  // namespace daikon

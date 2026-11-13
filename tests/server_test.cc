#include "server.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "config.h"
#include "errors.h"
#include "unique_fd.h"

namespace daikon {
namespace {

// Makes a blocking client call fail instead of hanging the test run when the
// server misbehaves. No assertion depends on it.
constexpr timeval kClientTimeout{.tv_sec = 10, .tv_usec = 0};

UniqueFd Connect(const std::string& ip, std::uint16_t port) {
    sockaddr_storage address{};
    socklen_t length = 0;
    sockaddr_in ipv4{};
    sockaddr_in6 ipv6{};

    if (inet_pton(AF_INET, ip.c_str(), &ipv4.sin_addr) == 1) {
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(port);
        std::memcpy(&address, &ipv4, sizeof(ipv4));
        length = sizeof(ipv4);
    } else if (inet_pton(AF_INET6, ip.c_str(), &ipv6.sin6_addr) == 1) {
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(port);
        std::memcpy(&address, &ipv6, sizeof(ipv6));
        length = sizeof(ipv6);
    } else {
        throw std::invalid_argument("not an IP address: " + ip);
    }

    UniqueFd fd{socket(address.ss_family, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!fd.valid()) {
        ThrowSystemError(errno, "socket");
    }
    if (setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &kClientTimeout,
                   sizeof(kClientTimeout)) != 0 ||
        setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &kClientTimeout,
                   sizeof(kClientTimeout)) != 0) {
        ThrowSystemError(errno, "setsockopt");
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&address),
                length) != 0) {
        ThrowSystemError(errno, "connect");
    }
    return fd;
}

UniqueFd Connect(std::uint16_t port) { return Connect("127.0.0.1", port); }

// Returns the error that connecting failed with, or no error on success.
std::error_code ConnectError(const std::string& ip, std::uint16_t port) {
    try {
        Connect(ip, port);
    } catch (const std::system_error& e) {
        return e.code();
    }
    return {};
}

void SendAll(int fd, std::string_view data) {
    while (!data.empty()) {
        const ssize_t sent = send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (sent < 0) {
            ThrowSystemError(errno, "send");
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

std::string RecvExactly(int fd, std::size_t size) {
    std::string data(size, '\0');
    std::size_t received = 0;
    while (received < size) {
        const std::span<char> rest = std::span{data}.subspan(received);
        const ssize_t n = recv(fd, rest.data(), rest.size(), 0);
        if (n < 0) {
            ThrowSystemError(errno, "recv");
        }
        if (n == 0) {
            data.resize(received);
            break;  // EOF: the caller's comparison reports the shortfall.
        }
        received += static_cast<std::size_t>(n);
    }
    return data;
}

[[nodiscard]]
bool IsClosedByPeer(int fd) {
    char byte = 0;
    return recv(fd, &byte, 1, MSG_PEEK) == 0;
}

// Sends without reading the echo until the server is blocked on the client.
// One EAGAIN may only mean the server thread lags behind; a long run of them
// makes it very likely.
void SaturateConnection(int fd) {
    constexpr int kConsecutiveEagains = 1000;
    const std::string chunk(std::size_t{64} * 1024, 'x');

    for (int eagains = 0; eagains < kConsecutiveEagains;) {
        const ssize_t sent =
            send(fd, chunk.data(), chunk.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent >= 0) {
            eagains = 0;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ++eagains;
            std::this_thread::yield();  // Let a lagging server thread run.
        } else {
            ThrowSystemError(errno, "send");
        }
    }
}

// Makes close() send RST instead of the orderly FIN, like a crashed client.
void CloseWithReset(UniqueFd fd) {
    const linger reset{.l_onoff = 1, .l_linger = 0};
    if (setsockopt(fd.get(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) !=
        0) {
        ThrowSystemError(errno, "setsockopt(SO_LINGER)");
    }
}

std::string MakePayload(std::size_t size) {
    std::string payload(size, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        payload[i] = static_cast<char>('a' + (i % 26));
    }
    return payload;
}

void ExpectEchoes(const std::string& ip, std::uint16_t port) {
    const UniqueFd client = Connect(ip, port);
    SendAll(client.get(), "ping");
    EXPECT_EQ(RecvExactly(client.get(), 4), "ping") << "via " << ip;
}

Config MakeConfig(std::vector<std::string> bind_addresses) {
    Config config;
    config.bind_addresses = std::move(bind_addresses);
    config.port = 0;
    return config;
}

Config LoopbackConfig() { return MakeConfig({"127.0.0.1"}); }

bool HasIpv6Loopback() {
    const UniqueFd fd{socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    return fd.valid() &&
           // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
           bind(fd.get(), reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == 0;
}

// Runs a Server on a background thread. It is stopped through an eventfd,
// which plays the role the signalfd plays in production.
class RunningServer {
public:
    explicit RunningServer(const Config& config) : server_{config} {
        if (!stop_.valid()) {
            ThrowSystemError(errno, "eventfd");
        }
        running_ = std::async(std::launch::async,
                              [this] { server_.Run(stop_.get()); });
    }

    ~RunningServer() {
        if (running_.valid()) {
            RequestStop();
            running_.wait();
        }
    }

    RunningServer(const RunningServer&) = delete;
    RunningServer(RunningServer&&) = delete;
    RunningServer& operator=(const RunningServer&) = delete;
    RunningServer& operator=(RunningServer&&) = delete;

    // Stops the server and waits for Run() to return, rethrowing anything it
    // threw.
    void Stop() {
        if (running_.valid()) {
            RequestStop();
            running_.get();
        }
    }

    [[nodiscard]]
    std::uint16_t port() const {
        return server_.port();
    }

private:
    void RequestStop() const noexcept {
        const std::uint64_t one = 1;
        // Writing 8 bytes to an eventfd only fails on counter overflow.
        static_cast<void>(write(stop_.get(), &one, sizeof(one)));
    }

    UniqueFd stop_{eventfd(0, EFD_CLOEXEC)};
    Server server_;
    std::future<void> running_;
};

class ServerTest : public testing::Test {
protected:
    void TearDown() override { server_.Stop(); }

    void Stop() { server_.Stop(); }

    [[nodiscard]]
    std::uint16_t port() const {
        return server_.port();
    }

private:
    RunningServer server_{LoopbackConfig()};
};

TEST_F(ServerTest, BindsToAFreePortWhenConfiguredWithZero) {
    EXPECT_NE(port(), 0);
}

TEST_F(ServerTest, EchoesMessage) {
    const UniqueFd client = Connect(port());

    SendAll(client.get(), "hello");

    EXPECT_EQ(RecvExactly(client.get(), 5), "hello");
}

TEST_F(ServerTest, EchoesMessagesInOrderOnOneConnection) {
    const UniqueFd client = Connect(port());

    for (const std::string_view message : {"first", "second", "third"}) {
        SendAll(client.get(), message);
        EXPECT_EQ(RecvExactly(client.get(), message.size()), message);
    }
}

TEST_F(ServerTest, EchoesPayloadLargerThanBuffers) {
    const UniqueFd client = Connect(port());
    const std::string payload = MakePayload(std::size_t{4} * 1024 * 1024);

    // Sending and receiving at the same time: sending everything first would
    // deadlock once all buffers between client and server are full.
    auto sending =
        std::async(std::launch::async, [&] { SendAll(client.get(), payload); });
    const std::string echoed = RecvExactly(client.get(), payload.size());
    sending.get();

    EXPECT_EQ(echoed, payload);
}

TEST_F(ServerTest, EchoesBinaryData) {
    const UniqueFd client = Connect(port());
    const std::string data{"\0\r\n\xff", 4};

    SendAll(client.get(), data);

    EXPECT_EQ(RecvExactly(client.get(), data.size()), data);
}

TEST_F(ServerTest, ServesQueuedClientAfterCurrentOneDisconnects) {
    UniqueFd first = Connect(port());
    const UniqueFd second = Connect(port());
    SendAll(second.get(), "second");

    SendAll(first.get(), "first");
    EXPECT_EQ(RecvExactly(first.get(), 5), "first");
    first = UniqueFd{};

    EXPECT_EQ(RecvExactly(second.get(), 6), "second");
}

TEST_F(ServerTest, KeepsServingAfterClientResetsConnection) {
    UniqueFd first = Connect(port());
    SendAll(first.get(), "ping");
    ASSERT_EQ(RecvExactly(first.get(), 4), "ping");
    SendAll(first.get(), "unanswered");
    CloseWithReset(std::move(first));

    const UniqueFd second = Connect(port());
    SendAll(second.get(), "still alive");
    EXPECT_EQ(RecvExactly(second.get(), 11), "still alive");
}

// Closing with unread data sends RST, so the server's pending write fails.
TEST_F(ServerTest, KeepsServingAfterClientClosesWithoutReading) {
    UniqueFd first = Connect(port());
    SendAll(first.get(), "ping");
    ASSERT_EQ(RecvExactly(first.get(), 4), "ping");
    SaturateConnection(first.get());
    first = UniqueFd{};

    const UniqueFd second = Connect(port());
    SendAll(second.get(), "still alive");
    EXPECT_EQ(RecvExactly(second.get(), 11), "still alive");
}

TEST_F(ServerTest, StopsWhenIdle) { Stop(); }

TEST_F(ServerTest, StopClosesCurrentConnection) {
    const UniqueFd client = Connect(port());
    SendAll(client.get(), "ping");
    ASSERT_EQ(RecvExactly(client.get(), 4), "ping");

    Stop();

    EXPECT_TRUE(IsClosedByPeer(client.get()));
}

// Guards against waiting for the client inside a blocking send(), which a
// stop request could not interrupt.
TEST_F(ServerTest, StopsWhileClientIsNotReading) {
    const UniqueFd client = Connect(port());
    SendAll(client.get(), "ping");
    ASSERT_EQ(RecvExactly(client.get(), 4), "ping");
    SaturateConnection(client.get());

    Stop();
}

TEST(ServerSetupTest, ThrowsWhenPortIsInUse) {
    const Server first{LoopbackConfig()};
    Config config = LoopbackConfig();
    config.port = first.port();

    EXPECT_THROW(Server{config}, std::system_error);
}

TEST(ServerSetupTest, ThrowsOnInvalidBindAddress) {
    EXPECT_THROW(Server{MakeConfig({"localhost"})}, std::invalid_argument);
}

TEST(ServerBindTest, ListensOnlyOnGivenAddress) {
    RunningServer server{MakeConfig({"127.0.0.1"})};

    ExpectEchoes("127.0.0.1", server.port());
    // All of 127.0.0.0/8 is loopback on Linux, but nothing listens on .2.
    EXPECT_EQ(ConnectError("127.0.0.2", server.port()),
              std::errc::connection_refused);

    server.Stop();
}

TEST(ServerBindTest, ListensOnEveryGivenAddressWithOnePort) {
    RunningServer server{MakeConfig({"127.0.0.1", "127.0.0.2"})};

    ExpectEchoes("127.0.0.1", server.port());
    ExpectEchoes("127.0.0.2", server.port());

    server.Stop();
}

TEST(ServerBindTest, ListensOnIpv6Address) {
    if (!HasIpv6Loopback()) {
        GTEST_SKIP() << "IPv6 loopback is not available";
    }
    RunningServer server{MakeConfig({"::1"})};

    ExpectEchoes("::1", server.port());
    EXPECT_EQ(ConnectError("127.0.0.1", server.port()),
              std::errc::connection_refused);

    server.Stop();
}

// Without IPV6_V6ONLY the :: socket would also claim the IPv4 port, and
// binding both wildcards would fail with "address in use".
TEST(ServerBindTest, ListensOnIpv4AndIpv6WildcardsByDefault) {
    RunningServer server{MakeConfig({})};

    ExpectEchoes("127.0.0.1", server.port());
    if (HasIpv6Loopback()) {
        ExpectEchoes("::1", server.port());
    }

    server.Stop();
}

}  // namespace
}  // namespace daikon

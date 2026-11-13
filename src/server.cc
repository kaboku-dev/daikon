#include "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "config.h"
#include "errors.h"
#include "logger.h"
#include "unique_fd.h"

namespace daikon {
namespace {

// Matches Redis' tcp-backlog's default.
constexpr int kListenBacklog = 511;

// Matches Redis' PROTO_IOBUF_LEN.
constexpr std::size_t kIOBufferSize = std::size_t{16} * 1024;

// The sockets API predates void* and takes every address family through
// the generic sockaddr type, so this cast is how it is meant to be used.
template <typename SockaddrType>
sockaddr* AsSockaddr(SockaddrType* addr) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<sockaddr*>(addr);
}

// sockaddr_storage is large and aligned enough for any address family. The
// family-specific views are copied in and out with memcpy, which, unlike a
// pointer cast, does not break C++ aliasing rules.
struct SocketAddress {
    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);
};

// Returns std::nullopt if `ip` is not a numeric IPv4 or IPv6 address.
// Link-local IPv6 addresses are not supported: inet_pton rejects the "%zone"
// suffix, so sin6_scope_id stays 0 and the address cannot be bound.
std::optional<SocketAddress> MakeSocketAddress(const std::string& ip,
                                               std::uint16_t port) {
    SocketAddress result;

    sockaddr_in ipv4{};
    if (inet_pton(AF_INET, ip.c_str(), &ipv4.sin_addr) == 1) {
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(port);
        std::memcpy(&result.storage, &ipv4, sizeof(ipv4));
        result.length = sizeof(ipv4);
        return result;
    }

    sockaddr_in6 ipv6{};
    if (inet_pton(AF_INET6, ip.c_str(), &ipv6.sin6_addr) == 1) {
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(port);
        std::memcpy(&result.storage, &ipv6, sizeof(ipv6));
        result.length = sizeof(ipv6);
        return result;
    }

    return std::nullopt;
}

std::string FormatAddress(const SocketAddress& address) {
    std::array<char, INET6_ADDRSTRLEN> ip{};

    if (address.storage.ss_family == AF_INET) {
        sockaddr_in ipv4{};
        std::memcpy(&ipv4, &address.storage, sizeof(ipv4));
        inet_ntop(AF_INET, &ipv4.sin_addr, ip.data(), ip.size());
        return std::format("{}:{}", ip.data(), ntohs(ipv4.sin_port));
    }

    sockaddr_in6 ipv6{};
    std::memcpy(&ipv6, &address.storage, sizeof(ipv6));
    inet_ntop(AF_INET6, &ipv6.sin6_addr, ip.data(), ip.size());
    return std::format("[{}]:{}", ip.data(), ntohs(ipv6.sin6_port));
}

SocketAddress LocalAddress(int fd) {
    SocketAddress address;
    if (getsockname(fd, AsSockaddr(&address.storage), &address.length) != 0) {
        ThrowSystemError(errno, "getsockname");
    }
    return address;
}

std::uint16_t LocalPort(int fd) {
    const SocketAddress address = LocalAddress(fd);

    if (address.storage.ss_family == AF_INET) {
        sockaddr_in ipv4{};
        std::memcpy(&ipv4, &address.storage, sizeof(ipv4));
        return ntohs(ipv4.sin_port);
    }

    sockaddr_in6 ipv6{};
    std::memcpy(&ipv6, &address.storage, sizeof(ipv6));
    return ntohs(ipv6.sin6_port);
}

// A kernel built without IPv6 rejects AF_INET6 sockets with EAFNOSUPPORT.
bool IsIpv6Supported() {
    const UniqueFd probe{socket(AF_INET6, SOCK_STREAM, 0)};
    if (probe.valid()) {
        return true;
    }
    if (errno == EAFNOSUPPORT) {
        return false;
    }
    ThrowSystemError(errno, "socket(AF_INET6)");
}

std::vector<std::string> ResolveBindAddresses(const Config& config) {
    if (!config.bind_addresses.empty()) {
        return config.bind_addresses;
    }

    std::vector<std::string> addresses{"0.0.0.0"};
    if (IsIpv6Supported()) {
        addresses.emplace_back("::");
    }
    return addresses;
}

UniqueFd Listen(const std::string& ip, std::uint16_t port) {
    auto address = MakeSocketAddress(ip, port);
    if (!address) {
        throw std::invalid_argument(
            std::format("invalid bind address '{}'", ip));
    }

    const int family = address->storage.ss_family;

    // Non-blocking because a connection reported by poll() can be gone by the
    // time accept() runs (see accept(2)); a blocking accept() would then wait
    // for the next client and stall shutdown.
    UniqueFd fd{socket(family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    if (!fd.valid()) {
        ThrowSystemError(errno, "socket");
    }

    const int enable = 1;
    if (setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) != 0) {
        ThrowSystemError(errno, "setsockopt(SO_REUSEADDR)");
    }

    // By default, a Linux IPv6 socket bound to :: also accepts IPv4
    // connections, so it would clash with a 0.0.0.0 socket on the same port.
    // IPv6-only keeps each socket to exactly the address it was given.
    if (family == AF_INET6 && setsockopt(fd.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                                         &enable, sizeof(enable)) != 0) {
        ThrowSystemError(errno, "setsockopt(IPV6_V6ONLY)");
    }

    if (bind(fd.get(), AsSockaddr(&address->storage), address->length) != 0) {
        const int error = errno;
        ThrowSystemError(error,
                         std::format("bind to {}", FormatAddress(*address)));
    }

    if (listen(fd.get(), kListenBacklog) != 0) {
        ThrowSystemError(errno, "listen");
    }

    return fd;
}

// Waits in poll() with EINTR retries. EINTR happens when the process is stopped
// and resumed (Ctrl+Z, a debugger).
void Poll(std::span<pollfd> fds) {
    while (poll(fds.data(), fds.size(), -1) < 0) {
        if (errno != EINTR) {
            ThrowSystemError(errno, "poll");
        }
    }
}

enum class WaitResult : std::uint8_t { kReady, kStop };

// Blocks until `fd` is ready for `events` (POLLIN or POLLOUT) or stop_fd
// becomes readable. decltype instead of short: google-runtime-int rejects bare
// short.
WaitResult WaitFor(int fd, decltype(pollfd::events) events, int stop_fd) {
    std::array<pollfd, 2> fds{{
        {.fd = stop_fd, .events = POLLIN, .revents = 0},
        {.fd = fd, .events = events, .revents = 0},
    }};

    Poll(fds);

    // The stop check comes first so a busy client cannot delay shutdown.
    // Any revents on `fd` means the next call on it will not block: it returns
    // the data, the EOF, or the error.
    return fds[0].revents != 0 ? WaitResult::kStop : WaitResult::kReady;
}

// Returns a listening socket with a connection waiting to be accepted, or
// std::nullopt once stop_fd becomes readable.
std::optional<int> WaitForConnection(std::span<const UniqueFd> listen_fds,
                                     int stop_fd) {
    std::vector<pollfd> fds;
    fds.push_back({.fd = stop_fd, .events = POLLIN, .revents = 0});
    for (const UniqueFd& listen_fd : listen_fds) {
        fds.push_back({.fd = listen_fd.get(), .events = POLLIN, .revents = 0});
    }

    Poll(fds);

    if (fds[0].revents != 0) {
        return std::nullopt;
    }
    for (const pollfd& pfd : std::span{fds}.subspan(1)) {
        if (pfd.revents != 0) {
            return pfd.fd;
        }
    }
    return std::nullopt;  // Unreachable: poll() only returns when one is ready.
}

// Returns false if the client is gone or a stop was requested.
bool SendAll(int fd, std::span<const char> data, int stop_fd) {
    while (!data.empty()) {
        if (WaitFor(fd, POLLOUT, stop_fd) == WaitResult::kStop) {
            return false;
        }

        // POLLOUT guarantees room for some bytes, not all, so a blocking send()
        // could still stall shutdown; MSG_DONTWAIT sends what fits.
        // MSG_NOSIGNAL avoids SIGPIPE even without a SignalManager (as in the
        // tests).
        const ssize_t sent =
            send(fd, data.data(), data.size(), MSG_DONTWAIT | MSG_NOSIGNAL);

        if (sent < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LogInfo("Client send failed: {}",
                    std::system_category().message(errno));
            return false;
        }

        data = data.subspan(static_cast<std::size_t>(sent));
    }
    return true;
}

// Echoes everything back until the client disconnects, an I/O error occurs,
// or a stop is requested.
void ServeClient(int fd, int stop_fd) {
    std::array<char, kIOBufferSize> buffer{};

    while (WaitFor(fd, POLLIN, stop_fd) == WaitResult::kReady) {
        // POLLIN is a readiness hint, not a guarantee; MSG_DONTWAIT keeps a
        // spurious wakeup from blocking recv() and stalling shutdown.
        const ssize_t received =
            recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);

        if (received == 0) {
            return;
        }
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LogInfo("Client recv failed: {}",
                    std::system_category().message(errno));
            return;
        }

        const std::span<const char> data{buffer.data(),
                                         static_cast<std::size_t>(received)};
        if (!SendAll(fd, data, stop_fd)) {
            return;
        }
    }
}

}  // namespace

Server::Server(const Config& config) : port_(config.port) {
    for (const std::string& ip : ResolveBindAddresses(config)) {
        listen_fds_.push_back(Listen(ip, port_));
        // With port 0, the first socket gets a free port and the others
        // reuse it, so that port() is the same on every address.
        if (port_ == 0) {
            port_ = LocalPort(listen_fds_.back().get());
        }
    }
}

void Server::Run(int stop_fd) {
    for (const UniqueFd& listen_fd : listen_fds_) {
        LogNotice("Listening on {}",
                  FormatAddress(LocalAddress(listen_fd.get())));
    }

    // ServeClient() does not consume stop_fd, so after a stop request it
    // stays readable and this loop ends too.
    while (const auto listen_fd = WaitForConnection(listen_fds_, stop_fd)) {
        SocketAddress peer;
        // The client I/O calls already pass MSG_DONTWAIT; the fd flag is a
        // safety net for a future call that forgets it.
        const UniqueFd client{accept4(*listen_fd, AsSockaddr(&peer.storage),
                                      &peer.length,
                                      SOCK_CLOEXEC | SOCK_NONBLOCK)};
        if (!client.valid()) {
            const int error = errno;
            // The connection vanished between poll() and accept().
            if (error == EAGAIN || error == EWOULDBLOCK) {
                continue;
            }
            // Other failures concern a single connection (e.g. it was reset
            // while queued) or are temporary (e.g. out of descriptors), so
            // the server keeps going.
            LogWarning("accept failed: {}",
                       std::system_category().message(error));
            continue;
        }

        const std::string peer_name = FormatAddress(peer);
        LogInfo("Client {} connected", peer_name);
        ServeClient(client.get(), stop_fd);
        LogInfo("Client {} disconnected", peer_name);
    }

    LogNotice("Shutdown requested, closing the server");
}

}  // namespace daikon

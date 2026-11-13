#pragma once

#include <cstdint>
#include <vector>

#include "config.h"
#include "unique_fd.h"

namespace daikon {

// A TCP echo server that serves one client at a time. Further clients wait in
// the kernel's accept queue until the current one disconnects.
class Server {
public:
    // Starts listening on every address in config.bind_addresses, all on
    // config.port (0 picks a free port). Clients can connect from then on, even
    // before Run() is called. Throws std::system_error if a socket cannot be
    // set up and std::invalid_argument on an invalid address.
    explicit Server(const Config& config);

    ~Server() = default;
    Server(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(const Server&) = delete;
    Server& operator=(Server&&) = delete;

    // The port actually bound; differs from config.port when that was 0.
    [[nodiscard]]
    std::uint16_t port() const noexcept {
        return port_;
    }

    // Serves clients until stop_fd becomes readable, then closes the current
    // connection and returns. Throws std::system_error on unrecoverable errors.
    void Run(int stop_fd);

private:
    std::vector<UniqueFd> listen_fds_;
    std::uint16_t port_ = 0;
};

}  // namespace daikon

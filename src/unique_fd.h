#pragma once

#include <unistd.h>

#include <utility>

namespace daikon {

// Sole owner of a POSIX file descriptor; closes it on destruction.
class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    ~UniqueFd() { Close(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            Close();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]]
    int get() const noexcept {
        return fd_;
    }

    [[nodiscard]]
    bool valid() const noexcept {
        return fd_ >= 0;
    }

private:
    void Close() noexcept {
        if (valid()) {
            // Not retried on EINTR: most systems release the descriptor anyway,
            // and a retry could close one that another thread has since been
            // handed.
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};

}  // namespace daikon

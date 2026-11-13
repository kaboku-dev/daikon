#pragma once

#include <string>
#include <string_view>
#include <system_error>

namespace daikon {

// Reports a failed system call. The error code is a parameter rather than read
// from errno because some calls (pthread_*) return it instead, and because
// errno must be captured before any other work that might overwrite it.
[[noreturn]]
inline void ThrowSystemError(int error, std::string_view what) {
    throw std::system_error(error, std::system_category(), std::string{what});
}

}  // namespace daikon

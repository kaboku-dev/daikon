#include <cstddef>
#include <cstdlib>
#include <exception>
#include <span>
#include <string_view>
#include <vector>

#include "config.h"
#include "logger.h"
#include "server.h"
#include "signals.h"

namespace daikon {
namespace {

int Run(int argc, char** argv) {
    const std::span<char*> raw{argv, static_cast<std::size_t>(argc)};
    const std::vector<std::string_view> args{raw.begin(), raw.end()};

    const auto config = ParseArgs(args);
    if (!config) {
        return EXIT_SUCCESS;  // --help / --version
    }

    SetLogLevel(config->log_level);

    // Must precede any thread creation; see SignalManager.
    const SignalManager signal_manager;

    Server server{*config};
    // All handled signals mean shutdown for now, so the signal fd is stop fd.
    server.Run(signal_manager.fd());

    return EXIT_SUCCESS;
}

}  // namespace
}  // namespace daikon

int main(int argc, char** argv) {
    try {
        return daikon::Run(argc, argv);
    } catch (const std::exception& e) {
        daikon::LogError("Fatal: {}", e.what());
    } catch (...) {
        daikon::LogError("Fatal: unknown exception");
    }

    return EXIT_FAILURE;
}

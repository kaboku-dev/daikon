#include <cstdlib>
#include <exception>
#include <iostream>

#include "logger.h"

namespace daikon {
namespace {

int Run() {
    std::cout << "Hello world!\n";

    return EXIT_SUCCESS;
}

}  // namespace
}  // namespace daikon

int main() {
    try {
        return daikon::Run();
    } catch (const std::exception& e) {
        daikon::LogError("Fatal: {}", e.what());
    } catch (...) {
        daikon::LogError("Fatal: unknown exception");
    }

    return EXIT_FAILURE;
}

include(FetchContent)
include(GoogleTest)

# SYSTEM marks GoogleTest's headers as system headers, so our warning flags
# and clang-tidy do not report issues in third-party code.
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM
)
set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)

add_executable(daikon_tests
    smoke_test.cc
)
target_link_libraries(daikon_tests
    PRIVATE
        daikon_options
        GTest::gtest_main
)

gtest_discover_tests(daikon_tests)

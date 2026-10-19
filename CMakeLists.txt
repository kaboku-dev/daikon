cmake_minimum_required(VERSION 3.25)
project(daikon VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

option(DAIKON_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_library(daikon_options INTERFACE)
target_compile_features(daikon_options INTERFACE cxx_std_20)
target_compile_options(daikon_options INTERFACE -Wall -Wextra -Wpedantic)

if(DAIKON_WARNINGS_AS_ERRORS)
    target_compile_options(daikon_options INTERFACE -Werror)
endif()

add_subdirectory(src)

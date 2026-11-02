configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/daikon/version.h
    @ONLY
)

add_library(daikon_core STATIC
    logger.cc
    config.cc
)

target_link_libraries(daikon_core PRIVATE daikon_options)
target_include_directories(daikon_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

add_executable(daikon main.cc)
target_link_libraries(daikon PRIVATE daikon_core daikon_options)

add_executable(daikon main.cc logger.cc)
target_link_libraries(daikon PRIVATE daikon_options)

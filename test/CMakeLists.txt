include_directories(${MICROTCP_INCLUDE_DIRS})

add_executable(bandwidth_test bandwidth_test.c)

target_link_libraries(bandwidth_test microtcp)

install(TARGETS bandwidth_test DESTINATION bin)
cmake_minimum_required(VERSION 3.10)
project(blob-kernel C)

set(CMAKE_C_STANDARD 11)

# Core lib
add_library(kernel SHARED
    src/core/core.c
)

target_include_directories(kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Static library
add_library(kernel_static STATIC
    src/core/core.c
)

target_include_directories(kernel_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# test
add_executable(test_kernel
    main.c
)

target_link_libraries(test_kernel kernel_static)


add_custom_target(run_test
    COMMAND test_kernel
    DEPENDS test_kernel
)

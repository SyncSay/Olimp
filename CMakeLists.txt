cmake_minimum_required(VERSION 3.10)
project(blob-kernel C)

# Создаем библиотеку и исполняемый файл
add_library(kernel STATIC src/core/core.c)
add_executable(test_kernel main.c)

# Связываем их вместе
target_link_libraries(test_kernel PRIVATE kernel)

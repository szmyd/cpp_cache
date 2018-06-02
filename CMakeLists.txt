cmake_minimum_required(VERSION 3.10)
project(cpp_cache)

enable_testing()

include("${CMAKE_BINARY_DIR}/conanbuildinfo.cmake")
conan_basic_setup()

add_subdirectory(src)
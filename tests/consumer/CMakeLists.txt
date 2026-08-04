cmake_minimum_required(VERSION 3.0)
project(http_framework_consumer CXX)

find_package(http_framework CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE http_framework::http_framework)

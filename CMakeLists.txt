cmake_minimum_required(VERSION 3.10)
project(MyWebServer)

set(CMAKE_CXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# Ensure proper linking for epoll support
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# 多线程版本（线程池）
add_executable(server main.cpp
        threadpool/threadpool.h
        webserver.h)
target_link_libraries(server Threads::Threads)

# 单线程对照版本（无线程池）
add_executable(server_single main_single.cpp)

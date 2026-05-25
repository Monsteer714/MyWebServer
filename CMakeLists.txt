cmake_minimum_required(VERSION 3.10)
project(MyWebServer)

set(CMAKE_CXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)

# Ensure proper linking for epoll support
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# macOS epoll compatibility via libepoll-shim
if(APPLE)
    set(EPOLL_SHIM_HINTS
        /opt/homebrew
        /usr/local
    )
    find_library(EPOLL_SHIM_LIB epoll-shim
        HINTS ${EPOLL_SHIM_HINTS}
        PATH_SUFFIXES lib
    )
    find_path(EPOLL_SHIM_INCLUDE_DIR sys/epoll.h
        HINTS ${EPOLL_SHIM_HINTS}
        PATH_SUFFIXES include/libepoll-shim
    )
    if(EPOLL_SHIM_LIB AND EPOLL_SHIM_INCLUDE_DIR)
        message(STATUS "Found epoll-shim lib: ${EPOLL_SHIM_LIB}")
        message(STATUS "Found epoll-shim include: ${EPOLL_SHIM_INCLUDE_DIR}")
        include_directories(${EPOLL_SHIM_INCLUDE_DIR})
    else()
        message(WARNING "libepoll-shim not found. Install with: brew install libepoll-shim")
    endif()
endif()

# 多线程版本（线程池）
add_executable(server main.cpp
        config.cpp
        log/log.cpp
        log/async_log.cpp
        timer/timer_lst.cpp
        timer/timer_wheel.cpp
        util/util.cpp
        threadpool/threadpool.h
        webserver.h)

add_executable(http_test http_conn/HttpTest.cpp)

target_link_libraries(server Threads::Threads ${EPOLL_SHIM_LIB})


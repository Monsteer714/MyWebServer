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
        webserver.h
        util/types.h
        middleware/middleware.h
        middleware/middleware_log.cpp)

add_executable(http_test http_conn/HttpTest.cpp)

# ---- MySQL 库 ----
if(APPLE)
    set(MYSQL_HINTS /usr/local/opt/mysql /opt/homebrew/opt/mysql)
    find_library(MYSQL_LIB mysqlclient HINTS ${MYSQL_HINTS} PATH_SUFFIXES lib)
    find_path(MYSQL_INCLUDE mysql/mysql.h HINTS ${MYSQL_HINTS} PATH_SUFFIXES include/mysql)
else()
    find_library(MYSQL_LIB mysqlclient)
    find_path(MYSQL_INCLUDE mysql/mysql.h)
endif()

if(MYSQL_LIB AND MYSQL_INCLUDE)
    include_directories(${MYSQL_INCLUDE})
    set(MYSQL_LINK_LIB ${MYSQL_LIB})
    message(STATUS "MySQL support enabled: ${MYSQL_LIB}")
else()
    set(MYSQL_LINK_LIB "")
    message(WARNING "MySQL not found — auth routes disabled")
endif()


target_link_libraries(server Threads::Threads ${EPOLL_SHIM_LIB} ${MYSQL_LINK_LIB})


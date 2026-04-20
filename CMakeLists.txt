cmake_minimum_required(VERSION 4.1)
project(MyWebServer)

set(CMAKE_CXX_STANDARD 20)

add_executable(MyWebServer main.cpp
        threadpool/threadpool.h
        webserver.h)

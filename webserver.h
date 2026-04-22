//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_WEBSERVER_H
#define MYWEBSERVER_WEBSERVER_H
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <sys/epoll.h>

#include "http_conn/http_conn.h"
#include "threadpool/threadpool.h"

class WebServer {
private:
    Threadpool<http_conn>* thread_pool_ = {};
    int server_fd_ = {-1};
    int epoll_fd_ = {-1};
    size_t max_events_num_ = 1024;

    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void epollAdd(int fd) {
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLONESHOT;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }

    void epollAddServer(int fd) {
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }
public:
    WebServer() {
        thread_pool_ = new Threadpool<http_conn>(8, 10000);
    }

    ~WebServer() {
        delete thread_pool_;
        if (epoll_fd_ >= 0) close(epoll_fd_);
        if (server_fd_ >= 0) close(server_fd_);
    }

    void start() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            perror("socket");
            return;
        }

        // 允许端口快速复用，避免压测重启时 "Address already in use"
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8888);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(server_fd_);
            server_fd_ = -1;
            return;
        }
        if (listen(server_fd_, 1000) < 0) {
            perror("listen");
            close(server_fd_);
            server_fd_ = -1;
            return;
        }

        setNonBlocking(server_fd_);

        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0) {
            perror("epoll_create1");
            return;
        }

        epollAddServer(server_fd_);
    }

    void loop() {
        std::vector<epoll_event> events(max_events_num_);
        while (true) {
            int n = epoll_wait(epoll_fd_, events.data(), max_events_num_, -1);
            if (n < 0) {
                if (errno == EINTR) {
                    continue; // 被信号中断，继续等待
                }
                perror("epoll_wait");
                return;
            }

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                if (fd == server_fd_) { // 新连接
                    while (true) {
                        int client_fd = accept(server_fd_, nullptr, nullptr);
                        if (client_fd < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                break;
                            }
                            perror("accept");
                            break;
                        }

                        setNonBlocking(client_fd);

                        epollAdd(client_fd);
                    }
                } else { // 读写请求
                    auto conn = new http_conn(fd);
                    if (!thread_pool_->append(conn)) {
                        delete conn;
                    }
                }
            }
        }
    }
};
#endif //MYWEBSERVER_WEBSERVER_H

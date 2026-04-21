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
#include <sys/epoll.h>
#include <vector>

#include "http_conn/http_conn.h"
#include "threadpool/threadpool.h"

static const int MAX_EVENTS = 1024;

class WebServer {
private:
    Threadpool<http_conn>* thread_pool_ = {};
    int server_fd_ = -1;
    int epoll_fd_  = -1;

    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    // 把 fd 注册到 epoll，使用 EPOLLONESHOT：
    // 事件触发一次后自动摘除，防止同一 fd 被多个 worker 同时处理。
    void epollAdd(int fd) {
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLONESHOT;
        ev.data.fd = fd;
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
        if (server_fd_ < 0) { perror("socket"); return; }

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(8888);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind"); close(server_fd_); server_fd_ = -1; return;
        }
        if (listen(server_fd_, 128) < 0) {
            perror("listen"); close(server_fd_); server_fd_ = -1; return;
        }

        // server socket 也设非阻塞，方便在 epoll 事件里 accept 直到 EAGAIN
        setNonBlocking(server_fd_);

        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0) { perror("epoll_create1"); return; }

        // 监听 server socket 的"可读"事件（有新连接到来）
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = server_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev);
    }

    void loop() {
        std::vector<epoll_event> events(MAX_EVENTS);

        while (true) {
            // 阻塞等待，直到至少一个 fd 就绪
            int n = epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, -1);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;

                if (fd == server_fd_) {
                    // ── 新连接到来 ─────────────────────────────────────
                    // server socket 非阻塞：循环 accept 直到 EAGAIN，
                    // 一次性处理所有积压连接，不遗漏。
                    while (true) {
                        int client_fd = accept(server_fd_, nullptr, nullptr);
                        if (client_fd < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            perror("accept");
                            break;
                        }
                        setNonBlocking(client_fd);
                        epollAdd(client_fd);  // 等数据到了再通知
                    }
                } else {
                    // ── 某个客户端有数据可读 ───────────────────────────
                    // EPOLLONESHOT 保证此 fd 只触发一次，不会被其他 worker 抢走。
                    // 直接把 fd 交给线程池，worker 在数据已经到达的情况下读，
                    // 非阻塞 read 不会卡住。
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

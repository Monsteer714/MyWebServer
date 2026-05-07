//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_WEBSERVER_H
#define MYWEBSERVER_WEBSERVER_H
#include <cassert>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <sys/epoll.h>

#include "http_conn/http_conn.h"
#include "log/log.h"
#include "threadpool/threadpool.h"
#include "timer/timer_lst.h"


class WebServer {
private:
    //threadpool
    Threadpool<http_conn>* m_thread_pool_ = {};
    int m_max_threads_num_ = {};

    //epoll
    int m_server_fd_ = {-1};
    int m_epoll_fd_ = {-1};
    size_t m_max_events_num_ = 1024;

    //timer
    Util m_util_ = {};
    int m_pipe_fd_[2] = {};

    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void epollAdd(int fd) {
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLONESHOT;
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }

    void epollAddServer(int fd) {
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN;
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }

    void createLog() {
        Log::getInstance()->init("./ServerLog", 0, 2048, 5000000, 800);
    }
public:
    WebServer() {
        m_thread_pool_ = new Threadpool<http_conn>(8, 10000);
    }

    ~WebServer() {
        delete m_thread_pool_;
        if (m_epoll_fd_ >= 0) close(m_epoll_fd_);
        if (m_server_fd_ >= 0) close(m_server_fd_);
    }

    void start() {
        m_server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(m_server_fd_ >= 0);

        // 允许端口快速复用，避免压测重启时 "Address already in use"
        int opt = 1;
        setsockopt(m_server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8888);
        addr.sin_addr.s_addr = INADDR_ANY;
        memset(addr.sin_zero, '\0', sizeof addr.sin_zero);

        int ret = bind(m_server_fd_, (sockaddr*)&addr, sizeof(addr));
        assert(ret >= 0);
        ret = listen(m_server_fd_, 1024);
        assert(ret >= 0);

        setNonBlocking(m_server_fd_);

        m_epoll_fd_ = epoll_create1(0);
        assert(m_epoll_fd_ >= 0);

        http_conn::m_epollfd_ = m_epoll_fd_;

        socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipe_fd_);
        Util::u_epoll_fd_ = m_epoll_fd_;
        Util::u_pipe_fd_ = m_pipe_fd_;

        epollAddServer(m_server_fd_);

        createLog();

        Log::LOG_INFO("Web server started on port %d", 8888);
    }

    void loop() {
        std::vector<epoll_event> events(m_max_events_num_);
        while (true) {
            int n = epoll_wait(m_epoll_fd_, events.data(), m_max_events_num_, -1);
            if (n < 0) {
                if (errno == EINTR) {
                    continue; // 被信号中断，继续等待
                }
                perror("epoll_wait");
                return;
            }

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                if (fd == m_server_fd_) { // 新连接
                    while (true) {
                        int client_fd = accept(m_server_fd_, nullptr, nullptr);
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
                } else if ((fd == m_pipe_fd_[0]) && (events[i].events & EPOLLIN)) {
                    //sig信号处理
                } else {
                    auto conn = new http_conn(fd);
                    if (!m_thread_pool_->append(conn)) {
                        delete conn;
                    }
                }
            }
        }
    }
};
#endif //MYWEBSERVER_WEBSERVER_H

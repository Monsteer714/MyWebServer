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


constexpr int MAX_FD = 65535;
constexpr int TIME_SLOT = 5;

class WebServer {
private:
    //http_conn
    http_conn* m_user_ = {};

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
    client_data* m_user_timer_ = {};

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

    //初始化新的http连接，初始化该连接对应的定时器
    void createConn(int connfd) {
        m_user_[connfd].init(connfd);

        m_user_timer_[connfd].m_sock_fd_ = connfd;
        auto timer = std::make_shared<util_timer>();
        timer->cb_func = cb_func;
        timer->user_data_ = &m_user_timer_[connfd];
        timer->expire_ = time(NULL) + 3 * TIME_SLOT;
        m_user_timer_[connfd].m_timer_ = timer;
        m_util_.m_timer_.add_timer(timer);
    }

    void createLog() {
        Log::getInstance()->init("./ServerLog", 0, 2048, 5000000, 800);
    }

public:
    WebServer() {
        m_thread_pool_ = new Threadpool<http_conn>(8, 10000, 0);
        m_user_ = new http_conn[MAX_FD];
        m_user_timer_ = new client_data[MAX_FD];
    }

    ~WebServer() {
        delete m_thread_pool_;
        delete[] m_user_;
        delete[] m_user_timer_;
        if (m_epoll_fd_ >= 0)
            close(m_epoll_fd_);
        if (m_server_fd_ >= 0)
            close(m_server_fd_);
    }

    void start() {
        //网络编程基础步骤
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
        //


        setNonBlocking(m_server_fd_);

        //创建内核epoll事件表
        m_epoll_fd_ = epoll_create1(0);
        assert(m_epoll_fd_ >= 0);

        http_conn::m_epollfd_ = m_epoll_fd_;

        //定时器相关
        ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipe_fd_);
        assert(ret >= 0);

        Util::u_epoll_fd_ = m_epoll_fd_;
        Util::u_pipe_fd_ = m_pipe_fd_;

        m_util_.init(TIME_SLOT);
        setNonBlocking(m_pipe_fd_[1]);
        m_util_.addfd(m_epoll_fd_, m_pipe_fd_[0], false, 0);
        m_util_.addsig(SIGPIPE, SIG_IGN, false);
        m_util_.addsig(SIGALRM, Util::sig_handler, false);
        m_util_.addsig(SIGTERM, Util::sig_handler, false);
        alarm(TIME_SLOT);

        epollAddServer(m_server_fd_);

        createLog();

        Log::LOG_INFO("Web server started on port %d", 8888);
    }

    void adjustTimer(int fd) {
        auto timer = m_user_timer_[fd].m_timer_;
        timer->expire_ = time(NULL) + 3 * TIME_SLOT;
        m_util_.m_timer_.adjust_timer(timer);

        LOG_INFO("adjust %d timer", fd);
    }


    void dealWithTimer(int fd) {
        auto timer = m_user_timer_[fd].m_timer_;
        if (timer) {
            timer->cb_func(timer->user_data_);
            m_util_.m_timer_.del_timer(timer);
        }
        LOG_INFO("close fd %d", fd);
    }

    void dealwithclient() {
    }
    void dealwithread(int fd) {
        adjustTimer(fd);

        auto conn = m_user_ + fd;
        LOG_INFO("%s", "epollin in webserver loop");
        m_thread_pool_->append_s(conn, 0);
    }

    void dealwithwrite(int fd) {
        adjustTimer(fd);

        auto conn = m_user_ + fd;
        LOG_INFO("%s", "epollout in webserver loop");
        m_thread_pool_->append_s(conn, 1);
    }

    bool dealwithsignal(bool& timeout, bool& stop_server) {
        char signals[1024];
        int ret = recv(m_pipe_fd_[0], &signals, sizeof(signals), 0);
        if (ret < 1) {
            LOG_ERROR("%s", "dealwithsignal error.");
            return false;
        }
        for (int _ = 0; _ < ret; ++_) {
            switch (signals[_]) {
            case SIGALRM:
                timeout = true;
                break;
            case SIGTERM:
                stop_server = true;
                break;
            default:
                break;
            }
        }
        return true;
    }


    void loop() {
        std::vector<epoll_event> events(m_max_events_num_);
        bool timeout = false;
        bool stop_server = false;
        while (!stop_server) {
            int n = epoll_wait(m_epoll_fd_, events.data(), m_max_events_num_, -1);
            if (n < 0 && errno != EINTR) {
                LOG_ERROR("%s", "epoll_wait error");
                break;
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
                            if (errno == EINTR) {
                                continue;
                            }
                            perror("accept");
                            break;
                        }

                        setNonBlocking(client_fd);

                        epollAdd(client_fd);

                        createConn(client_fd);
                    }
                }
                else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    dealWithTimer(fd);
                    //错误，关闭连接；
                }
                else if ((fd == m_pipe_fd_[0]) && (events[i].events & EPOLLIN)) {
                    dealwithsignal(timeout, stop_server);
                    //sig信号处理
                }
                else if (events[i].events & EPOLLIN) {
                    dealwithread(fd);
                }
                else if (events[i].events & EPOLLOUT) {
                    dealwithwrite(fd);
                }
            }
            if (timeout) {
                m_util_.time_handler();
                LOG_INFO("%s", "tick.");
                timeout = false;
            }
        }
    }
};
#endif //MYWEBSERVER_WEBSERVER_H

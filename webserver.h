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
#include "log/async_log.h"
#include "log/log.h"
#include "threadpool/threadpool.h"
#include "timer/timer_policy.h"
#include "timer/timer_lst.h"
#include "timer/timer_wheel.h"

//threads number for thread pool
constexpr int THREAD_NUM = 8;
//max connect number
constexpr int MAX_FD = 65535;
//time spand for one tick
constexpr int TIME_SLOT = 5;
//epoll table size
constexpr int MAX_EVENT_NUM = 10000;

class WebServer {
private:
    //http_conn
    http_conn* m_user_ = {};

    //threadpool
    Threadpool<http_conn>* m_thread_pool_ = {};

    //epoll
    int m_server_fd_ = {-1};
    int m_epoll_fd_ = {-1};
    int m_actor_model_ = {}; //Reactor : 0, Proactor : 1;
    int m_TRIGMode_ = {};
    int m_LISTENTrigMode_ = {}; //Trigger mode of server, LT : 0, ET : 1;
    int m_CONNTrigMode_ = {}; //Trigger mode of client, LT : 0, ET : 1;

    //timer
    int m_pipe_fd_[2] = {};
    int m_timer_model_ = {};
    Util m_util_ = {};
    client_data* m_user_timer_ = {};

    //log
    int m_close_log_ = {};
    int m_log_model_ = {};

    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    //初始化新的http连接，初始化该连接对应的定时器
    void createConn(int connfd) {
        m_user_[connfd].init(connfd, m_CONNTrigMode_);
        m_user_timer_[connfd].m_sock_fd_ = connfd;
        auto timer = m_util_.m_timer_->create_timer();
        timer->cb_func = cb_func;
        timer->user_data_ = &m_user_timer_[connfd];
        timer->adjust_expire(TIME_SLOT);
        m_user_timer_[connfd].m_timer_ = timer;
        m_util_.m_timer_->add_timer(timer);
    }

public:
    WebServer() {
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

    void init(int m_TRIGMode, int m_actor_model, int m_log_model, int m_close_log, int m_timer_model) {
        m_TRIGMode_ = m_TRIGMode;
        m_actor_model_ = m_actor_model;
        m_log_model_ = m_log_model;
        m_close_log_ = m_close_log;
        m_timer_model_ = m_timer_model;
    }

    void setTrigMode() {
        if (m_TRIGMode_ == 0) {
            m_LISTENTrigMode_ = 0; // LT
            m_CONNTrigMode_ = 0; // LT
        }
        if (m_TRIGMode_ == 1) {
            m_LISTENTrigMode_ = 1; // ET
            m_CONNTrigMode_ = 0; // LT
        }
        if (m_TRIGMode_ == 2) {
            m_LISTENTrigMode_ = 0; // LT
            m_CONNTrigMode_ = 1; // ET
        }
        if (m_TRIGMode_ == 3) {
            m_LISTENTrigMode_ = 1; // ET
            m_CONNTrigMode_ = 1; // ET
        }
    }

    void createThreadPool() {
        m_thread_pool_ = new Threadpool<http_conn>(THREAD_NUM, MAX_FD, m_actor_model_);
    }

    void createLog() {
        if (m_log_model_ == 0) {
            Log::getInstance()->init("./ServerLog", m_close_log_, 2048, 5000000, 800);
        }
        if (m_log_model_ == 1) {
            Async_Log::getInstance()->init("./ServerLog", m_close_log_);
        }
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
        ret = listen(m_server_fd_, 65535);
        assert(ret >= 0);
        //

        setNonBlocking(m_server_fd_);

        //创建内核epoll事件表
        m_epoll_fd_ = epoll_create1(0);
        assert(m_epoll_fd_ >= 0);
        m_util_.addfd(m_epoll_fd_, m_server_fd_, false, m_LISTENTrigMode_);

        http_conn::m_epollfd_ = m_epoll_fd_;

        //定时器相关
        ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipe_fd_);
        assert(ret >= 0);

        Util::u_epoll_fd_ = m_epoll_fd_;
        Util::u_pipe_fd_ = m_pipe_fd_;

        m_util_.init(TIME_SLOT);
        //设定定时器模式
        if (m_timer_model_ == 0) {
            m_util_.init_timer(std::make_unique<sort_timer_lst>());
        }
        if (m_timer_model_ == 1) {
            m_util_.init_timer(std::make_unique<timer_wheel>());
        }
        setNonBlocking(m_pipe_fd_[1]);
        m_util_.addfd(m_epoll_fd_, m_pipe_fd_[0], false, 0);
        m_util_.addsig(SIGPIPE, SIG_IGN, false);
        m_util_.addsig(SIGALRM, Util::sig_handler, false);
        m_util_.addsig(SIGTERM, Util::sig_handler, false);
        alarm(TIME_SLOT);

        Async_Log::LOG_INFO("Web server started on port %d", 8888);
    }

    void adjustTimer(int fd) {
        auto timer = m_user_timer_[fd].m_timer_;
        if (timer) {
            timer->adjust_expire(TIME_SLOT);
            m_util_.m_timer_->adjust_timer(timer);

            LOG_INFO("adjust fd %d's timer", fd);
        }
    }

    void dealWithTimer(int fd) {
        auto timer = m_user_timer_[fd].m_timer_;
        if (timer) {
            timer->cb_func(timer->user_data_);
            m_util_.m_timer_->del_timer(timer);

            LOG_INFO("delete fd %d's timer", fd);
        }
    }

    bool dealwithclient() {
        if (m_LISTENTrigMode_ == 0) { // LT
            int client_fd = accept(m_server_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                return false;
            }
            if (client_fd >= MAX_FD) {
                return false;
            }
            createConn(client_fd);
        }
        else { //ET
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

                if (client_fd >= MAX_FD) {
                    return false;
                }

                createConn(client_fd);
            }
        }
        return true;
    }

    void dealwithread(int fd) {
        auto conn = m_user_ + fd;
        if (m_actor_model_ == 0) {
            adjustTimer(fd);
            m_thread_pool_->append_s(conn, 0);
        }
        else {
            if (conn->read_once()) {
                m_thread_pool_->append(conn);
                adjustTimer(fd);
            }
            else {
                dealWithTimer(fd);
            }
        }
    }

    void dealwithwrite(int fd) {
        auto conn = m_user_ + fd;
        if (m_actor_model_ == 0) {
            adjustTimer(fd);
            m_thread_pool_->append_s(conn, 1);
        }
        else {
            if (conn->write()) {
                adjustTimer(fd);
            }
            else {
                dealWithTimer(fd);
            }
        }
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
        std::vector<epoll_event> events(MAX_EVENT_NUM);
        bool timeout = false;
        bool stop_server = false;
        while (!stop_server) {
            int n = epoll_wait(m_epoll_fd_, events.data(), MAX_EVENT_NUM, -1);
            if (n < 0 && errno != EINTR) {
                LOG_ERROR("%s", "epoll_wait error");
                break;
            }

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                if (fd == m_server_fd_) { // 新连接
                    if (!dealwithclient()) {
                        continue;
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

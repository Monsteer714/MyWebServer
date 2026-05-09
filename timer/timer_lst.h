//
// Created by Hanhong Wong on 2026/5/6.
//

#ifndef MYWEBSERVER_TIMER_LST_H
#define MYWEBSERVER_TIMER_LST_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <memory>

#include <time.h>
#include "../log/log.h"

class util_timer;

struct client_data {
    int m_sock_fd_;
    std::shared_ptr<util_timer> m_timer_;
};

class util_timer {
public:
    void (*cb_func)(client_data* data);
    time_t expire_ = {};

    client_data* user_data_ = {};
    std::weak_ptr<util_timer> prev_ = {};
    std::shared_ptr<util_timer> next_ = {};
public:
    util_timer() : next_(nullptr) {}
};

class sort_timer_lst {
private:
    void add_timer(const std::shared_ptr<util_timer> &timer, const std::shared_ptr<util_timer> &head);
    std::weak_ptr<util_timer> tail_ = {};
    std::shared_ptr<util_timer> head_ = {};
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(const std::shared_ptr<util_timer> &timer);
    void adjust_timer(const std::shared_ptr<util_timer> &timer);
    void del_timer(const std::shared_ptr<util_timer> &timer);
    void tick();
};

class Util {
public:
    inline static int u_epoll_fd_ = {};
    inline static int *u_pipe_fd_ = {};
    sort_timer_lst m_timer_ = {};
    int m_time_slot_ = {};
public:
    Util() {}
    ~Util() {}

    void init(int timeslot);
    void addsig(int sig, void (*handler)(int), bool restart);
    void setnonblocking(int fd);
    void addfd(int epollfd, int fd, bool one_shot, int trigmode);
    void time_handler();
    static void sig_handler(int sig);
};

void cb_func(client_data* data);

#endif //MYWEBSERVER_TIMER_LST_H

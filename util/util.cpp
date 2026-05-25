//
// Created by Hanhong Wong on 2026/5/16.
//

#include "../timer/timer_policy.h"

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
#include <ctime>

#include "../log/log.h"
#include "../log/async_log.h"

// ========== Util 成员定义 ==========

Util::Util() = default;
Util::~Util() = default;

void Util::init(int timeslot) {
    m_time_slot_ = timeslot;
}

void Util::init_timer(std::unique_ptr<timer_container> container) {
    m_timer_ = std::move(container);
}

void Util::addsig(int sig, void (*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    int ret = sigaction(sig, &sa, nullptr);
    assert(ret != -1);
}

void Util::setnonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

void Util::addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;

    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

bool modfd(int epfd, int fd, int ev) {
    epoll_event event = {};
    event.data.fd = fd;

    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
    return true;
}

bool delfd(int epfd, int fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, 0);
    return true;
}

void Util::sig_handler(int sig) {
    int saved_errno = errno;

    int msg = sig;
    send(u_pipe_fd_[1], &msg, 1, 0);
    errno = saved_errno;
}

void Util::time_handler() {
    m_timer_->tick();
    alarm(m_time_slot_);
}

// ========== 定时器到期回调 ==========

void cb_func(client_data* data) {
    int& fd = data->m_sock_fd_;

    epoll_ctl(Util::u_epoll_fd_, EPOLL_CTL_DEL, fd, 0);

    if (fd > 0) {
        close(fd);
        LOG_INFO("close fd %d", fd);
        fd = -1;
    }
}
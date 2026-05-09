//
// Created by Hanhong Wong on 2026/5/6.
//
#include "timer_lst.h"

sort_timer_lst::sort_timer_lst() {
    auto head = std::make_shared<util_timer>();
    auto tail = std::make_shared<util_timer>();
    head->expire_ = time_t(0);
    tail->expire_ = time_t(INT_MAX);
    head->next_ = tail;
    tail->prev_ = head;
    head_ = std::move(head);
    tail_ = head_->next_;
}

sort_timer_lst::~sort_timer_lst() {
    while (head_) {
        head_ = std::move(head_->next_);
    }
}

void sort_timer_lst::add_timer(const std::shared_ptr<util_timer> &timer) {
    if (!timer) {
        return;
    }
    add_timer(timer, head_);
}

void sort_timer_lst::adjust_timer(const std::shared_ptr<util_timer> &timer) {
    if (!timer) {
        return;
    }
    auto next = timer->next_;
    auto prev = timer->prev_.lock();

    if (!next || !prev) {
        return;
    }

    next->prev_ = prev;
    prev->next_ = next;
    add_timer(timer, head_);
}

void sort_timer_lst::del_timer(const std::shared_ptr<util_timer> &timer) {
    if (!timer) {
        return;
    }

    auto next = timer->next_;
    auto prev = timer->prev_.lock();

    if (!next || !prev) {
        return;
    }

    next->prev_ = prev;
    prev->next_ = next;

    timer->next_.reset();
    timer->prev_.reset();
}

void sort_timer_lst::add_timer(const std::shared_ptr<util_timer> &timer, const std::shared_ptr<util_timer> &head) {
    auto temp = head->next_;
    while (temp) {
        if (timer->expire_ < temp->expire_) {
            auto prev = temp->prev_.lock();
            timer->next_ = temp;
            timer->prev_ = temp->prev_;
            if (prev) {
                prev->next_ = timer;
            }
            temp->prev_ = timer;
            return;
        } else {
            temp = temp->next_;
        }
    }
}

void sort_timer_lst::tick() {
    time_t now = time(nullptr);
    auto temp = head_->next_;
    while (temp->next_ != nullptr) {
        auto next = temp->next_;

        if (now >= temp->expire_) {
            del_timer(temp);
            temp->cb_func(temp->user_data_);
        } else {
            break;
        }

        temp = next;
    }
}

void cb_func(client_data* data) {
    int fd = data->m_sock_fd_;

    epoll_ctl(Util::u_epoll_fd_, EPOLL_CTL_DEL, fd, 0);

    LOG_INFO("close fd %d", fd);
    close(fd);
}

void Util::setnonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

void Util::addfd(int epollfd, int fd, bool one_shot, int trigmode) {
    epoll_event event;
    event.data.fd = fd;

    event.events =  EPOLLIN | EPOLLET | EPOLLRDHUP;

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

void Util::init(int timeslot) {
    m_time_slot_ = timeslot;
}

void Util::sig_handler(int sig) {
    int saved_errno = errno;
    int msg = sig;
    send(u_pipe_fd_[1], &msg, 1, 0);
    errno = saved_errno;
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

void Util::time_handler() {
    m_timer_.tick();
    alarm(m_time_slot_);
}

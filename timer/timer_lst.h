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
#include "timer_policy.h"


class util_timer : public timer {
public:
    // cb_func、expire_、user_data_、adjust_expire 均由基类 timer 提供

    // 升序链表特有：前后指针
    std::weak_ptr<util_timer> prev_ = {};
    std::shared_ptr<util_timer> next_ = {};

public:
    util_timer() : next_(nullptr) {
    }
};

class sort_timer_lst : public timer_container {
private:
    void add_timer(const std::shared_ptr<util_timer>& timer, const std::shared_ptr<util_timer>& head);
    std::weak_ptr<util_timer> tail_ = {};
    std::shared_ptr<util_timer> head_ = {};

public:
    sort_timer_lst();
    ~sort_timer_lst() override;

    std::shared_ptr<timer> create_timer() override;
    void add_timer(const std::shared_ptr<timer>& timer) override;
    void adjust_timer(const std::shared_ptr<timer>& timer) override;
    void del_timer(const std::shared_ptr<timer>& timer) override;
    void tick() override;
};

#endif //MYWEBSERVER_TIMER_LST_H
//
// Created by Hanhong Wong on 2026/5/16.
//

#ifndef MYWEBSERVER_TIMER_WHEEL_H
#define MYWEBSERVER_TIMER_WHEEL_H

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


// 时间轮定时器节点 — 继承自 timer 基类
class tw_timer : public timer {
public:
    // cb_func、expire_、user_data_、adjust_expire 均由基类 timer 提供

    // 时间轮特有：槽位位置
    int rotation_ = {};
    int time_slot_ = {};

    // 槽内双向链表指针
    std::shared_ptr<tw_timer> next_ = {};
    std::weak_ptr<tw_timer> prev_ = {};
};

// 时间轮定时器容器 — 继承自 timer_container 抽象接口
class timer_wheel : public timer_container {
private:
    static const int N_ = 60;   // 槽数量
    static const int SI_ = 1;   // 每个槽的时间间隔（秒）
    int cur_slot_ = {};
    std::vector<std::shared_ptr<tw_timer>> timer_ = {};

public:
    timer_wheel();
    ~timer_wheel() override;

    std::shared_ptr<timer> create_timer() override;
    void add_timer(const std::shared_ptr<timer>& timer) override;
    void adjust_timer(const std::shared_ptr<timer>& timer) override;
    void del_timer(const std::shared_ptr<timer>& timer) override;
    void tick() override;
};

#endif //MYWEBSERVER_TIMER_WHEEL_H
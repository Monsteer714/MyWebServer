//
// Created by Hanhong Wong on 2026/5/16.
//

#ifndef MYWEBSERVER_TIMER_POLICY_H
#define MYWEBSERVER_TIMER_POLICY_H

#include <memory>
#include <ctime>

// 前置声明
struct client_data;

// 定时器抽象基类 — 所有定时器类型的公共属性和接口
class timer {
public:
    client_data* user_data_ = {};
    time_t expire_ = {};                             // 绝对过期时间，所有定时器通用
    void (*cb_func)(client_data* data) = nullptr;

    void adjust_expire(int time_slot) {
        this->expire_ = time(nullptr) + 3 * time_slot;
    }

    virtual ~timer() = default;
};

// client_data 依赖 timer 的完整定义
struct client_data {
    int m_sock_fd_ = {};
    std::shared_ptr<timer> m_timer_;
};

// 定时器容器抽象接口 — 升序链表 / 时间轮 / 时间堆 都必须实现
class timer_container {
public:
    virtual ~timer_container() = default;

    // 工厂方法 — 每个容器知道该创建什么类型的节点

    virtual std::shared_ptr<timer> create_timer() = 0;

    virtual void add_timer(const std::shared_ptr<timer>& timer) = 0;
    virtual void adjust_timer(const std::shared_ptr<timer>& timer) = 0;
    virtual void del_timer(const std::shared_ptr<timer>& timer) = 0;
    virtual void tick() = 0;
};

// 工具类 — 持有定时器容器（多态），提供信号/fd/定时等辅助功能
class Util {
public:
    inline static int u_epoll_fd_ = {};
    inline static int* u_pipe_fd_ = {};

    std::unique_ptr<timer_container> m_timer_ = {};
    int m_time_slot_ = {};

    Util();
    ~Util();

    void init(int timeslot);
    void init_timer(std::unique_ptr<timer_container> container);
    void addsig(int sig, void (*handler)(int), bool restart);
    void setnonblocking(int fd);
    void addfd(int epollfd, int fd, bool one_shot, int trigmode);
    void time_handler();
    static void sig_handler(int sig);
};

// 定时器到期回调 — 关闭 socket，从 epoll 移除
void cb_func(client_data* data);

#endif //MYWEBSERVER_TIMER_POLICY_H
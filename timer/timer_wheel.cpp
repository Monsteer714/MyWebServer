//
// Created by Hanhong Wong on 2026/5/16.
//
#include "timer_wheel.h"

// ========== timer_wheel 占位实现 ==========
// 时间轮的具体逻辑待后续完善

timer_wheel::timer_wheel() {
    timer_.resize(N_);
}

timer_wheel::~timer_wheel() {
}

std::shared_ptr<timer> timer_wheel::create_timer() {
    return std::make_shared<tw_timer>();
}

void timer_wheel::add_timer(const std::shared_ptr<timer>& t) {
    (void)t;
    // TODO: 将 timer 的 expire_ 转换为 rotation + time_slot，插入对应槽位
}

void timer_wheel::adjust_timer(const std::shared_ptr<timer>& t) {
    (void)t;
    // TODO: 从当前槽位移除，重新计算位置后插入
}

void timer_wheel::del_timer(const std::shared_ptr<timer>& t) {
    (void)t;
    // TODO: 从槽位链表中移除定时器
}

void timer_wheel::tick() {
    // TODO: 推进时间轮，触发到期的定时器回调
}
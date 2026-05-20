//
// Created by Hanhong Wong on 2026/5/16.
//
#include "timer_wheel.h"

// ========== timer_wheel 占位实现 ==========
// 时间轮的具体逻辑待后续完善

timer_wheel::timer_wheel() {
    this->timer_.resize(N_);
    for (int _ = 0; _ < N_; ++_) {
        timer_[_] = nullptr;
    }
}

timer_wheel::~timer_wheel() {
    for (int _ = 0; _ < N_; ++_) {
        if (timer_[_] != nullptr) {
            auto head = timer_[_];
            while (head) {
                head = std::move(head->next_);
            }
        }
    }
}

std::shared_ptr<timer> timer_wheel::create_timer() {
    return std::make_shared<tw_timer>();
}

void timer_wheel::add_timer(const std::shared_ptr<timer>& t) {
    if (!t) {
        return;
    }

    int time_gap = t->expire_ - time(nullptr);
    if (time_gap < 0) {
        return;
    }

    auto timer = std::static_pointer_cast<tw_timer>(t);

    int tick = 0;
    if (time_gap < SI_) {
        tick = 1;
    }
    else {
        tick = time_gap / SI_;
    }
    int rotation = tick / N_;
    int slot = (cur_slot_ + (tick % N_)) % N_;
    timer->rotation_ = rotation;
    timer->time_slot_ = slot;

    LOG_INFO("add timer to slot %d", slot);

    auto head = timer_[slot];
    if (head == nullptr) {
        timer_[slot] = timer;
    }
    else {
        timer->next_ = head;
        head->prev_ = timer;
        timer_[slot] = timer;
    }
}

void timer_wheel::adjust_timer(const std::shared_ptr<timer>& t) {
    if (!t) {
        return;
    }

    auto timer = std::static_pointer_cast<tw_timer>(t);

    int slot = timer->time_slot_;
    if (timer_[slot] == timer) { //是该槽位的头节点
        auto temp = timer->next_;
        if (temp != nullptr) {
            temp->prev_.reset();
        }
        timer_[slot] = temp;
    }
    else {
        auto next = timer->next_;
        auto prev = timer->prev_.lock();

        prev->next_ = next;
        if (next != nullptr) {
            next->prev_ = prev;
        }
    }

    if (timer->next_) {
        timer->next_.reset();
    }
    if (timer->prev_.lock()) {
        timer->prev_.reset();
    }

    add_timer(timer);
}

void timer_wheel::del_timer(const std::shared_ptr<timer>& t) {
    if (!t) {
        return;
    }

    auto timer = std::static_pointer_cast<tw_timer>(t);

    int slot = timer->time_slot_;
    if (timer_[slot] == timer) { //是该槽位的头节点
        auto temp = timer->next_;
        if (temp != nullptr) {
            temp->prev_.reset();
        }
        timer_[slot] = temp;
    }
    else {
        auto next = timer->next_;
        auto prev = timer->prev_.lock();

        prev->next_ = next;
        if (next != nullptr) {
            next->prev_ = prev;
        }
    }

    if (timer->next_) {
        timer->next_.reset();
    }
    if (timer->prev_.lock()) {
        timer->prev_.reset();
    }
}

void timer_wheel::tick() {
    auto temp = timer_[cur_slot_];

    LOG_INFO("tick to slot %d", cur_slot_);

    while (temp) {
        if (temp->rotation_ > 1) {
            temp->rotation_--;
            temp = temp->next_;
        } else {
            temp->cb_func(temp->user_data_);
            auto next = temp->next_;
            del_timer(temp);
            temp = next;
        }
    }
    cur_slot_ = (cur_slot_ + 1) % N_;
}

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

void sort_timer_lst::add_timer(const std::shared_ptr<timer> &t) {
    if (!t) {
        return;
    }
    auto timer = std::static_pointer_cast<util_timer>(t);
    add_timer(timer, head_);
}

void sort_timer_lst::adjust_timer(const std::shared_ptr<timer> &t) {
    if (!t) {
        return;
    }
    auto timer = std::static_pointer_cast<util_timer>(t);
    auto next = timer->next_;
    auto prev = timer->prev_.lock();

    if (!next || !prev) {
        return;
    }

    next->prev_ = prev;
    prev->next_ = next;
    add_timer(timer, head_);
}

void sort_timer_lst::del_timer(const std::shared_ptr<timer> &t) {
    if (!t) {
        return;
    }
    auto timer = std::static_pointer_cast<util_timer>(t);

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

// 工厂方法 — 升序链表容器创建对应的定时器节点
std::shared_ptr<timer> sort_timer_lst::create_timer() {
    return std::make_shared<util_timer>();
}
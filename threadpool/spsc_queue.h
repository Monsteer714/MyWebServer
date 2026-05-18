//
// Created by Hanhong Wong on 2026/5/17.
//

#ifndef MYWEBSERVER_SPSC_QUEUE_H
#define MYWEBSERVER_SPSC_QUEUE_H

#include <atomic>
#include <array>

template <typename T, ssize_t N>
class spsc_queue {
private:
    std::array<T, N + 1> array_ = {};
    ssize_t capacity_ = {};
    alignas(64) std::atomic<ssize_t> head_ = {0};
    alignas(64) std::atomic<ssize_t> tail_ = {0};

public:
    spsc_queue() {
        capacity_ = N + 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~spsc_queue() {
    }

    bool push(T item) {
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_acquire);
        auto next_h = (h + 1) % capacity_;

        if (next_h == t) {
            return false;
        }

        array_[h] = item;

        head_.store(next_h, std::memory_order_release);

        return true;
    }

    bool pop() {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_relaxed);

        if (h == t) {
            return false;
        }

        array_[t] = T{};

        auto next_t = (t + 1) % capacity_;
        tail_.store(next_t, std::memory_order_release);

        return true;
    }

    T front() {
        if (empty()) {
            return T{};
        }

        auto t = tail_.load(std::memory_order_relaxed);

        return array_[t];
    }

    bool empty() {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_relaxed);

        if (h == t) {
            return true;
        }

        return false;
    }

    bool full() {
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_acquire);

        auto next_h = (h + 1) % capacity_;

        if (next_h == t) {
            return true;
        }

        return false;
    }

    ssize_t size() {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_acquire);

        return (h - t + capacity_) % capacity_;
    }
};

#endif //MYWEBSERVER_SPSC_QUEUE_H

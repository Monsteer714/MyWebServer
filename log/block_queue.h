//
// Created by Hanhong Wong on 2026/4/27.
//

#ifndef MYWEBSERVER_BLOCK_QUEUE_H
#define MYWEBSERVER_BLOCK_QUEUE_H
#include <pthread.h>
#include <queue>

#include "../locker/locker.h"

template <typename T>
class block_queue {
private:
    locker mutex_ = {};
    cond cond_ = {};
    std::queue<T> queue_ = {};
    ssize_t max_size_ = {};

public:
    block_queue(ssize_t max_size) : max_size_(max_size) {
    }

    ~block_queue() {
        mutex_.lock();
        cond_.broadcast();
        mutex_.unlock();
    }

    //productor
    bool push(const T& item) {
        mutex_.lock();

        while (queue_.size() >= max_size_) {
            if (!cond_.wait(mutex_.getMutex())) {
                mutex_.unlock();
                return false;
            }
        }

        queue_.push(item);

        cond_.signal();
        mutex_.unlock();
        return true;
    }

    //consumer
    bool pop(T& item) {
        mutex_.lock();

        while (queue_.empty()) {
            if (!cond_.wait(mutex_.getMutex())) {
                mutex_.unlock();
                return false;
            }
        }

        item = queue_.front();
        queue_.pop();

        cond_.signal();
        mutex_.unlock();
        return true;
    }

    bool is_full() {
        mutex_.lock();
        bool res = queue_.size() >= max_size_;
        mutex_.unlock();
        return res;
    }
};


#endif //MYWEBSERVER_BLOCK_QUEUE_H

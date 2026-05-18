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
    locker m_mutex_ = {};
    cond m_cond_ = {};
    std::queue<T> m_queue_ = {};
    ssize_t m_max_size_ = {};
    bool m_shutdown_ = {};

public:
    block_queue(ssize_t max_size) : m_max_size_(max_size) {
    }

    ~block_queue() {
        m_mutex_.lock();
        m_shutdown_ = true;
        m_cond_.broadcast();
        m_mutex_.unlock();
    }

    //productor
    bool push(const T& item) {
        m_mutex_.lock();

        if (m_queue_.size() >= m_max_size_) {
            m_cond_.broadcast();
            m_mutex_.unlock();
            return false;
        }

        m_queue_.push(item);

        m_cond_.broadcast();
        m_mutex_.unlock();
        return true;
    }

    //consumer
    bool pop(T& item) {
        m_mutex_.lock();

        while (m_queue_.empty() && !m_shutdown_) {
            if (!m_cond_.wait(m_mutex_)) {
                m_mutex_.unlock();
                return false;
            }
        }

        if (m_shutdown_) {
            m_mutex_.unlock();
            return false;
        }

        item = m_queue_.front();
        m_queue_.pop();

        m_cond_.signal();
        m_mutex_.unlock();
        return true;
    }

    bool is_full() {
        m_mutex_.lock();
        bool res = m_queue_.size() >= m_max_size_;
        m_mutex_.unlock();
        return res;
    }
};


#endif //MYWEBSERVER_BLOCK_QUEUE_H

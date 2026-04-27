//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_THREADPOOL_H
#define MYWEBSERVER_THREADPOOL_H
#include "../locker/locker.h"
#include <vector>
#include <queue>


template <typename T>
class Threadpool {
private:
    size_t thread_num_ = {};
    size_t max_thread_num_ = {};
    std::vector<pthread_t> threads_ = {};
    std::queue<T*> task_queue_ = {};
    locker mutex_ = {};
    cond cond_ = {};
    bool shutdown_ = {};

    static void* worker(void* arg) {
        auto tp = static_cast<Threadpool*>(arg);
        while (true) {
            tp->mutex_.lock();

            while (tp->task_queue_.empty() && !tp->shutdown_) {
                tp->cond_.wait(tp->mutex_.getMutex());
            }

            if (tp->shutdown_ && tp->task_queue_.empty()) {
                tp->mutex_.unlock();
                return nullptr;
            }

            auto task = tp->task_queue_.front();
            tp->task_queue_.pop();

            tp->mutex_.unlock();
            task->process();
        }
    }

public:
    Threadpool(size_t thread_num, size_t max_thread_num) {
        thread_num_ = thread_num;
        max_thread_num_ = max_thread_num;
        threads_ = std::vector<pthread_t>(thread_num_);
        size_t created = 0;
        for (size_t i = 0; i < thread_num_; ++i, ++created) {
            if (pthread_create(&threads_[i], nullptr, worker, this) != 0) {
                shutdown_ = true;
                cond_.broadcast();
                for (size_t j = 0; j < created; ++j) {
                    pthread_join(threads_[j], nullptr);
                }
                throw std::runtime_error("pthread_create failed");
            }
        }
    };

    ~Threadpool() {
        shutdown_ = true;
        cond_.broadcast();
        for (size_t i = 0; i < thread_num_; ++i) {
            pthread_join(threads_[i], nullptr);
        }
    }

    bool append(T* task) {
        mutex_.lock();

        if (task_queue_.size() >= max_thread_num_) {
            mutex_.unlock();
            return false;
        }

        task_queue_.push(task);
        mutex_.unlock();
        cond_.signal();
        return true;
    }
};
#endif //MYWEBSERVER_THREADPOOL_H

//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_THREADPOOL_H
#define MYWEBSERVER_THREADPOOL_H
#include "../locker/locker.h"
#include <vector>
#include <queue>
#include "spsc_queue.h"

template <typename T>
class Threadpool {
private:
    struct Worker {
        pthread_t thread_ = {};
        spsc_queue<T*, 8192> task_queue_ = {};
        locker mutex_ = {};
        cond cond_ = {};
        Threadpool* threadpool_ = {};
    };

    size_t thread_num_ = {};
    size_t m_max_conn_num_ = {};
    std::unique_ptr<Worker[]> workers_ = {};
    std::atomic<size_t> round_robin_cnt{0};
    std::atomic<bool> shutdown_{false};
    int m_actor_model_ = {};

    static void* worker(void* arg) {
        auto& w = *static_cast<Worker*>(arg);
        auto tp = w.threadpool_;

        while (true) {
            T* task = nullptr;

            // 快速路径：无锁取任务，完全不碰 mutex
            if (w.task_queue_.pop(task)) {
                goto process_task;
            }

            // 慢速路径：队列空，持锁等待
            w.mutex_.lock();
            if (!w.task_queue_.pop(task)) {
                while (!tp->shutdown_.load(std::memory_order_acquire)
                       && !w.task_queue_.pop(task)) {
                    w.cond_.wait(w.mutex_);
                }
                if (tp->shutdown_.load(std::memory_order_acquire)) {
                    w.mutex_.unlock();
                    return nullptr;
                }
            }
            w.mutex_.unlock();

        process_task:
            if (tp->m_actor_model_ == 0) { //Reactor
                if (task->m_state_ == 0) { //read
                    if (task->read_once()) {
                        task->process();
                        task->m_op_finish_flag_ = true;
                    } else {
                        task->m_op_finish_flag_ = true;
                        task->m_error_flag_ = true;
                    }
                } else { //write
                    if (task->write()) {
                        task->m_op_finish_flag_ = true;
                    } else {
                        task->m_op_finish_flag_ = true;
                        task->m_error_flag_ = true;
                    }
                }
            } else { // Proactor
                task->process();
            }
        }
    }

public:
    Threadpool(size_t thread_num, size_t m_max_conn_num, int m_actor_model) {
        thread_num_ = thread_num;
        m_max_conn_num_ = m_max_conn_num;
        m_actor_model_ = m_actor_model;
        workers_ = std::make_unique<Worker[]>(thread_num_);
        size_t created = 0;

        for (size_t i = 0; i < thread_num_; ++i, ++created) {
            auto& w = workers_[i];
            w.threadpool_ = this;

            if (pthread_create(&w.thread_, nullptr, worker, &w) != 0) {
                shutdown_.store(true, std::memory_order_relaxed);
                for (size_t j = 0; j < created; ++j) {
                    auto& wj = workers_[j];
                    wj.mutex_.lock();
                    wj.cond_.signal();
                    wj.mutex_.unlock();
                    pthread_join(wj.thread_, nullptr);
                }
                throw std::runtime_error("pthread_create failed");
            }
        }
    };

    ~Threadpool() {
        shutdown_.store(true, std::memory_order_release);
        for (size_t i = 0; i < thread_num_; ++i) {
            auto& w = workers_[i];
            w.mutex_.lock();
            w.cond_.signal();
            w.mutex_.unlock();
            pthread_join(w.thread_, nullptr);
        }
    }

    bool append(T* task) {
        auto index = round_robin_cnt.fetch_add(1, std::memory_order_relaxed) % thread_num_;

        auto& w = workers_[index];

        bool was_empty = w.task_queue_.empty();

        if (!w.task_queue_.push(task)) {
            return false;
        }

        if (was_empty) {
            w.mutex_.lock();
            w.cond_.signal();
            w.mutex_.unlock();
        }

        return true;
    }

    bool append_s(T* task, int state) {
        auto index = round_robin_cnt.fetch_add(1, std::memory_order_relaxed) % thread_num_;

        auto& w = workers_[index];

        bool was_empty = w.task_queue_.empty();

        task->m_state_ = state;

        if (!w.task_queue_.push(task)) {
            return false;
        }

        if (was_empty) {
            w.mutex_.lock();
            w.cond_.signal();
            w.mutex_.unlock();
        }

        return true;
    }
};
#endif //MYWEBSERVER_THREADPOOL_H


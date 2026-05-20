//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_LOCKER_H
#define MYWEBSERVER_LOCKER_H

#include <pthread.h>
#include <semaphore.h>

class locker {
private:
    pthread_mutex_t mutex_ = {};

public:
    locker() {
        pthread_mutex_init(&mutex_, nullptr);
    }

    ~locker() {
        pthread_mutex_destroy(&mutex_);
    }

    bool lock() {
        return pthread_mutex_lock(&mutex_) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&mutex_) == 0;
    }

    pthread_mutex_t* getMutex() {
        return &mutex_;
    }
};

class cond {
private:
    pthread_cond_t cond_ = {};

public:
    cond() {
        pthread_cond_init(&cond_, nullptr);
    }

    ~cond() {
        pthread_cond_destroy(&cond_);
    }

    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&cond_, mutex) == 0;
    }

    bool wait_for_time(pthread_mutex_t* mutex, int seconds) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += seconds;
        return pthread_cond_timedwait(&cond_, mutex, &ts) == 0;
    }

    bool signal() {
        return pthread_cond_signal(&cond_) == 0;
    }

    bool broadcast() {
        return pthread_cond_broadcast(&cond_) == 0;
    }

    pthread_cond_t* getCond() {
        return &cond_;
    }
};
#endif //MYWEBSERVER_HTTP_CONN_H

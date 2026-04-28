//
// Created by Hanhong Wong on 2026/4/28.
//

#ifndef MYWEBSERVER_LOG_H
#define MYWEBSERVER_LOG_H
#include <string>
#include <cstdio>
#include "../locker/locker.h"
#include "block_queue.h"

class Log {
public:
    static Log* getInstance() {
        static Log instance;
        return &instance;
    }

    bool init(const char* m_log_name, int m_close_log, ssize_t m_max_buf_size = 2048, ssize_t m_max_log_lines = 5000000,
              ssize_t m_max_queue_size = 0);

    void write_log(int level, const char* format, ...);

    static void* async_write_log(void* args) {
        getInstance()->async_write_in();
        return nullptr;
    }

    void flush();
private:
    char m_log_name_[128] = {};
    char m_dir_name_[128] = {};
    char* m_log_buffer_ = {};
    int m_close_log_ = {};
    int m_today_ = {};
    bool m_is_async_ = {};
    locker m_mutex_ = {};
    block_queue<std::string>* m_queue_;
    ssize_t m_max_queue_size_ = {};
    ssize_t m_max_buf_size_ = {};
    ssize_t m_max_log_lines_ = {};
    ssize_t m_count_ = {};
    FILE* m_fp_ = {};

private:
    Log();
    ~Log();

    void async_write_in() {
        std::string log;
        while (m_queue_->pop(log)) {
            m_mutex_.lock();

            fputs(log.c_str(), m_fp_);
            fflush(m_fp_);

            m_mutex_.unlock();
        }
    }
};

#define LOG_DEBUG(format, ...) Log::getInstance()->write_log(0, format, ##__VA_ARGS__); Log::getInstance()->flush();
#define LOG_INFO(format, ...)  Log::getInstance()->write_log(1, format, ##__VA_ARGS__); Log::getInstance()->flush();
#define LOG_WARN(format, ...)  Log::getInstance()->write_log(2, format, ##__VA_ARGS__); Log::getInstance()->flush();
#define LOG_ERROR(format, ...) Log::getInstance()->write_log(3, format, ##__VA_ARGS__); Log::getInstance()->flush();

#endif //MYWEBSERVER_LOG_H

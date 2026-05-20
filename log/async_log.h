//
// Created by Hanhong Wong on 2026/5/19.
//

#ifndef MYWEBSERVER_ASYNC_LOG_H
#define MYWEBSERVER_ASYNC_LOG_H

#include <string>
#include <atomic>
#include <memory>
#include <vector>
#include "../locker/locker.h"
#include "large_log_buffer.h"


class Async_Log {
public:
    static Async_Log* getInstance() {
        static Async_Log instance;
        return &instance;
    }

    bool init(const char* m_log_name, int m_close_log);

    void write_log(int level, const char* format, ...);

    static void* async_write_log(void* args);

    void flush();
private:
    using Buffer = LargeLogBuffer;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    BufferPtr curBuffer_ = {};
    BufferPtr nextBuffer_ = {};
    BufferVector buffers_ = {};
    std::atomic<bool> m_running_ = {};
    char m_log_name_[128] = {};
    char m_dir_name_[128] = {};
    int m_close_log_ = {};
    int m_today_ = {};
    int m_max_buf_size_ = {};

    pthread_t m_thread_ = {};
    locker m_mutex_ = {};
    cond m_cond_ = {};
    FILE* m_fp_;

private:
    Async_Log();
    ~Async_Log();

    void async_write_in();
};

#define LOG_DEBUG(format, ...) Async_Log::getInstance()->write_log(0, format, ##__VA_ARGS__);
#define LOG_INFO(format, ...)  Async_Log::getInstance()->write_log(1, format, ##__VA_ARGS__);
#define LOG_WARN(format, ...)  Async_Log::getInstance()->write_log(2, format, ##__VA_ARGS__);
#define LOG_ERROR(format, ...) Async_Log::getInstance()->write_log(3, format, ##__VA_ARGS__);

#endif //MYWEBSERVER_ASYNC_LOG_H
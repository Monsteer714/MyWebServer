#include "async_log.h"
//
// Created by Hanhong Wong on 2026/5/19.
//
Async_Log::Async_Log() {
    m_log_buffer_ = {};
    m_max_buf_size_ = 1024;
    m_running_ = true;
    memset(m_log_buffer_, '\0', m_max_buf_size_);
}

Async_Log::~Async_Log() {
    if (m_fp_ != nullptr) {
        fclose(m_fp_);
    }
    m_running_ = false;
    delete[] m_log_buffer_;
}

bool Async_Log::init(const char* m_log_name, int m_close_log) {
    pthread_t tid;
    pthread_create(&tid, nullptr, async_write_log, nullptr);

    m_close_log_ = m_close_log;

    time_t t = time(nullptr);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(m_log_name, '/');
    char m_log_fullname[256] = {};

    if (p == nullptr) {
        snprintf(m_log_fullname, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                 m_log_name);
    }
    else {
        strcpy(m_log_name_, p + 1);
        strncpy(m_dir_name_, m_log_name, p - m_log_name + 1);
        snprintf(m_log_fullname, 255, "%s%d_%02d_%02d_%s", m_dir_name_, my_tm.tm_year + 1900, my_tm.tm_mon + 1,
                 my_tm.tm_mday, m_log_name_);
    }

    m_today_ = my_tm.tm_mday;

    m_fp_ = fopen(m_log_fullname, "a");
    if (m_fp_ == nullptr) {
        return false;
    }
    return true;
}

void Async_Log::write_log(int level, const char* format, ...) {
    if (m_close_log_) return;

    time_t t = time(nullptr);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16];

    switch (level) {
    case(0):
        strcpy(s, "[debug]: ");
        break;
    case(1):
        strcpy(s, "[info]: ");
        break;
    case(2):
        strcpy(s, "[warning]: ");
        break;
    case(3):
        strcpy(s, "[error]: ");
        break;
    default:
        strcpy(s, "[info]: ");
        break;
    }

    m_mutex_.lock();

    va_list valist;
    va_start(valist, format);

    int n = snprintf(m_log_buffer_, m_max_buf_size_, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ", my_tm.tm_year + 1900, my_tm.tm_mon + 1,
                     my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, t % 1000000, s);
    int m = vsnprintf(m_log_buffer_ + n, m_max_buf_size_ - n - 1, format, valist);
    m_log_buffer_[n + m] = '\n';
    m_log_buffer_[n + m + 1] = '\0';

    int len = n + m + 2;

    if (curBuffer_->avail() > len) {
        curBuffer_->append(m_log_buffer_, len);
    } else {
        buffers_.emplace_back(std::move(curBuffer_));

        if (nextBuffer_ != nullptr) {
            curBuffer_ = std::move(nextBuffer_);
        } else {
            curBuffer_ = std::move(std::make_unique<Buffer>());
        }

        curBuffer_->append(m_log_buffer_, len);
        m_cond_.signal();
    }

    m_mutex_.unlock();

    va_end(valist);
}

void Async_Log::async_write_in() {
    BufferPtr newBuffer1 = std::make_unique<Buffer>();
    BufferPtr newBuffer2 = std::make_unique<Buffer>();
    BufferVector buffersToWrite;
    while (m_running_) {
        m_mutex_.lock();
        if (buffers_.empty()) {
            m_cond_.wait_for_time(m_mutex_.getMutex(), 3);
        }

        buffers_.emplace_back(std::move(curBuffer_));
        curBuffer_ = std::move(newBuffer1);

        buffersToWrite.swap(buffers_);

        if (nextBuffer_ == nullptr) {
            nextBuffer_ = std::move(newBuffer2);
        }

        m_mutex_.unlock();


        for (const auto& buffer : buffersToWrite) {
            fputs(buffer->data(), m_fp_);
        }

        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }

        if (newBuffer1 == nullptr) {
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }

        if (newBuffer2 == nullptr) {
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }

        buffersToWrite.clear();
        Async_Log::flush();
    }
    Async_Log::flush();
}

void* Async_Log::async_write_log(void* args) {
    Async_Log::getInstance()->async_write_in();
    return nullptr;
}

void Async_Log::flush() {
    if (m_close_log_) {
        return;
    }
    m_mutex_.lock();
    fflush(m_fp_);
    m_mutex_.unlock();
}

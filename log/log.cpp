//
// Created by Hanhong Wong on 2026/4/28.
//

#include "log.h"
#include <sys/time.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdarg>

Log::Log() {
    m_count_ = 0;
    m_is_async_ = false;
}

Log::~Log() {
    if (m_fp_ != nullptr) {
        fclose(m_fp_);
    }
    delete[] m_log_buffer_;
}

bool Log::init(const char* m_log_name, int m_close_log, ssize_t m_max_buf_size, ssize_t m_max_log_lines,
               ssize_t m_max_queue_size) {
    if (m_max_queue_size > 0) {
        m_is_async_ = true;
        m_queue_ = new block_queue<std::string>(m_max_queue_size);
        pthread_t tid;
        pthread_create(&tid, nullptr, async_write_log, nullptr);
    }

    m_max_log_lines_ = m_max_log_lines;
    m_close_log_ = m_close_log;

    m_max_buf_size_ = m_max_buf_size;
    m_log_buffer_ = new char[m_max_buf_size_];
    memset(m_log_buffer_, '\0', m_max_buf_size_);

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

void Log::write_log(int level, const char* format, ...) {
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

    std::string log_str;

    m_mutex_.lock();

    va_list valist;
    va_start(valist, format);

    int n = snprintf(m_log_buffer_, m_max_buf_size_, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ", my_tm.tm_year + 1900, my_tm.tm_mon + 1,
                     my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, t % 1000000, s);
    int m = vsnprintf(m_log_buffer_ + n, m_max_buf_size_ - n - 1, format, valist);
    m_log_buffer_[n + m] = '\n';
    m_log_buffer_[n + m + 1] = '\0';
    log_str = m_log_buffer_;

    m_mutex_.unlock();

    if (m_is_async_ && !m_queue_->is_full()) {
        m_queue_->push(log_str);
    } else {
        m_mutex_.lock();
        fputs(log_str.c_str(), m_fp_);
        m_mutex_.unlock();
    }

    va_end(valist);
}

void Log::flush() {
    m_mutex_.lock();
    fflush(m_fp_);
    m_mutex_.unlock();
}

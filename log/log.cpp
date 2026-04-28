//
// Created by Hanhong Wong on 2026/4/28.
//

#include "log.h"

Log::Log() {

}

Log::~Log() {

}

bool Log::init(const char* m_log_name, int m_close_log, ssize_t m_max_buf_size, ssize_t m_max_log_lines, ssize_t m_max_queue_size) {
    if (m_max_queue_size > 0) {
        m_is_async_ = true;
        m_queue_ = new block_queue<std::string>(m_max_queue_size);
        pthread_t tid;
        pthread_create(&tid, nullptr, async_write_log, nullptr);
        return true;
    }
}

void Log::write_log(int level, const char* format, ...) {

}



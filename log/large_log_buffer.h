//
// Created by Hanhong Wong on 2026/5/19.
//

#ifndef MYWEBSERVER_LARGE_LOG_BUFFER_H
#define MYWEBSERVER_LARGE_LOG_BUFFER_H
#include <cstring>
#include <cstddef>

constexpr int LARGE_BUFFER_SIZE = 4 * 1000 * 1000;

class LargeLogBuffer {
public:
    LargeLogBuffer() : cur_(data_) {}

    void append(const char* item, size_t len) {
        if (avail() > len) {
            memcpy(cur_, item, len);
            cur_ += len;
        }
    }

    size_t avail() const {
        return end() - cur_;
    }

    size_t size() const {
        return cur_ - data_;
    }

    void reset() {
        cur_ = data_;
    }

    void bzero() {
        memset(data_, 0, sizeof(data_));
    }

    const char* data() const {
        return data_;
    }

private:
    const char* end() const { return data_ + sizeof(data_); }
    char  data_[LARGE_BUFFER_SIZE];
    char* cur_;
};

#endif //MYWEBSERVER_LARGE_LOG_BUFFER_H
//
// Created by Hanhong Wong on 2026/5/25.
//

#ifndef MYWEBSERVER_HTTPCONNECT_H
#define MYWEBSERVER_HTTPCONNECT_H
#include <sys/epoll.h>
#include <sys/socket.h>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <strings.h>
#include <cerrno>
#include <sys/stat.h>
#include <stdarg.h>

#include "../log/log.h"
#include "../timer/timer_lst.h"

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "HttpContext.h"
#include "../timer/timer_policy.h"

#ifdef __linux__
#include <sys/sendfile.h>
#endif

// 平台无关包装，消除 IDE 对 sendfile 签名的误报
inline ssize_t sendfile_wrap(int out_fd, int in_fd, off_t* offset, ssize_t count) {
#ifdef __linux__
    return sendfile(out_fd, in_fd, offset, count);
#else
    off_t len = count;
    int ret = sendfile(in_fd, out_fd, *offset, &len, nullptr, 0);
    if (ret == 0) { *offset += len; return len; }
    return -1;
#endif
}

class HttpConnect {
public:
    constexpr static int READ_BUFFER_SIZE = 2048;
    constexpr static int WRITE_BUFFER_SIZE = 1024;

private:
    int m_client_fd_ = {};
    bool m_linger_ = {};
    ssize_t m_bytes_to_send_ = {};
    ssize_t m_bytes_have_sent_ = {};
    char m_read_buffer_[READ_BUFFER_SIZE] = {};
    char m_write_buffer_[WRITE_BUFFER_SIZE] = {};

    //send file
    SEND_STATE m_send_state_ = {};
    int m_file_fd_ = {};
    std::string m_file_path_ = {};
    struct stat m_file_stat_ = {};
    ssize_t m_file_bytes_left_ = {};
    off_t m_file_offset_ = {};

    //util
    Util m_util_ = {};

    HttpResponse m_response_ = {};
    HttpContext m_context_ = {};

    void close_conn() {
        m_util_.delfd(m_epollfd_, m_client_fd_);
        if (m_client_fd_ >= 0) {
            ::close(m_client_fd_);
            m_client_fd_ = -1;
        }
    }

public:
    inline static int m_epollfd_ = -1;
    int m_state_ = {}; //0:read, 1:write
public:
    HttpConnect() {
    };

    ~HttpConnect() {
    }

    void init(int m_client_fd) {
        m_client_fd_ = m_client_fd;
        m_util_.addfd(m_epollfd_, m_client_fd_, true);
        init();
    }

    void init() {
        m_linger_ = {};
        m_response_.bind(m_write_buffer_, WRITE_BUFFER_SIZE);
        m_context_.init();
        m_response_.init();
        m_send_state_ = SEND_STATE::SEND_HEAD;
        m_bytes_to_send_ = 0;
        m_bytes_have_sent_ = 0;
        m_file_bytes_left_ = 0;
        m_file_fd_ = -1;
    }

    bool read() {
        auto& read_idx = m_context_.m_read_idx_;
        while (true) {
            ssize_t n = ::read(m_client_fd_, m_read_buffer_ + read_idx,
                               READ_BUFFER_SIZE - read_idx);
            if (n < 0) {
                if (errno == EINTR) {
                    continue; // interrupted, retry
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // No more data to read right now
                }
                perror("read");
                return false;
            }
            if (n == 0) {
                // Client closed the connection
                return false;
            }
            read_idx += n;
        }
        return true;
    }

    StatusCode process_read() {
        auto ret = m_context_.parse_request(m_read_buffer_);
        return ret == StatusCode::SUCCESS ? do_request() : ret;
    }

    StatusCode do_request() {
        m_file_path_ = m_context_.get_request().get_path();

        // 目录路径默认返回 index.html
        if (stat(m_file_path_.c_str(), &m_file_stat_) < 0 || S_ISDIR(m_file_stat_.st_mode)) {
            if (m_file_path_.back() != '/') {
                m_file_path_ += '/';
            }
            m_file_path_ += "index.html";
            if (stat(m_file_path_.c_str(), &m_file_stat_) < 0 || !S_ISREG(m_file_stat_.st_mode)) {
                return StatusCode::NOT_FOUND;
            }
        }

        m_file_fd_ = open(m_file_path_.c_str(), O_RDONLY);
        if (m_file_fd_ < 0) {
            return StatusCode::NOT_FOUND;
        }
        m_file_offset_ = 0;
        m_file_bytes_left_ = m_file_stat_.st_size;
        return StatusCode::OK;
    }

    bool process_write(StatusCode code) {
        // 仅 OK 响应的 body 来自文件，其余响应的 body 已含在缓冲区中
        size_t file_size = (code == StatusCode::OK) ? m_file_stat_.st_size : 0;
        if (!m_response_.build_response(code, file_size, m_linger_)) {
            return false;
        }
        m_bytes_to_send_ = m_response_.write_idx() + file_size;
        m_bytes_have_sent_ = 0;
        m_send_state_ = SEND_STATE::SEND_HEAD;
        return true;
    }

    bool write() {
        ssize_t temp = 0;
        size_t head_len = m_response_.write_idx();

        while (true) {
            if (m_send_state_ == SEND_STATE::SEND_HEAD) {
                temp = ::write(m_client_fd_,
                               m_write_buffer_ + m_bytes_have_sent_,
                               head_len - m_bytes_have_sent_);

                if (temp == -1) {
                    if (errno == EAGAIN) {
                        m_util_.modfd(m_epollfd_, m_client_fd_, EPOLLOUT);
                        return true;
                    }
                    close(m_client_fd_);
                    m_client_fd_ = -1;
                    return false;
                }

                m_bytes_have_sent_ += temp;
                m_bytes_to_send_ -= temp;

                if (static_cast<size_t>(m_bytes_have_sent_) >= head_len) {
                    m_send_state_ = SEND_STATE::SEND_FILE;
                }
            }
            else if (m_send_state_ == SEND_STATE::SEND_FILE && m_file_bytes_left_ > 0) {
                temp = sendfile_wrap(m_client_fd_, m_file_fd_, &m_file_offset_, m_file_bytes_left_);

                if (temp == -1) {
                    if (errno == EAGAIN) {
                        m_util_.modfd(m_epollfd_, m_client_fd_, EPOLLOUT);
                        return true;
                    }
                    close(m_client_fd_);
                    m_client_fd_ = -1;
                    return false;
                }

                m_bytes_to_send_ -= temp;
            }

            if (m_bytes_to_send_ <= 0) {
                close(m_file_fd_);
                m_file_fd_ = -1;
                m_util_.modfd(m_epollfd_, m_client_fd_, EPOLLIN);
                if (m_linger_) {
                    init();
                    return true;
                }
                return false;
            }
        }
    }

    // process may modify the fd state (close it), so it cannot be const.
    void process() {
        if (m_client_fd_ < 0) {
            std::cerr << "Invalid client fd" << std::endl;
            return;
        }

        auto read_ret = process_read();
        if (read_ret == StatusCode::UNKNOWN) {
            m_util_.modfd(m_epollfd_, m_client_fd_, EPOLLIN);
            return;
        }

        // 根据 Connection 头和 HTTP 版本决定是否 keep-alive
        auto conn_hdr = m_context_.m_request_.get_headers("Connection");
        if (!conn_hdr.empty()) {
            m_linger_ = (conn_hdr == "keep-alive");
        } else {
            // HTTP/1.1 默认 keep-alive；HTTP/1.0 默认 close
            m_linger_ = (m_context_.m_request_.get_version() == "HTTP/1.1");
        }

        auto write_ret = process_write(read_ret);
        if (!write_ret) {
            close_conn();
            return;
        }
        m_util_.modfd(m_epollfd_, m_client_fd_, EPOLLOUT);
    }
};
#endif //MYWEBSERVER_HTTPCONNECT_H

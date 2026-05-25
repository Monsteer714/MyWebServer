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
    int m_TRIGMode_ = {}; // LT : 0, ET : 1;
    bool m_linger_ = {};
    ssize_t m_read_idx_ = {}; //read_buffer下一位填充的起始序号，也即read_buffer可读数据最后一位的下一位
    ssize_t m_write_idx_ = {};
    ssize_t m_check_idx_ = {};
    ssize_t m_start_line_ = {};
    ssize_t m_content_length_ = {};
    ssize_t m_bytes_to_send_ = {};
    ssize_t m_bytes_have_sent_ = {};
    std::string m_host_ = {};
    std::string m_path = {};
    std::string m_version = {};
    char* m_index_path_ = (char*)"./root/index.html";
    METHOD m_method_ = {};
    CHECK_STATE m_check_state_ = {};
    char m_read_buffer_[READ_BUFFER_SIZE] = {};
    char m_write_buffer_[WRITE_BUFFER_SIZE] = {};

    //send file
    SEND_STATE m_send_state_ = {};
    int m_file_fd_ = {};
    char* m_file_ = {};
    char* m_file_address = {};
    struct stat m_file_stat_ = {};
    ssize_t m_file_bytes_left_ = {};
    off_t m_file_offset_ = {};

    //util
    Util m_util_ = {};

    HttpRequest m_request_ = {};
    HttpResponse m_response_ = {};
    HttpContext m_context_ = {};

    void close_fd(int& fd) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    void close_conn() {
        m_util_.delfd(m_epollfd_, m_client_fd_);
        close_fd(m_client_fd_);
    }

    char* get_line() {
        return m_read_buffer_ + m_start_line_;
    }

public:
    inline static int m_epollfd_ = -1;
    int m_state_ = {}; //0:read, 1:write
    int m_error_flag_ = {};
    int m_op_finish_flag_ = {};

public:
    http_conn() {
    };

    ~http_conn() {
    }

    void init(int m_client_fd, int m_TRIGMode) {
        m_client_fd_ = m_client_fd;
        m_TRIGMode_ = m_TRIGMode;
        m_util_.addfd(m_epollfd_, m_client_fd_, true);
        init();
    }

    void init() {
        m_read_idx_ = 0;
        m_write_idx_ = 0;
        m_check_idx_ = 0;
        m_start_line_ = 0;
        m_content_length_ = 0;
        m_bytes_to_send_ = 0;
        m_bytes_have_sent_ = 0;
        m_file_fd_ = -1;
        m_file_bytes_left_ = 0;
        m_file_offset_ = {};
        m_host_.clear();
        m_path.clear();
        m_version.clear();
        m_method_ = GET;
        m_check_state_ = CHECK_REQUEST;
        m_send_state_ = SEND_HEAD;
        m_linger_ = false;
        m_state_ = 0;
    }

    bool read_once() {
        if (m_read_idx_ >= READ_BUFFER_SIZE) {
            return false;
        }
        while (true) {
            ssize_t n = ::read(m_client_fd_, m_read_buffer_ + m_read_idx_, READ_BUFFER_SIZE - m_read_idx_);
            if (n < 0) {
                if (errno == EINTR) {
                    continue; // interrupted, retry
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // No more data to read right now
                }
                else {
                    perror("read");
                    return false;
                }
            }
            else if (n == 0) {
                // Client closed the connection
                return false;
            }
            m_read_idx_ += n;
        }
        return true;
    }

    StatusCode process_read() {
        return m_context_.parse_request(m_read_buffer_);
    }

    HTTP_CODE do_request() {
        m_file_ = m_index_path_;
        stat(m_file_, &m_file_stat_);
        m_file_fd_ = open(m_file_, O_RDONLY);
        if (m_file_fd_ < 0) {
            return BAD_REQUEST;
        }
        m_file_offset_ = 0;
        m_file_bytes_left_ = m_file_stat_.st_size;
        return FILE_REQUEST;
    }

    bool add_response(const char* format, ...) {
        va_list args;
        va_start(args, format);

        int len = vsnprintf(m_write_buffer_ + m_write_idx_, WRITE_BUFFER_SIZE - m_write_idx_ - 1, format, args);
        if (len >= WRITE_BUFFER_SIZE - m_write_idx_ - 1) {
            va_end(args);
            return false;
        }

        m_write_idx_ += len;

        va_end(args);
        return true;
    }

    bool add_status_line(int status, const char* title) {
        return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
    }

    bool add_headers(const int& content_length) {
        return add_content_length(content_length) && add_content_type()
            && add_connection() && add_blank_line();
    }

    bool add_content_type() {
        return add_response("Content-Type:%s\r\n", "text/html");
    }

    bool add_content_length(const int& content_length) {
        return add_response("Content-Length:%d\r\n", content_length);
    }

    bool add_connection() {
        return add_response("Connection:%s\r\n", m_linger_ ? "keep-alive" : "close");
    }

    bool add_blank_line() {
        return add_response("\r\n");
    }

    bool add_content(const char* content) {
        return add_response("%s", content);
    }

    bool process_write(HTTP_CODE http_code) {
        switch (http_code) {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(std::strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(std::strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            if (m_file_stat_.st_size != 0) {
                add_headers(m_file_stat_.st_size);
                m_bytes_to_send_ = m_write_idx_ + m_file_stat_.st_size;
                return true;
            }
            else {
                const char* body = "<html><body></body></html>";
                add_headers(std::strlen(body));
                if (!add_content(body)) {
                    return false;
                }
            }
            break;
        default:
            return false;
            break;
        }
        m_bytes_to_send_ = m_write_idx_;
        return true;
    }

    bool write() {
        ssize_t temp = 0;
        while (true) {
            if (m_send_state_ == SEND_HEAD) {
                temp = ::write(m_client_fd_, m_write_buffer_ + m_bytes_have_sent_, m_write_idx_ - m_bytes_have_sent_);

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

                if (m_bytes_have_sent_ >= m_write_idx_) {
                    m_send_state_ = SEND_FILE;
                }
            }
            else if (m_send_state_ == SEND_FILE && m_file_bytes_left_ > 0) {
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
                else {
                    return false;
                }
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
        if (read_ret == NO_REQUEST) {
            m_util_.modfd(m_epollfd_, m_client_fd_, EPOLLIN);
            return;
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

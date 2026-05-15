//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_HTTP_CONN_H
#define MYWEBSERVER_HTTP_CONN_H

#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <strings.h>
#include <cerrno>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <stdarg.h>

#include "../log/log.h"
#include "../timer/timer_lst.h"

inline const char* ok_200_title = "OK";
inline const char* error_400_title = "Bad Request";
inline const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
inline const char* error_403_title = "Forbidden";
inline const char* error_403_form = "You do not have permission to get file form this server.\n";
inline const char* error_404_title = "Not Found";
inline const char* error_404_form = "The requested file was not found on this server.\n";
inline const char* error_500_title = "Internal Error";
inline const char* error_500_form = "There was an unusual problem serving the request file.\n";

class http_conn {
public:
    constexpr static int READ_BUFFER_SIZE = 2048;
    constexpr static int WRITE_BUFFER_SIZE = 1024;

    enum METHOD {
        GET = 0,
        POST,
    };

    enum CHECK_STATE {
        CHECK_REQUEST = 0,
        CHECK_HEADER,
        CHECK_CONTENT,
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        BAD_REQUEST,
        GET_REQUEST,
        INTERNAL_ERROR,
        FILE_REQUEST,
    };

    enum LINE_STATE {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN,
    };

private:
    int m_client_fd_ = {};
    int m_TRIGMode_ = {};// LT : 0, ET : 1;
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

    //mmap
    char* m_file_ = {};
    char* m_file_address = {};
    struct stat m_file_stat_ = {};
    struct iovec m_iovec_[2] = {};
    int m_iovec_count_ = {};

    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void addfd(int epfd, int fd, bool oneshot) {
        epoll_event event = {};
        event.data.fd = fd;

        if (m_TRIGMode_ == 0) {
            event.events = EPOLLIN | EPOLLRDHUP;
        } else {
            event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        }

        if (oneshot) {
            event.events |= EPOLLONESHOT;
        }

        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
        setNonBlocking(fd);
    }

    void removefd(int epollfd, int fd) {
        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    }

    // Close the client fd if it's valid and mark it as closed.
    void close_fd(int &fd) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    bool modfd(int epfd, int fd, int ev) {
        epoll_event event = {};
        event.data.fd = fd;

        if (m_TRIGMode_ == 0) {
            event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
        } else {
            event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
        }

        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
        return true;
    }

    void close_conn() {
        removefd(m_epollfd_, m_client_fd_);
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
        addfd(m_epollfd_, m_client_fd_, true);
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
        m_host_.clear();
        m_path.clear();
        m_version.clear();
        m_method_ = GET;
        m_check_state_ = CHECK_REQUEST;
        m_linger_ = false;
        m_iovec_count_ = 0;
        m_iovec_[0] = {};
        m_iovec_[1] = {};
        m_state_ = 0;

        memset(m_read_buffer_, '\0', sizeof(m_read_buffer_));
        memset(m_write_buffer_, '\0', sizeof(m_write_buffer_));
    }

    bool read_once() {
        if (m_read_idx_ >= READ_BUFFER_SIZE) {
            return false;
        }
        if (m_TRIGMode_ == 0) { //LT
            ssize_t n = ::read(m_client_fd_, m_read_buffer_ + m_read_idx_, READ_BUFFER_SIZE - m_read_idx_);
            if (n <= 0) {
                return false;
            }
            m_read_idx_ += n;
            return true;
        } else { //ET
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
    }

    LINE_STATE parse_line() {
        char temp = {};
        for (; m_check_idx_ < m_read_idx_; ++m_check_idx_) {
            temp = m_read_buffer_[m_check_idx_];
            if (temp == '\r') {
                if (m_check_idx_ + 1 >= m_read_idx_) {
                    return LINE_OPEN;
                }
                else if (m_read_buffer_[m_check_idx_ + 1] == '\n') {
                    m_read_buffer_[m_check_idx_++] = '\0';
                    m_read_buffer_[m_check_idx_++] = '\0';
                    return LINE_OK;
                }
                return LINE_BAD;
            }
            else if (temp == '\n') {
                if (m_check_idx_ > 0 && m_read_buffer_[m_check_idx_ - 1] == '\r') {
                    m_read_buffer_[m_check_idx_ - 1] = '\0';
                    m_read_buffer_[m_check_idx_++] = '\0';
                    return LINE_OK;
                }
                return LINE_BAD;
            }
        }
        return LINE_OPEN;
    }

    HTTP_CODE do_request() {
        m_file_ = m_index_path_;
        stat(m_file_, &m_file_stat_);
        int fd = open(m_file_, O_RDONLY);
        m_file_address = (char*)mmap(0, m_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        return FILE_REQUEST;
    }

    void unmap() {
        if (m_file_address) {
            munmap(m_file_address, m_file_stat_.st_size);
            m_file_address = 0;
        }
    }

    HTTP_CODE process_read() {
        auto line_state = LINE_OK;
        auto ret = NO_REQUEST;
        char* text = {};
        while ((line_state = parse_line()) == LINE_OK) {
            text = get_line();
            m_start_line_ = m_check_idx_;
            switch (m_check_state_) {
            case CHECK_REQUEST:
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            case CHECK_HEADER:
                ret = parse_header_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            case CHECK_CONTENT:
                ret = parse_content_line(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_state = LINE_OPEN;
                break;
            default:
                return INTERNAL_ERROR;
                break;
            }
        }
        return NO_REQUEST;
    }

    HTTP_CODE parse_request_line(char* text) {
        std::string method, path, version;
        std::istringstream ss(text);
        ss >> method >> path >> version;
        if (method == "GET") {
            m_method_ = GET;
        }
        else if (method == "POST") {
            m_method_ = POST;
        }
        else {
            return BAD_REQUEST;
        }
        m_path = path;
        m_version = version;
        m_check_state_ = CHECK_HEADER;
        return NO_REQUEST;
    }

    HTTP_CODE parse_header_line(char* text) {
        if (text[0] == '\0') {
            if (m_content_length_ > 0) {
                m_check_state_ = CHECK_CONTENT;
                return NO_REQUEST;
            }
            return GET_REQUEST;
        }
        if (strncasecmp(text, "Content-Length:", 15) == 0) {
            text += 15;
            text += strspn(text, " \t");
            m_content_length_ = atol(text);
        }
        else if (strncasecmp(text, "Host:", 5) == 0) {
            text += 5;
            text += strspn(text, " \t");
            m_host_ = text;
        }
        else if (strncasecmp(text, "Connection:", 11) == 0) {
            text += 11;
            text += strspn(text, " \t");
            std::string connection = text;
            if (strncasecmp(connection.c_str(), "keep-alive", 10) == 0) {
                m_linger_ = true;
            }
        }
        else {
        }
        return NO_REQUEST;
    }

    HTTP_CODE parse_content_line(char* text) {
        if (m_read_idx_ >= m_content_length_ + m_check_idx_) {
            text[m_content_length_] = '\0';
            return GET_REQUEST;
        }
        return NO_REQUEST;
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

    bool add_headers(const int &content_length) {
        return add_content_length(content_length) && add_content_type()
                && add_connection() && add_blank_line();
    }

    bool add_content_type(){
        return add_response("Content-Type:%s\r\n", "text/html");
    }

    bool add_content_length(const int &content_length) {
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
                m_iovec_[0].iov_base = m_write_buffer_;
                m_iovec_[0].iov_len = m_write_idx_;
                m_iovec_[1].iov_base = m_file_address;
                m_iovec_[1].iov_len = m_file_stat_.st_size;
                m_iovec_count_ = 2;
                m_bytes_to_send_ = m_write_idx_ + m_file_stat_.st_size;
                return true;
            } else {
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
        m_iovec_[0].iov_base = m_write_buffer_;
        m_iovec_[0].iov_len = m_write_idx_;
        m_iovec_count_ = 1;
        m_bytes_to_send_ = m_write_idx_;
        LOG_INFO("%s", "processed_write");
        return true;
    }

    bool write() {
        int temp = 0;
        while (true) {
            temp = writev(m_client_fd_, m_iovec_, m_iovec_count_);

            if (temp == -1) {
                if (errno == EAGAIN) {
                    modfd(m_epollfd_, m_client_fd_, EPOLLOUT);
                    return true;
                }
                unmap();
                return false;
            }

            m_bytes_to_send_ -= temp;
            m_bytes_have_sent_ += temp;

            if (m_bytes_have_sent_ >= m_write_idx_) {
                m_iovec_[0].iov_len = 0;
                m_iovec_[1].iov_base = m_file_address + (m_bytes_have_sent_ - m_write_idx_);
                m_iovec_[1].iov_len = m_bytes_to_send_;
            } else {
                m_iovec_[0].iov_base = m_write_buffer_ + m_bytes_have_sent_;
                m_iovec_[0].iov_len = m_write_idx_ - m_bytes_have_sent_;
            }

            if (m_bytes_to_send_ <= 0) {
                unmap();
                modfd(m_epollfd_, m_client_fd_, EPOLLIN);
                if (m_linger_) {
                    init();
                    return true;
                } else {
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
            modfd(m_epollfd_, m_client_fd_, EPOLLIN);
            return;
        }
        auto write_ret = process_write(read_ret);
        if (!write_ret) {
            LOG_INFO("%s", "process write error");
            close_conn();
            return;
        }
        modfd(m_epollfd_, m_client_fd_, EPOLLOUT);
    }
};

#endif //MYWEBSERVER_HTTP_CONN_H

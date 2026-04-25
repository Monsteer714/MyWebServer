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
#include <cerrno>

class http_conn {
public:
    constexpr static int READ_BUFFER_SIZE = 1024;
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
    };

    enum LINE_STATE {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN,
    };

private:
    int m_client_fd_ = {};
    ssize_t m_read_idx_ = {}; //read_buffer下一位填充的起始序号，也即read_buffer可读数据最后一位的下一位
    ssize_t m_check_idx_ = {};
    ssize_t m_start_line_ = {};
    std::string m_path = {};
    std::string m_version = {};
    METHOD m_method_ = {};
    CHECK_STATE m_check_state_ = {};
    char m_read_buffer_[READ_BUFFER_SIZE] = {};

    // Close the client fd if it's valid and mark it as closed.
    void close_fd() {
        if (m_client_fd_ >= 0) {
            ::close(m_client_fd_);
            m_client_fd_ = -1;
        }
    }

    char* get_line() {
        return m_read_buffer_ + m_start_line_;
    }
public:
    static int m_epollfd_;
public:
    http_conn(int client_fd) {
        m_client_fd_ = client_fd;
    };

    ~http_conn() {
        close_fd();
    }

    static std::string read_file(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    bool modfd(int epfd, int fd, int ev) {
        epoll_event ev;
        ev.data.fd = fd;

        ev.events = ev | EPOLLET | EPOLLONESHOT;

        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }

    bool read_once() {
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
                    close_fd();
                    return false;
                }
            }
            else if (n == 0) {
                // Client closed the connection
                close_fd();
                return false;
            }
            m_read_idx_ += n;
        }
        return true;
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
                break;
            case CHECK_CONTENT:
                ret = parse_content_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            default:

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
        } else if (method == "POST") {
            m_method_ = POST;
        } else {
            return BAD_REQUEST;
        }
        m_path = path;
        m_version = version;
        m_check_state_ = CHECK_HEADER;
        if (m_method_ == GET) {
            return GET_REQUEST;
        }
        return NO_REQUEST;
    }

    HTTP_CODE parse_header_line(char* text) {
        return HTTP_CODE{};
    }

    HTTP_CODE parse_content_line(char* text) {
        return HTTP_CODE{};
    }

    void process_write() {

    }

    // process may modify the fd state (close it), so it cannot be const.
    void process() {
        if (m_client_fd_ < 0) {
            std::cerr << "Invalid client fd" << std::endl;
            return;
        }

        read_once();
        if (process_read() == NO_REQUEST) {
            modfd(m_epollfd_, m_client_fd_, EPOLLIN);
            return;
        }
        std::cout << m_path << " " << m_version << std::endl;
        // Ensure all data is flushed before closing the connection
        if (m_client_fd_ >= 0) {
            shutdown(m_client_fd_, SHUT_WR);
        }
        close_fd();
    }
};

#endif //MYWEBSERVER_HTTP_CONN_H

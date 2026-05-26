//
// Created by Hanhong Wong on 2026/5/24.
//

#ifndef MYWEBSERVER_HTTPCONTEXT_H
#define MYWEBSERVER_HTTPCONTEXT_H
#include "HttpResponse.h"
#include "HttpRequest.h"

enum class CHECK_STATE {
    CHECK_REQUEST = 0,
    CHECK_HEADER,
    CHECK_CONTENT,
};

enum class LINE_STATE {
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN,
};

class HttpContext {
public:
    CHECK_STATE m_check_state_ = {};

    HttpRequest m_request_ = {};

    ssize_t m_check_idx_ = {};
    ssize_t m_read_idx_ = {};
    ssize_t m_start_line_ = {};
    ssize_t m_remain_content_length_ = {};
public:
    char* get_line(char* buffer) {
        return buffer + m_start_line_;
    }

    LINE_STATE parse_line(char* buffer) {
        char temp = {};
        for (; m_check_idx_ < m_read_idx_; ++m_check_idx_) {
            temp = buffer[m_check_idx_];
            if (temp == '\r') {
                if (m_check_idx_ + 1 >= m_read_idx_) {
                    return LINE_STATE::LINE_OPEN;
                }
                else if (buffer[m_check_idx_ + 1] == '\n') {
                    m_check_idx_ += 2;
                    return LINE_STATE::LINE_OK;
                }
                return LINE_STATE::LINE_BAD;
            }
            else if (temp == '\n') {
                if (m_check_idx_ > 0 && buffer[m_check_idx_ - 1] == '\r') {
                    m_check_idx_ += 1;
                    return LINE_STATE::LINE_OK;
                }
                return LINE_STATE::LINE_BAD;
            }
            if (m_check_state_ == CHECK_STATE::CHECK_CONTENT) {
                m_remain_content_length_--;
            }
        }
        return LINE_STATE::LINE_OPEN;
    }

    StatusCode parse_request_line(char* text) {
        int start = 0, end = 0;
        for (int cnt = 0; cnt < 3; cnt++) {
            while (text[end] != '\r') {
                end++;
                if (text[end] == ' ') {
                    break;
                }
            }
            if (cnt == 0) {
                m_request_.set_method(text + start, text + end);
            } else if (cnt == 1) {
                m_request_.set_path(text + start, text + end);
            } else {
                m_request_.set_version(text + start, text + end);
            }
            start = end + 1;
        }
        m_check_state_ = CHECK_STATE::CHECK_HEADER;
        return StatusCode::UNKNOWN;
    }

    StatusCode parse_header_line(char* text) {
        if (text[0] == '\r' && text[1] == '\n') {
            if (m_request_.get_content_length() > 0) {
                m_remain_content_length_ = m_request_.get_content_length();
                m_check_state_ = CHECK_STATE::CHECK_CONTENT;
                return StatusCode::UNKNOWN;
            }
            return StatusCode::SUCCESS;
        }

        int end = 0;
        while (text[end] != '\r') {
            end++;
        }
        m_request_.set_headers(text, text + end);

        if (!m_request_.get_headers("Content-Length").empty()) {
            ssize_t content_length = stoi(std::string(m_request_.get_headers("Content-Length")));
            m_request_.set_content_length(content_length);
        }
        return StatusCode::UNKNOWN;
    }

    StatusCode parse_content_line(char* text) {
        if (m_read_idx_ >= m_remain_content_length_ + m_check_idx_) {
            m_request_.set_content(text, text + m_request_.get_content_length());
            return StatusCode::SUCCESS;
        }
        return StatusCode::UNKNOWN;
    }

    StatusCode parse_request(char* buffer) {
        auto line_state = LINE_STATE::LINE_OK;
        auto ret = StatusCode::UNKNOWN;
        char* text = {};
        while (m_check_state_ == CHECK_STATE::CHECK_CONTENT && line_state == LINE_STATE::LINE_OK ||
            (line_state = parse_line(buffer)) == LINE_STATE::LINE_OK) {
            text = get_line(buffer);
            m_start_line_ = m_check_idx_;
            switch (m_check_state_) {
            case CHECK_STATE::CHECK_REQUEST:
                ret = parse_request_line(text);
                if (ret == StatusCode::BAD_REQUEST) {
                    return StatusCode::BAD_REQUEST;
                }
                break;
            case CHECK_STATE::CHECK_HEADER:
                ret = parse_header_line(text);
                if (ret == StatusCode::BAD_REQUEST) {
                    return StatusCode::BAD_REQUEST;
                }
                if (ret == StatusCode::SUCCESS) {
                    //return do_request();
                }
                break;
            case CHECK_STATE::CHECK_CONTENT:
                ret = parse_content_line(text);
                if (ret == StatusCode::SUCCESS) {
                    //return do_request();
                }
                line_state = LINE_STATE::LINE_OPEN;
                break;
            default:
                return StatusCode::INTERNAL_ERROR;
                break;
            }
        }
        return StatusCode::UNKNOWN;
    }

    HttpRequest get_request() {
        return m_request_;
    }
};

#endif //MYWEBSERVER_HTTPCONTEXT_H

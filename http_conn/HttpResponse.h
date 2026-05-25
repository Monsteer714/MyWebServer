//
// Created by Hanhong Wong on 2026/5/24.
//

#ifndef MYWEBSERVER_HTTPRESPONSE_H
#define MYWEBSERVER_HTTPRESPONSE_H

enum class StatusCode {
    UNKNOWN = 0,
    SUCCESS = 1,

    OK = 200,

    BAD_REQUEST = 400,
    FORBIDDEN = 403,
    NOT_FOUND = 404,

    INTERNAL_ERROR = 500,
    BAD_GATEWAY = 502,
};

enum class SEND_STATE {
    SEND_HEAD = 0,
    SEND_FILE,
};

class HttpResponse {
private:
    StatusCode m_status_code_ = {};

    std::string_view m_version_ = {};

    std::string_view m_status_message_ = {};

    std::unordered_map<std::string_view, std::string_view> m_headers_ = {};

    std::string_view m_body_ = {};

    ssize_t m_body_length_ = {};
public:
    HttpResponse() = default;

//    bool add_response(const char* format, ...) {
//        va_list args;
//        va_start(args, format);
//
//        int len = vsnprintf(m_write_buffer_ + m_write_idx_, WRITE_BUFFER_SIZE - m_write_idx_ - 1, format, args);
//        if (len >= WRITE_BUFFER_SIZE - m_write_idx_ - 1) {
//            va_end(args);
//            return false;
//        }
//
//        m_write_idx_ += len;
//
//        va_end(args);
//        return true;
//    }
//
//    bool add_status_line(int status, const char* title) {
//        return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
//    }
//
//    bool add_headers(const int& content_length) {
//        return add_content_length(content_length) && add_content_type()
//            && add_connection() && add_blank_line();
//    }
//
//    bool add_content_type() {
//        return add_response("Content-Type:%s\r\n", "text/html");
//    }
//
//    bool add_content_length(const int& content_length) {
//        return add_response("Content-Length:%d\r\n", content_length);
//    }
//
//    bool add_connection() {
//        //return add_response("Connection:%s\r\n", m_linger_ ? "keep-alive" : "close");
//    }
//
//    bool add_blank_line() {
//        return add_response("\r\n");
//    }
//
//    bool add_content(const char* content) {
//        return add_response("%s", content);
//    }
};

#endif //MYWEBSERVER_HTTPRESPONSE_H

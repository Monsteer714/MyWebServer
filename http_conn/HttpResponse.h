//
// Created by Hanhong Wong on 2026/5/24.
//

#ifndef MYWEBSERVER_HTTPRESPONSE_H
#define MYWEBSERVER_HTTPRESPONSE_H
#include <cstring>
#include <unordered_map>
#include <string_view>
#include <string>
#include <cstdarg>
#include <cstdio>

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

inline const char* ok_200_title = "OK";
inline const char* error_400_title = "Bad Request";
inline const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
inline const char* error_403_title = "Forbidden";
inline const char* error_403_form = "You do not have permission to get file from this server.\n";
inline const char* error_404_title = "Not Found";
inline const char* error_404_form = "The requested file was not found on this server.\n";
inline const char* error_500_title = "Internal Error";
inline const char* error_500_form = "There was an unusual problem serving the request file.\n";

// HttpResponse 是一个纯格式化器，不依赖 HttpConnect 的任何内部状态。
// 通过 bind() 绑定外部缓冲区，build_response() 将 HTTP 响应头写入其中。
class HttpResponse {
private:
    char* m_buffer_ = nullptr;
    int m_buffer_size_ = 0;
    int m_write_idx_ = 0;

    bool append(const char* format, ...) {
        va_list args;
        va_start(args, format);
        int len = vsnprintf(m_buffer_ + m_write_idx_,
                            m_buffer_size_ - m_write_idx_ - 1,
                            format, args);
        va_end(args);
        if (len < 0 || len >= m_buffer_size_ - m_write_idx_ - 1) {
            return false;
        }
        m_write_idx_ += len;
        return true;
    }

    bool add_status_line(int status, const char* title) {
        return append("%s %d %s\r\n", "HTTP/1.1", status, title);
    }

    bool add_headers(size_t content_length, bool keep_alive) {
        return append("Content-Length:%zu\r\n", content_length)
            && append("Content-Type:%s\r\n", "text/html")
            && append("Connection:%s\r\n", keep_alive ? "keep-alive" : "close")
            && append("\r\n");
    }

    bool add_content(const char* content) {
        return append("%s", content);
    }

public:
    HttpResponse() = default;

    // 绑定外部缓冲区（由 HttpConnect 拥有）
    void bind(char* buffer, int size) {
        m_buffer_ = buffer;
        m_buffer_size_ = size;
        m_write_idx_ = 0;
    }

    void init() {
        m_write_idx_ = 0;
    }


    bool build_response(StatusCode code, size_t content_length, bool keep_alive) {
        m_write_idx_ = 0;

        switch (code) {
        case StatusCode::INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(std::strlen(error_500_form), keep_alive);
            return add_content(error_500_form);
        case StatusCode::BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(std::strlen(error_400_form), keep_alive);
            return add_content(error_400_form);
        case StatusCode::FORBIDDEN:
            add_status_line(403, error_403_title);
            add_headers(std::strlen(error_403_form), keep_alive);
            return add_content(error_403_form);
        case StatusCode::NOT_FOUND:
            add_status_line(404, error_404_title);
            add_headers(std::strlen(error_404_form), keep_alive);
            return add_content(error_404_form);
        case StatusCode::OK:
            add_status_line(200, ok_200_title);
            if (content_length > 0) {
                return add_headers(content_length, keep_alive);
            } else {
                const char* empty_body = "<html><body></body></html>";
                add_headers(std::strlen(empty_body), keep_alive);
                return add_content(empty_body);
            }
        default:
            return false;
        }
    }

    int get_write_idx() const { return m_write_idx_; }

    // --- 用于逐步构建响应的底层接口（保留给未来扩展，如 WebSocket 握手） ---
    void reset() { m_write_idx_ = 0; }
    bool write_status(int status, const char* title) { return add_status_line(status, title); }
    bool write_header(const char* key, const char* value) {
        return append("%s:%s\r\n", key, value);
    }
    bool write_blank_line() { return append("\r\n"); }
    bool write_body(const char* body) { return add_content(body); }
};

#endif //MYWEBSERVER_HTTPRESPONSE_H
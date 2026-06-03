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
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../util/types.h"
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
    std::string m_mime_type_;

    // 文件 body — handler 可指定磁盘文件作为响应体，HttpConnect 通过 sendfile 发送
    // fd 所有权：set_file_body() 创建 → release_file_body_fd() 移交给 HttpConnect
    int    m_file_body_fd_   = -1;
    size_t m_file_body_size_ = 0;

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
            && append("Content-Type:%s\r\n", m_mime_type_.c_str())
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
        // 清理上一次请求可能残留的文件 body fd（如 handler 设了 body 但请求异常中断）
        if (m_file_body_fd_ >= 0) {
            close(m_file_body_fd_);
            m_file_body_fd_ = -1;
        }
        m_file_body_size_ = 0;
    }

    inline const void set_mime_type(const std::string& path) {
        static const std::unordered_map<std::string, std::string> mime_types = {
            {".html", "text/html"},
            {".css", "text/css"},
            {".js", "application/javascript"},
            {".json", "application/json"},
            {".png", "image/png"},
            {".jpg", "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif", "imgae/gif"},
            {".svg", "image/svg+xml"},
        };

        auto pos = path.rfind(".");
        if (pos == std::string::npos) {
            static const std::string fallback = "application/octet-stream";
            m_mime_type_ = fallback;
            return;
        }

        std::string ext = path.substr(pos);
        auto it = mime_types.find(ext);
        if (it != mime_types.end()) {
            m_mime_type_ = it->second;
            return;
        }
        static const std::string fallback = "application/octet-stream";
        m_mime_type_ = fallback;
        return;
    }

    const std::string& get_mime_type() const { return m_mime_type_; }

    // =========================================================================
    // 文件 body — handler 可指定磁盘文件作为响应体，HttpConnect 通过 sendfile 零拷贝发送
    // =========================================================================
    // handler 中调用 set_file_body() 打开文件，再正常写 HTTP 头（含 Content-Length）
    // 但不要调用 write_body()  — body 由 sendfile 直接从文件 fd 传输
    //
    // 使用示例（handler 内）:
    //   if (!resp.set_file_body("/data/report.json")) { return 404; }
    //   resp.write_status(200, "OK");
    //   resp.write_header("Content-Length", std::to_string(resp.file_body_size()));
    //   resp.write_blank_line();    // 无 write_body，body 来自文件
    //   return true;

    // 打开文件作为响应体，返回 false 表示打开失败（handler 应返回 404/500）
    bool set_file_body(const std::string& path) {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) { close(fd); return false; }
        if (m_file_body_fd_ >= 0) close(m_file_body_fd_);
        m_file_body_fd_   = fd;
        m_file_body_size_ = st.st_size;
        return true;
    }

    bool   has_file_body()   const { return m_file_body_fd_ >= 0; }
    size_t file_body_size()  const { return m_file_body_size_; }

    // 移交 fd 所有权给 HttpConnect — 之后由 write() 末尾的 close(m_file_fd_) 统一关闭
    int release_file_body_fd() { int fd = m_file_body_fd_; m_file_body_fd_ = -1; return fd; }

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
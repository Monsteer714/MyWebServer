//
// Created by Hanhong Wong on 2026/5/22.
//

#ifndef MYWEBSERVER_HTTPREQUEST_H
#define MYWEBSERVER_HTTPREQUEST_H
#include <cassert>
#include <string>
#include <unordered_map>

enum class Method {
    GET = 0,
    POST = 1,
};

class HttpRequest {
private:
    Method m_method_ = {};

    std::string m_path_ = {};

    std::string_view m_version_ = {};

    std::unordered_map<std::string_view, std::string_view> m_headers_ = {};

    std::string_view m_body_ = {};

    ssize_t m_body_length_ = {};
public:
    HttpRequest() = default;

    void set_method(const char* start, const char* end) {
        std::string_view s = std::string_view(start, end);
        if (s == "GET") {
            m_method_ = Method::GET;
        }
        if (s == "POST") {
            m_method_ = Method::POST;
        }
    }

    Method get_method() {
        return m_method_;
    }

    void set_path(const char* start, const char* end) {
        std::string_view path = std::string_view(start, end);
        m_path_ = path;
    }

    std::string_view get_path() {
        return m_path_;
    }

    void set_version(const char* start, const char* end) {
        std::string_view version = std::string_view(start, end);
        m_version_ = version;
    }

    std::string_view get_version() {
        return m_version_;
    }

    void set_headers(const char* start, const char* end) {
        std::string_view s = std::string_view(start, end);
        auto pos = s.find(':');
        assert(pos != std::string_view::npos);
        std::string_view header_type = s.substr(0, pos);
        std::string_view header_content = s.substr(pos + 1);

        int i = 0;
        while (header_content[i] == ' ') {
            i++;
        }
        header_content = header_content.substr(i);
        m_headers_[header_type] = header_content;
    }

    std::string_view get_headers(const std::string_view& header) {
        if (m_headers_.contains(header)) {
            return m_headers_[header];
        } else {
            return "";
        }
    }

    void set_content(const char* start, const char* end) {
        m_body_ = std::string_view(start, end);
    }

    std::string_view get_content() {
        return m_body_;
    }

    void set_content_length(int l) {
        m_body_length_ = l;
    }

    ssize_t get_content_length() {
        return m_body_length_;
    }
};


#endif //MYWEBSERVER_HTTPREQUEST_H
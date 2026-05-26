//
// Created by Hanhong Wong on 2026/5/24.
//
#include <iostream>
#include <ostream>

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "HttpContext.h"

void test_http_request_method() {
    const char* buffer = "GET";
    HttpRequest request;
    request.set_method(buffer, buffer + std::strlen(buffer));

    assert(static_cast<int>(request.get_method()) == 0);
}

void test_http_request_path() {
    const char* buffer = "/index.html";
    HttpRequest request;

    request.set_path(buffer, buffer + std::strlen(buffer));

    assert(request.get_path() == buffer);

    std::cout << request.get_path() << std::endl;
}

void test_http_request_version() {

}

void test_http_request_headers() {
    const char* buffer = "Content-Type: text/html";
    HttpRequest request;
    request.set_headers(buffer, buffer + std::strlen(buffer));

    assert(request.get_headers("Content-Type") == std::string_view("text/html"));

    std::cout << request.get_headers("Content-Type") << std::endl;
}

void test_http_context_parse_request() {
    char* buffer = (char*)("POST /index.html HTTP/1.1\r\n");

    HttpContext context;
    context.parse_request_line(buffer);

    assert(static_cast<int>(context.get_request().get_method()) == 1);
    assert(context.get_request().get_path() == "/index.html");
    assert(context.get_request().get_version() == "HTTP/1.1");
}

void test_http_context_parse_headers() {
    char* buffer = (char*)("Content-Type: text/html/r/nContent-Length: 10/r/n/r/n");
}

void test_http_context_parse_all() {
    char* buffer1 = (char*)("POST /index.html HTTP/1.1\r\nContent-Type: text/html\r\nContent-Length: 7\r\n\r\ncon");
    char* buffer2 = (char*)("POST /index.html HTTP/1.1\r\nContent-Type: text/html\r\nContent-Length: 7\r\n\r\ncontent");
    HttpContext context;

    context.m_read_idx_ += strlen(buffer1);
    context.parse_request(buffer1);
    context.m_read_idx_ = strlen(buffer2);
    context.parse_request(buffer2);

    assert(static_cast<int>(context.get_request().get_method()) == 1);
    assert(context.get_request().get_path() == "/index.html");
    assert(context.get_request().get_version() == "HTTP/1.1");
    assert(context.get_request().get_headers("Content-Type") == std::string_view("text/html"));
    assert(context.get_request().get_headers("Content-Length") == std::string_view("7"));
    assert(context.get_request().get_content_length() == 7);
    assert(context.get_request().get_content() == std::string_view("content"));
}

void test_all() {
    //request
    test_http_request_method();
    test_http_request_path();
    test_http_request_version();
    test_http_request_headers();

    //context
    test_http_context_parse_request();
    test_http_context_parse_headers();
    test_http_context_parse_all();
}

int main() {
    test_all();
}


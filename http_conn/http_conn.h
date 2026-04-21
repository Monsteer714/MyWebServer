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
private:
    int client_fd_;

    // Close the client fd if it's valid and mark it as closed.
    void close_fd() {
        if (client_fd_ >= 0) {
            ::close(client_fd_);
            client_fd_ = -1;
        }
    }

public:
    http_conn(int client_fd) {
        client_fd_ = client_fd;
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

    // process may modify the fd state (close it), so it cannot be const.
    void process() {
        if (client_fd_ < 0) {
            std::cerr << "Invalid client fd" << std::endl;
            return;
        }

        std::string request;
        char buffer[1024];
        while (true) {
            ssize_t n = ::read(client_fd_, buffer, sizeof(buffer));
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
                    return;
                }
            }
            else if (n == 0) {
                // Client closed the connection
                close_fd();
                return;
            }
            request.append(buffer, n);
            if (request.find("\r\n\r\n") != std::string::npos) break;
        }

        if (request.empty()) {
            std::cerr << "Empty request received" << std::endl;
            close_fd();
            return;
        }

        size_t pos = request.find("\r\n");
        if (pos != std::string::npos) {
            std::string first_line = request.substr(0, pos);
            std::istringstream ss(first_line);
            std::string method;
            std::string path;
            std::string version;
            ss >> method >> path >> version;

            if (path == "/") {
                path = "/index.html";
            }

            if (path.find("..") != std::string::npos) {
                std::string resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
                write(client_fd_, resp.c_str(), resp.length());
                close_fd();
                return;
            }

            std::string full_path = "./root" + path;

            std::string body = read_file(full_path);

            std::string resp = "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "Content-Length: "
                + std::to_string(body.length()) + "\r\n"
                "\r\n" +
                body;

            if (body.empty()) {
                resp = "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n";
            }

            ssize_t bytes_written = 0;
            const char* data = resp.c_str();
            size_t total = resp.length();
            while (static_cast<size_t>(bytes_written) < total) {
                ssize_t n = ::write(client_fd_, data + bytes_written, total - bytes_written);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue; // interrupted, retry
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // In a real non-blocking server we'd wait for the socket to become writable.
                        // For this simple server just retry briefly.
                        sched_yield();
                        continue;
                    }
                    else {
                        perror("write");
                        close_fd();
                        return;
                    }
                }
                bytes_written += n;
            }
        }

        // Ensure all data is flushed before closing the connection
        if (client_fd_ >= 0) {
            shutdown(client_fd_, SHUT_WR);
        }
        close_fd();
    }
};

#endif //MYWEBSERVER_HTTP_CONN_H

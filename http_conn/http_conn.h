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

class http_conn {
private:
    int client_fd_;
public:
    http_conn(int client_fd) {
        client_fd_ = client_fd;
    };

    ~http_conn() {
        close(client_fd_);
    }

    std::string read_file(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    void process() {
        if (client_fd_ < 0) {
            std::cerr << "Invalid client fd" << std::endl;
            return;
        }

        std::cout << "Client connected" << std::endl;

        std::string request;
        char buffer[1024];
        while (true) {
            int n = read(client_fd_, buffer, sizeof(buffer));
            if (n <= 0) {
                close(client_fd_);
                return;
            }
            request.append(buffer, n);
            if (request.find("\r\n\r\n") != std::string::npos) break;
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
                close(client_fd_);
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

            write(client_fd_, resp.c_str(), resp.length());

        }
        close(client_fd_);
    }

};

#endif //MYWEBSERVER_HTTP_CONN_H

//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_WEBSERVER_H
#define MYWEBSERVER_WEBSERVER_H
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "http_conn/http_conn.h"
#include "threadpool/threadpool.h"

class WebServer {
private:
    Threadpool<http_conn>* thread_pool_ = {};
    int server_fd_ = {};
public:
    WebServer() {
        thread_pool_ = new Threadpool<http_conn>(4, 100);
    }

    ~WebServer() {
        delete thread_pool_;
    }

    void start() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            perror("socket");
            return;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET; // TCP
        addr.sin_port = htons(8888); // port
        addr.sin_addr.s_addr = INADDR_ANY; // 0?

        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(server_fd_);
            return;
        }
        if (listen(server_fd_, 128) < 0) {
            perror("listen");
            close(server_fd_);
            return;
        }
    }

    void loop() {
        while (true) {
            //TODO:epoll loop here
            int client_fd = accept(server_fd_, nullptr, nullptr);

            if (client_fd < 0) {
                std::cerr << "Accept failed" << std::endl;
                close(server_fd_);
                return;
            }

            auto conn = new http_conn(client_fd);
            thread_pool_->append(conn);
        }
    }
};
#endif //MYWEBSERVER_WEBSERVER_H

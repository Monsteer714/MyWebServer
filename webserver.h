//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_WEBSERVER_H
#define MYWEBSERVER_WEBSERVER_H
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "http_conn/http_conn.h"
#include "threadpool/threadpool.h"

class WebServer {
private:
    Threadpool<http_conn>* thread_pool_ = {};
    int server_fd_ = {};

public:
    WebServer() {
        thread_pool_ = new Threadpool<http_conn>(8, 10000);
    }

    ~WebServer() {
        delete thread_pool_;
        if (server_fd_ >= 0) close(server_fd_);
    }

    void start() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            perror("socket");
            return;
        }

        // 允许端口快速复用，避免压测重启时 "Address already in use"
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8888);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(server_fd_);
            server_fd_ = -1;
            return;
        }
        if (listen(server_fd_, 128) < 0) {
            perror("listen");
            close(server_fd_);
            server_fd_ = -1;
            return;
        }
        // server socket 保持阻塞：主线程阻塞在 accept() 等待连接即可，
        // 无需自旋消耗 CPU。客户端 fd 也保持阻塞，worker 线程可安全读写。
    }

    void loop() {
        while (true) {
            int client_fd = accept(server_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                perror("accept");
                break;
            }

            auto conn = new http_conn(client_fd);
            // Bug fix: append 失败（队列满）时必须释放，否则内存泄漏
            if (!thread_pool_->append(conn)) {
                delete conn;
            }
        }
    }
};
#endif //MYWEBSERVER_WEBSERVER_H

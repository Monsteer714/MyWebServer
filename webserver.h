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
    Threadpool<http_conn>* thread_pool_;

public:
    WebServer() {
        thread_pool_ = new Threadpool<http_conn>(4, 100);
    }

    ~WebServer() {
        delete thread_pool_;
    }

    void run() {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET; // TCP
        addr.sin_port = htons(8888); // port
        addr.sin_addr.s_addr = INADDR_ANY; // 0?

        bind(server_fd, (sockaddr*)&addr, sizeof(addr));
        listen(server_fd, 128);

        while (true) {
            int client_fd = accept(server_fd, nullptr, nullptr);

            if (client_fd < 0) {
                std::cerr << "Accept failed" << std::endl;
                return;
            }

            http_conn* conn = new http_conn(client_fd);
            thread_pool_->append(conn);
        }
    }
};
#endif //MYWEBSERVER_WEBSERVER_H

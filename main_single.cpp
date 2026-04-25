// 单线程阻塞服务器 —— 用于与线程池版本对比压测
// 每次 accept 后立即在主线程内处理请求，不使用任何线程池。
// 在高并发下性能远低于线程池版本，wrk 数据差距直观可见。

#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>

#include "http_conn/http_conn.h"

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 128) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    std::cout << "[single-thread] Listening on port 8888..." << std::endl;

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            perror("accept");
            break;
        }
        // 在主线程内同步处理，无并行能力
        http_conn conn(client_fd);
        conn.process();
    }

    close(server_fd);
    return 0;
}
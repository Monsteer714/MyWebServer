#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <cstring>

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET; // TCP
    addr.sin_port = htons(8888); // port
    addr.sin_addr.s_addr = INADDR_ANY; // 0?

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    while (true) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue;
        }

        std::cout << "Client connected" << std::endl;

        char buffer[1024];
        int n = read(client_fd, buffer, sizeof(buffer));
        if (n > 0) {
            std::cout << std::string(buffer) << std::endl;
        }

        std::string request = std::string(buffer);

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

            std::string full_path = "./root" + path;

            std::string body = read_file(full_path);

            std::string resp = "HTTP/1.1 200 OK\r\n"
                "Content-Length: "
                + std::to_string(body.length()) + "\r\n"
                "\r\n" +
                body;

            write(client_fd, resp.c_str(), resp.length());
        }
    }
}

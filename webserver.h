//
// Created by Hanhong Wong on 2026/4/20.
//

#ifndef MYWEBSERVER_WEBSERVER_H
#define MYWEBSERVER_WEBSERVER_H
#include <cassert>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <sys/epoll.h>

#include "http_conn/HttpConnect.h"
#include "log/async_log.h"
#include "log/log.h"
#include "threadpool/threadpool.h"
#include "timer/timer_policy.h"
#include "timer/timer_lst.h"
#include "timer/timer_wheel.h"
#include "util/types.h"
#include "db_conn/user_auth.h"
#include "third_party/json.hpp"


//threads number for thread pool
constexpr int THREAD_NUM = 8;
//max connect number
constexpr int MAX_FD = 65535;
//time spand for one tick
constexpr int TIME_SLOT = 5;
//epoll table size
constexpr int MAX_EVENT_NUM = 10000;

using json = nlohmann::json;

class WebServer {
private:
    //http_conn
    HttpConnect* m_user_ = {};

    // 路由表：所有连接共享，负责将 URL 分发给对应的 handler
    Router m_router_ = {};

    //threadpool
    Threadpool<HttpConnect>* m_thread_pool_ = {};

    //epoll
    int m_server_fd_ = {-1};
    int m_epoll_fd_ = {-1};
    int m_actor_model_ = {}; //Reactor : 0, Proactor : 1;

    //timer
    int m_pipe_fd_[2] = {};
    int m_timer_model_ = {};
    Util m_util_ = {};
    client_data* m_user_timer_ = {};

    //log
    int m_close_log_ = {};
    int m_log_model_ = {};

    //database
    MySQLClient m_mysql_client_ =
        {"localhost", "root", "", "webserver"};

    //初始化新的http连接，初始化该连接对应的定时器
    void createConn(int connfd) {
        m_user_[connfd].init(connfd);
        m_user_timer_[connfd].m_sock_fd_ = connfd;
        auto timer = m_util_.m_timer_->create_timer();
        timer->cb_func = cb_func;
        timer->user_data_ = &m_user_timer_[connfd];
        timer->adjust_expire(TIME_SLOT);
        m_user_timer_[connfd].m_timer_ = timer;
        m_util_.m_timer_->add_timer(timer);
    }

public:
    WebServer() {
        m_user_ = new HttpConnect[MAX_FD];
        m_user_timer_ = new client_data[MAX_FD];
    }

    ~WebServer() {
        delete m_thread_pool_;
        delete[] m_user_;
        delete[] m_user_timer_;
        if (m_epoll_fd_ >= 0)
            close(m_epoll_fd_);
        if (m_server_fd_ >= 0)
            close(m_server_fd_);
    }

    void init(int m_actor_model, int m_log_model, int m_close_log, int m_timer_model) {
        m_actor_model_ = m_actor_model;
        m_log_model_ = m_log_model;
        m_close_log_ = m_close_log;
        m_timer_model_ = m_timer_model;
    }

    void createThreadPool() {
        m_thread_pool_ = new Threadpool<HttpConnect>(THREAD_NUM, MAX_FD, m_actor_model_);
    }

    void createLog() const {
        if (m_log_model_ == 0) {
            Log::getInstance()->init("./ServerLog", m_close_log_, 2048, 5000000, 800);
        }
        if (m_log_model_ == 1) {
            Async_Log::getInstance()->init("./ServerLog", m_close_log_);
        }
    }

    // =========================================================================
    // 注册所有路由（静态 + 动态）
    // 在 start() 中调用，确保服务器启动前路由表已就绪
    // =========================================================================
    void registerRoutes() {
        // ---- 静态路由: GET /hello → 返回纯文本问候 ----
        m_router_.addRoute(Method::GET, "/hello",
                           [](HttpRequest& req, HttpResponse& resp) {
                               return resp.send_body(
                                   "<html><body><h1>Hello from Router!</h1>"
                                   "<p>这是一条静态路由响应。</p></body></html>",
                                   "text/html; charset=utf-8");
                           });

        // ---- 静态路由: GET /api/status → 返回 JSON 状态信息 ----
        m_router_.addRoute(Method::GET, "/api/status",
                           [](HttpRequest& req, HttpResponse& resp) {
                               return resp.send_body(
                                   "{ \"status\": \"ok\", \"server\": \"MyWebServer\", "
                                   "\"routes\": \"static + dynamic\" }",
                                   "application/json");
                           });

        // ---- 动态路由: GET /users/:id → 返回用户信息 JSON ----
        // 例: /users/123 → {"user_id":"123","name":"User-123"}
        m_router_.addRoute(Method::GET, "/users/:id",
                           [](HttpRequest& req, HttpResponse& resp) {
                               auto userId = req.get_path_parameters("id");
                               std::string body = R"({ "user_id": ")" + userId +
                                   R"(", "name": "User-)" + userId + "\" }";
                               return resp.send_body(body.c_str(), body.size(),
                                                     "application/json");
                           });

        // ---- 嵌套动态路由: GET /posts/:postId/comments/:commentId ----
        // 例: /posts/42/comments/7 → 展示两个动态参数
        m_router_.addRoute(Method::GET, "/posts/:postId/comments/:commentId",
                           [](HttpRequest& req, HttpResponse& resp) {
                               auto postId = req.get_path_parameters("postId");
                               auto commentId = req.get_path_parameters("commentId");
                               std::string body = "<html><body>"
                                   "<h1>嵌套动态路由测试</h1>"
                                   "<p>Post ID: <strong>" + postId + "</strong></p>"
                                   "<p>Comment ID: <strong>" + commentId + "</strong></p>"
                                   "</body></html>";
                               return resp.send_body(body.c_str(), body.size(),
                                                     "text/html; charset=utf-8");
                           });

        // ---- 动态路由: GET /download/:filename → sendfile 零拷贝文件下载 ----
        // send_file() 一行搞定: 打开文件 → 写头 → sendfile 发送
        // 文件不存在时自动返回 404
        m_router_.addRoute(Method::GET, "/download/:filename",
                           [](HttpRequest& req, HttpResponse& resp) {
                               auto filename = req.get_path_parameters("filename");
                               return resp.send_file("./root/" + filename,
                                                     "application/octet-stream",
                                                     filename.c_str());
                           });

        m_router_.addRoute(Method::POST, "/api/register",
                           [&](HttpRequest& req, HttpResponse& resp) -> bool {
                               auto body = req.get_content();

                               //parse json.
                               json register_info = json::parse(body);

                               const std::string username = register_info["username"];
                               const std::string password = register_info["password"];
                               int uid = user_register(m_mysql_client_, username, password);
                               if (uid == -1) {
                                   return resp.send_body(
                                       R"({"status":"error","message":"user exists"})",
                                       "application/json");
                               }
                               std::string success = R"({"status":"ok","message":"success"})";
                               return resp.send_body(success.c_str(), success.size(), "application/json");
                           });

        m_router_.addRoute(Method::POST, "/api/login",
                           [&](HttpRequest& req, HttpResponse& resp) -> bool {
                               auto body = req.get_content();

                               json login_info = json::parse(body);

                               const std::string username = login_info["username"];
                               const std::string password = login_info["password"];
                               auto uinfo = user_login(m_mysql_client_, username, password);
                               if (uinfo == std::nullopt) {
                                   return resp.send_body(R"({"status":"error","message":"user not exist"})",
                                                         "application/json");
                               }

                               json success;
                               success["status"] = "success";
                               success["message"] = "login successfully";
                               success["username"] = username;
                               return resp.send_body(success.dump().c_str(), success.dump().size(), "application/json");
                           });

        // ---- 静态路由: GET / → 返回 false，让 do_request() 走文件服务 ----
        // Router 不处理首页，退回给文件服务读取 root/index.html
        m_router_.addRoute(Method::GET, "/",
                           [](HttpRequest& req, HttpResponse& resp) {
                               return false; // 故意不处理，退回文件服务
                           });
    }

    void start() {
        // ---- 初始化路由表 ----
        // 在绑定端口前注册所有路由，确保首次请求时路由已就绪
        registerRoutes();
        // 将路由表指针注入 HttpConnect（静态成员，所有连接实例共享）
        HttpConnect::m_router_ = &m_router_;

        //网络编程基础步骤
        m_server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(m_server_fd_ >= 0);

        // 允许端口快速复用，避免压测重启时 "Address already in use"
        int opt = 1;
        setsockopt(m_server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8888);
        addr.sin_addr.s_addr = INADDR_ANY;
        memset(addr.sin_zero, '\0', sizeof addr.sin_zero);

        int ret = bind(m_server_fd_, (sockaddr*)&addr, sizeof(addr));
        assert(ret >= 0);
        ret = listen(m_server_fd_, 65535);
        assert(ret >= 0);
        //

        m_util_.setnonblocking(m_server_fd_);

        //创建内核epoll事件表
        m_epoll_fd_ = epoll_create1(0);
        assert(m_epoll_fd_ >= 0);
        m_util_.addfd(m_epoll_fd_, m_server_fd_, false);

        HttpConnect::m_epollfd_ = m_epoll_fd_;

        //定时器相关
        ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipe_fd_);
        assert(ret >= 0);

        Util::u_epoll_fd_ = m_epoll_fd_;
        Util::u_pipe_fd_ = m_pipe_fd_;

        m_util_.init(TIME_SLOT);
        //设定定时器模式
        if (m_timer_model_ == 0) {
            m_util_.init_timer(std::make_unique<sort_timer_lst>());
        }
        if (m_timer_model_ == 1) {
            m_util_.init_timer(std::make_unique<timer_wheel>());
        }
        m_util_.setnonblocking(m_pipe_fd_[1]);
        m_util_.addfd(m_epoll_fd_, m_pipe_fd_[0], false);
        m_util_.addsig(SIGPIPE, SIG_IGN, false);
        m_util_.addsig(SIGALRM, Util::sig_handler, false);
        m_util_.addsig(SIGTERM, Util::sig_handler, false);
        alarm(TIME_SLOT);

        Async_Log::LOG_INFO("Web server started on port %d", 8888);
    }

    void adjustTimer(int fd) {
        auto timer = m_user_timer_[fd].m_timer_;
        if (timer) {
            timer->adjust_expire(TIME_SLOT);
            m_util_.m_timer_->adjust_timer(timer);

            LOG_INFO("adjust fd %d's timer", fd);
        }
    }

    void dealWithTimer(int fd) {
        auto timer = m_user_timer_[fd].m_timer_;
        if (timer) {
            timer->cb_func(timer->user_data_);
            m_util_.m_timer_->del_timer(timer);

            LOG_INFO("delete fd %d's timer", fd);
        }
    }

    bool dealwithclient() {
        while (true) {
            int client_fd = accept(m_server_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                perror("accept");
                break;
            }

            if (client_fd >= MAX_FD) {
                return false;
            }

            createConn(client_fd);
        }
        return true;
    }

    void dealwithread(int fd) {
        auto conn = m_user_ + fd;
        if (m_actor_model_ == 0) {
            adjustTimer(fd);
            m_thread_pool_->append_s(conn, 0);
        }
        else {
            if (conn->read()) {
                m_thread_pool_->append(conn);
                adjustTimer(fd);
            }
            else {
                dealWithTimer(fd);
            }
        }
    }

    void dealwithwrite(int fd) {
        auto conn = m_user_ + fd;
        if (m_actor_model_ == 0) {
            adjustTimer(fd);
            m_thread_pool_->append_s(conn, 1);
        }
        else {
            if (conn->write()) {
                adjustTimer(fd);
            }
            else {
                dealWithTimer(fd);
            }
        }
    }


    bool dealwithsignal(bool& timeout, bool& stop_server) {
        char signals[1024];
        int ret = recv(m_pipe_fd_[0], &signals, sizeof(signals), 0);
        if (ret < 1) {
            LOG_ERROR("%s", "dealwithsignal error.");
            return false;
        }
        for (int _ = 0; _ < ret; ++_) {
            switch (signals[_]) {
            case SIGALRM:
                timeout = true;
                break;
            case SIGTERM:
                stop_server = true;
                break;
            default:
                break;
            }
        }
        return true;
    }


    void loop() {
        std::vector<epoll_event> events(MAX_EVENT_NUM);
        bool timeout = false;
        bool stop_server = false;
        while (!stop_server) {
            int n = epoll_wait(m_epoll_fd_, events.data(), MAX_EVENT_NUM, -1);
            if (n < 0 && errno != EINTR) {
                LOG_ERROR("%s", "epoll_wait error");
                break;
            }

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                if (fd == m_server_fd_) { // 新连接
                    if (!dealwithclient()) {
                        continue;
                    }
                }
                else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    dealWithTimer(fd);
                    //错误，关闭连接；
                }
                else if ((fd == m_pipe_fd_[0]) && (events[i].events & EPOLLIN)) {
                    dealwithsignal(timeout, stop_server);
                    //sig信号处理
                }
                else if (events[i].events & EPOLLIN) {
                    dealwithread(fd);
                }
                else if (events[i].events & EPOLLOUT) {
                    dealwithwrite(fd);
                }
            }
            if (timeout) {
                m_util_.time_handler();
                LOG_INFO("%s", "tick.");
                timeout = false;
            }
        }
    }
};
#endif //MYWEBSERVER_WEBSERVER_H

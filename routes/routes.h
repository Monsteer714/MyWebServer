//
// Created by Hanhong Wong on 2026/6/6.
//
// 所有路由 handler 的具体实现 — 每个 URL 对应一个 RouteHandler 子类
// registerAllRoutes() 统一注册，webserver.h 只需一行调用
//

#ifndef MYWEBSERVER_ROUTES_H
#define MYWEBSERVER_ROUTES_H

#include <memory>
#include <string>

#include "Router.h"
#include "RouteHandler.h"
#include "upload_routes.h"
#include "../http_conn/HttpRequest.h"
#include "../http_conn/HttpResponse.h"
#include "../db_conn/mysql_client.h"
#include "../db_conn/user_auth.h"
#include "../log/async_log.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;
// =============================================================================
// 静态路由: GET /hello — 返回纯文本问候
// =============================================================================
class HelloHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        return resp.send_body(
            "<html><body><h1>Hello from Router!</h1>"
            "<p>这是一条静态路由响应。</p></body></html>",
            "text/html; charset=utf-8");
    }
};

// =============================================================================
// 静态路由: GET /api/status — 返回 JSON 状态信息
// =============================================================================
class StatusHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        return resp.send_body(
            R"({"status":"ok","server":"MyWebServer","routes":"static + dynamic"})",
            "application/json");
    }
};

// =============================================================================
// 动态路由: GET /users/:id — 返回用户信息 JSON
// =============================================================================
class UserHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto userId = req.get_path_parameters("id");
        std::string body = R"({"user_id":")" + userId +
            R"(","name":"User-)" + userId + "\"}";
        return resp.send_body(body.c_str(), body.size(), "application/json");
    }
};

// =============================================================================
// 动态路由: GET /posts/:postId/comments/:commentId — 嵌套参数
// =============================================================================
class PostCommentHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto postId = req.get_path_parameters("postId");
        auto commentId = req.get_path_parameters("commentId");
        std::string body = "<html><body>"
            "<h1>嵌套动态路由测试</h1>"
            "<p>Post ID: <strong>" + postId + "</strong></p>"
            "<p>Comment ID: <strong>" + commentId + "</strong></p>"
            "</body></html>";
        return resp.send_body(body.c_str(), body.size(),
                              "text/html; charset=utf-8");
    }
};

// =============================================================================
// POST /api/register — 用户注册
// =============================================================================
class RegisterHandler : public RouteHandler {
    MySQLClient& db_;

public:
    explicit RegisterHandler(MySQLClient& db) : db_(db) {
    }

    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto body = req.get_content();
        json register_info = safe_parse(body);
        std::string username = register_info["username"];
        std::string password = register_info["password"];

        int uid = user_register(db_, username, password);
        if (uid == -1) {
            return resp.send_body(
                R"({"status":"error","message":"user exists"})",
                "application/json");
        }
        return resp.send_body(
            R"({"status":"ok","message":"success"})",
            "application/json");
    }
};

// =============================================================================
// POST /api/login — 用户登录
// =============================================================================
class LoginHandler : public RouteHandler {
    MySQLClient& db_;

public:
    explicit LoginHandler(MySQLClient& db) : db_(db) {
    }

    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto body = req.get_content();
        json login_info = safe_parse(body);
        std::string username = login_info["username"];
        std::string password = login_info["password"];

        auto uinfo = user_login(db_, username, password);
        if (!uinfo) {
            return resp.send_body(
                R"({"status":"error","message":"user not exist"})",
                "application/json");
        }
        json success;
        success["status"] = "ok";
        success["message"] = "login successfully";
        success["username"] = username;
        auto str = success.dump();
        return resp.send_body(str.c_str(), str.size(), "application/json");
    }
};

// =============================================================================
// POST /api/upload — 文件上传（multipart/form-data）
// =============================================================================
inline std::string extract_boundary(const std::string& content_type) {
    std::string boundary_mark = "boundary=";
    auto pos = content_type.find(boundary_mark);
    if (pos == std::string::npos) {
        return "";
    }
    return content_type.substr(pos + boundary_mark.size());
}

struct Uploadfile {
    std::string filename = {};
    std::string mime_type = {};
    const char* data = {};
    size_t size = {};
};

inline bool parse_multipart(std::string_view body, const std::string& boundary, Uploadfile& file) {
    std::string delim = "--" + boundary;
    auto start = body.find(delim);
    if (start == std::string::npos) {
        return false;
    }

    start = body.find("\r\n", start);
    if (start == std::string::npos) {
        return false;
    }

    // 解析 part 头
    auto header_end = body.find("\r\n\r\n", start);
    if (header_end == std::string_view::npos) return false;

    std::string_view part_header = body.substr(start, header_end - start);

    // 从 Content-Disposition 提取 filename
    auto fn_pos = part_header.find("filename=\"");
    if (fn_pos != std::string_view::npos) {
        fn_pos += 10;
        auto fn_end = part_header.find("\"", fn_pos);
        file.filename = std::string(part_header.substr(fn_pos, fn_end - fn_pos));
    }

    // 从 Content-Type 提取 MIME
    auto ct_pos = part_header.find("Content-Type: ");
    if (ct_pos != std::string_view::npos) {
        ct_pos += 14;
        auto ct_end = part_header.find("\r\n", ct_pos);
        file.mime_type = std::string(part_header.substr(ct_pos, ct_end - ct_pos));
    }

    // 文件内容：header 后的 \r\n\r\n 到下一个 --boundary
    const char* file_data = body.data() + header_end + 4;
    auto file_end = body.find(delim, header_end);
    if (file_end == std::string_view::npos) file_end = body.size();

    // 去掉末尾的 \r\n
    size_t file_size = file_end - header_end - 4;
    if (file_size >= 2 && body[file_end - 2] == '\r') file_size -= 2;

    file.data = file_data;
    file.size = file_size;
    return true;
}


class UploadHandler : public RouteHandler {
    MySQLClient& db_;

public:
    explicit UploadHandler(MySQLClient& db) : db_(db) {
    }

    bool handle(HttpRequest& req, HttpResponse& resp) override {
        LOG_DEBUG("start upload");
        auto body = req.get_content();
        if (body.empty()) {
            json error;
            error["status"] = "error";
            error["message"] = "empty file uploaded";
            return resp.send_body(error.dump(), "application/json");
        }

        auto content_type = std::string(req.get_headers("Content-Type"));
        auto boundary = extract_boundary(content_type);
        if (boundary.empty()) {
            return resp.send_error(StatusCode::BAD_REQUEST);
        }

        Uploadfile file;
        if (!parse_multipart(body, boundary, file)) {
            return resp.send_error(StatusCode::BAD_REQUEST);
        }

        if (file.size > 50 * 1024 * 1024) {
            json error;
            error["status"] = "error";
            error["message"] = "file too large";
            return resp.send_body(error.dump(), "application/json");
        }

        std::string uuid = file.filename;
        std::string file_path = "./root/files/" + uuid;
        LOG_DEBUG("file_path=%s", file_path.c_str());

        int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0644);
        if (fd < 0) {
            LOG_ERROR("upload internal error");
            return resp.send_error(StatusCode::INTERNAL_ERROR);
        }
        write(fd, file.data, file.size);
        close(fd);

        LOG_DEBUG("upload file success");
        json success;
        success["status"] = "ok";
        success["message"] = "success";
        success["filename"] = file.filename;
        success["uuid"] = uuid;

        return resp.send_body(success.dump(), "application/json");
    }
};

// =============================================================================
// GET /files/:uuid — 文件下载（sendfile + 浏览器下载）
// =============================================================================
class DownloadHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto uuid = req.get_path_parameters("uuid");
        // send_file 第3个参数触发 Content-Disposition: attachment → 浏览器弹出下载
        return resp.send_file("./root/files/" + uuid,
                              "application/octet-stream", uuid.c_str());
    }
};

// =============================================================================
// GET / — 首页（退回文件服务）
// =============================================================================
class IndexHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        return false; // 不处理，退回 do_request()
    }
};

// =============================================================================
// registerAllRoutes — 统一注册入口
// =============================================================================
// 由 webserver.h 调用，所有路由在此集中管理
inline void registerAllRoutes(Router& router, MySQLClient& db) {
    // ---- 分片上传路由（断点续传 + 多线程）----
    registerUploadRoutes(router);

    // ---- 静态路由 ----
    router.addRoute(Method::GET, "/", std::make_unique<IndexHandler>());
    router.addRoute(Method::GET, "/hello", std::make_unique<HelloHandler>());
    router.addRoute(Method::GET, "/api/status", std::make_unique<StatusHandler>());

    // ---- 动态路由 ----
    router.addRoute(Method::GET, "/users/:id",
                    std::make_unique<UserHandler>());
    router.addRoute(Method::GET, "/posts/:postId/comments/:commentId",
                    std::make_unique<PostCommentHandler>());
    // ---- 数据库相关（需持有 MySQLClient 引用）----
    router.addRoute(Method::POST, "/api/register",
                    std::make_unique<RegisterHandler>(db));
    router.addRoute(Method::POST, "/api/login",
                    std::make_unique<LoginHandler>(db));

    router.addRoute(Method::POST, "/api/upload",
                    std::make_unique<UploadHandler>(db));
    router.addRoute(Method::GET, "/files/:uuid",
                    std::make_unique<DownloadHandler>());
}

#endif //MYWEBSERVER_ROUTES_H

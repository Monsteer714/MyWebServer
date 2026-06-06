//
// Created by Hanhong Wong on 2026/5/26.
//
// 路由系统：将 HTTP 请求按路径分发给对应的处理函数。
//
// 核心概念 —— 静态路由 vs 动态路由：
//
//   静态路由：路径是写死的字符串，精确匹配。
//     例: addRoute(GET, "/login")     → GET /login 命中
//         addRoute(GET, "/api/status") → GET /api/status 命中
//     实现: unordered_map<"METHOD:path", handler> → O(1) 查找
//
//   动态路由：路径中包含 :param 占位符，匹配时提取变量。
//     例: addRoute(GET, "/users/:id")
//         GET /users/123 → 命中，handler 内通过 req.get_path_parameters("id") 拿到 "123"
//         GET /users/999 → 命中同一 handler，拿到 "999"
//     实现: 预分割 token 数组 + 逐段比较，O(k) 其中 k = 路径段数
//
// 设计决策 —— 为什么不用正则？
//   RESTful API 的路径模式足够简单（:param 占位符已覆盖绝大多数场景），
//   正则引擎在每个请求上初始化和回溯的开销在高并发下不可忽视。
//   token 比较只有 O(k) 次字符串相等检查，内存访问模式也更友好。
//   如果未来需要通配符 /files/*，可以用双表策略：token 快速路径 + 正则兜底。

#ifndef MYWEBSERVER_ROUTER_H
#define MYWEBSERVER_ROUTER_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../http_conn/HttpRequest.h"
#include "../http_conn/HttpResponse.h"
#include "RouteHandler.h"

class Router {
public:
    // =========================================================================
    // Handler 类型定义
    // =========================================================================
    // 每个路由注册时提供一个回调函数，签名为:
    //   bool handler(HttpRequest& req, HttpResponse& resp);
    //
    //   req  — 请求对象，handler 可从中读取 method / path / headers / body，
    //          动态路由匹配后还可通过 req.get_path_parameters(":id") 取到参数
    //   resp — 响应对象，handler 通过其底层 API 逐步构建 HTTP 响应
    //          （write_status → write_header → write_blank_line → write_body）
    //   返回 true  表示 handler 已成功构建响应
    //   返回 false 表示 handler 内部出错（调用方应返回 500）
    //
    // 选择 std::function 而非虚基类的原因：
    //   注册路由时可以直接写 lambda，无需为每个路由单独写一个类，开发体验更好。
    using HandlerCallback = std::function<bool(HttpRequest&, HttpResponse&)>;

    // =========================================================================
    // addRoute — 注册一条路由
    // =========================================================================
    //   method  : GET 或 POST
    //   pattern : 路径模式，如 "/login"（静态）或 "/users/:id"（动态）
    //   handler : 匹配成功后调用的处理函数
    //
    // 自动判别规则：
    //   pattern 中包含 ':' → 分割为 token 存入动态路由表
    //   pattern 中不含 ':' → 按 "METHOD:path" 为 key 存入静态路由表
    //
    // 同一个 path + 不同 method 可以注册不同的 handler:
    //   addRoute(GET,  "/api/data", getHandler);
    //   addRoute(POST, "/api/data", postHandler);
    // 注册路由 — std::function 版本（lambda / 函数指针）
    void addRoute(Method method, const std::string& pattern, HandlerCallback handler);

    // 注册路由 — 面向对象版本（RouteHandler 子类）
    // Router 接管 handler 的所有权，在 Router 析构时自动释放
    void addRoute(Method method, const std::string& pattern,
                  std::unique_ptr<RouteHandler> handler);

    // =========================================================================
    // route — 路由分发
    // =========================================================================
    //   输入: req（请求对象）、resp（响应对象）
    //   输出: true = 匹配成功并已执行 handler（响应已写入 resp）
    //         false = 未匹配到任何路由（调用方应回退到文件服务或返回 404）
    //
    // 匹配优先级:
    //   1. 先查静态路由表（O(1)，大部分路由是静态的，走最快路径）
    //   2. 再遍历动态路由表（O(n * k)，n = 动态路由数，k = 路径段数）
    //
    // 动态路由匹配时，捕获到的 :param 值通过 req.set_path_parameters() 注入，
    // handler 内可通过 req.get_path_parameters("key") 读取。
    bool route(HttpRequest& req, HttpResponse& resp);

    // 已注册路由总数（静态 + 动态），调试/监控用
    size_t get_route_count() const;

private:
    // =========================================================================
    // 静态路由表
    // =========================================================================
    // key = "GET:/login"、"POST:/api/data" 等
    // 精确字符串匹配，O(1) 哈希查找
    std::unordered_map<std::string, HandlerCallback> static_routers;

    // =========================================================================
    // 动态路由表
    // =========================================================================
    // 每条动态路由预先将 pattern 按 '/' 分割成 token 数组，避免每个请求重复分割。
    //
    // 例: pattern "/users/:id/posts/:postId"
    //     → tokens = ["users", ":id", "posts", ":postId"]
    //
    // 匹配时只需将请求 path 同样分割，然后逐段比较：
    //   "users"  == "users"     → 精确匹配
    //   ":id"    匹配 "123"     → 捕获为参数 id="123"
    //   "posts"   == "posts"    → 精确匹配
    //   ":postId" 匹配 "42"     → 捕获为参数 postId="42"
    struct DynamicRoute {
        Method method; // GET / POST
        std::vector<std::string> tokens; // 预分割的 token 数组
        HandlerCallback handler; // 处理函数
    };

    std::vector<DynamicRoute> dynamic_routers;

    // =========================================================================
    // 内部辅助函数
    // =========================================================================

    // 构造静态路由表的 key: "METHOD:/path"
    static std::string methodKey(Method m, const std::string& path);

    // 将路径按 '/' 分割为 token 数组，忽略前导 '/' 和空段
    //   "/users/123"      → ["users", "123"]
    //   "/a/b/c"          → ["a", "b", "c"]
    //   "/"               → []
    static std::vector<std::string> splitPath(const std::string& path);
};

// =============================================================================
// 实现
// =============================================================================

inline void Router::addRoute(Method method, const std::string& pattern,
                             HandlerCallback handler) {
    // pattern 包含 ':' → 动态路由
    if (pattern.find(':') != std::string::npos) {
        DynamicRoute dr;
        dr.method = method;
        dr.tokens = splitPath(pattern); // 预分割，避免每个请求重复分割
        dr.handler = std::move(handler);
        dynamic_routers.push_back(std::move(dr));
        return;
    }

    // 不含 ':' → 静态路由，直接用 "METHOD:path" 作哈希 key
    static_routers[methodKey(method, pattern)] = std::move(handler);
}

inline void Router::addRoute(Method method, const std::string& pattern,
                             std::unique_ptr<RouteHandler> h) {
    auto shared = std::shared_ptr<RouteHandler>(std::move(h));
    addRoute(method, pattern,
             [shared](HttpRequest& req, HttpResponse& resp) -> bool {
                 return shared->handle(req, resp);
             });
}

inline bool Router::route(HttpRequest& req, HttpResponse& resp) {
    // ---- 第一步：从请求中提取纯 URL 路径（去掉 ./root 前缀）----
    std::string url_path = req.get_url_path();

    // ---- 第二步：先查静态路由表 O(1) ----
    // 大部分路由是静态的，走这条快速路径
    auto key = methodKey(req.get_method(), url_path);
    auto it = static_routers.find(key);
    if (it != static_routers.end()) {
        return it->second(req, resp);
    }

    // ---- 第三步：遍历动态路由表 ----
    // 将请求路径按 '/' 分割为 token，与每条动态路由的预分割 token 逐段比较
    std::vector<std::string> path_tokens = splitPath(url_path);

    for (auto& dr : dynamic_routers) {
        // 过滤 1：Method 必须一致
        if (dr.method != req.get_method()) continue;

        // 过滤 2：token 数量必须一致（快速排除长度不匹配的候选）
        if (dr.tokens.size() != path_tokens.size()) continue;

        // 逐段比较
        std::unordered_map<std::string, std::string> matched_params;
        bool matched = true;

        for (size_t i = 0; i < dr.tokens.size(); ++i) {
            const auto& route_token = dr.tokens[i];
            const auto& path_token = path_tokens[i];

            if (route_token[0] == ':') {
                // 动态段 —— 以 ':' 开头，匹配任意值并捕获
                // 例: route_token = ":id", path_token = "123"
                //     → 存入 matched_params["id"] = "123"
                matched_params[route_token.substr(1)] = path_token;
            }
            else if (route_token != path_token) {
                // 静态段 —— 必须逐字符精确匹配
                matched = false;
                break;
            }
        }

        if (matched) {
            // 匹配成功 —— 将捕获到的参数注入 HttpRequest
            // handler 内可通过 req.get_path_parameters("id") 读取
            for (auto& [k, v] : matched_params) {
                req.set_path_parameters(k, v);
            }

            // 调用 handler，由它写响应
            return dr.handler(req, resp);
        }
    }

    // 静态表没命中，动态表也没匹配 → 返回 false
    // 调用方应回退到文件服务或返回 404
    return false;
}

inline size_t Router::get_route_count() const {
    return static_routers.size() + dynamic_routers.size();
}

inline std::string Router::methodKey(Method m, const std::string& path) {
    // 构造静态路由表的查找 key
    // GET /login  → "GET:/login"
    // POST /api   → "POST:/api"
    return (m == Method::GET ? "GET:" : "POST:") + path;
}

inline std::vector<std::string> Router::splitPath(const std::string& path) {
    std::vector<std::string> tokens;
    size_t start = 0;

    // 跳过前导 '/'
    if (!path.empty() && path[0] == '/') {
        start = 1;
    }

    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }

        // 忽略空段（例如连续 // 或尾部 /）
        if (end > start) {
            tokens.push_back(path.substr(start, end - start));
        }

        start = end + 1;
    }

    return tokens;
}

#endif //MYWEBSERVER_ROUTER_H

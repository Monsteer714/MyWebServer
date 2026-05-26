# 05 · Router 路由系统

## 目标

替换 `http_conn::do_request()` 中"所有请求只返回 index.html"的硬编码逻辑，实现路径→处理函数的灵活分发。

---

## 什么是路由？——一个餐厅的类比

在你没有路由器之前，你的服务器就像一个**自动售货机**：不管客人按哪个按钮（访问什么路径），掉出来的都是同一瓶饮料（index.html）。

```cpp
// 你现在的代码 = 只有一个按键的售货机
StatusCode do_request() {
    // 无论请求 / 还是 /hello 还是 /api/users
    // 永远返回 root/index.html
    m_file_path_ = "root/index.html";
    ...
}
```

有了路由器之后，你的服务器像一个**餐厅**：客人看菜单（URL 路径），服务员（Router）根据客人的选择，把订单分发给不同的厨师（handler 函数）。

```
客人: "我要牛排"    (/steak)    →  牛排厨师处理
客人: "我要沙拉"    (/salad)    →  沙拉厨师处理
客人: "我要第 3 号套餐" (/combo/3) → 套餐厨师处理，取出编号 3
```

**"动态路由"** 就是菜单上有变量部分的能力：`/users/123` 和 `/users/456` 走同一个 handler，但 handler 能拿到 `123` 和 `456` 这两个不同的值。

---

## 静态路由 vs 动态路由——用实际 URL 理解

### 静态路由：路径是写死的

```
注册: router.addRoute(GET, "/login")       → LoginHandler
      router.addRoute(GET, "/about")       → AboutHandler
      router.addRoute(GET, "/api/status")  → StatusHandler

请求: GET /login     → 精确匹配 "/login"    → LoginHandler 执行 ✓
      GET /about     → 精确匹配 "/about"    → AboutHandler 执行 ✓
      GET /hello     → 没有任何注册的路径    → 404 ✗
      GET /login/123 → 没有精确匹配的路径    → 404 ✗
```

每条路由是一个**精确的字符串**。实现：一个 `unordered_map<string, handler>`，O(1) 查找。

### 动态路由：路径中有变量占位符

```
注册: router.addRoute(GET, "/users/:id")        → UserHandler
      router.addRoute(GET, "/posts/:postId/comments/:commentId") → CommentHandler

请求: GET /users/123
      → 匹配 "/users/:id"， ":id" 捕获到 "123"
      → UserHandler 中通过 req.getPathParameter("id") 拿到 "123"

请求: GET /users/999
      → 匹配 "/users/:id"， ":id" 捕获到 "999"
      → UserHandler 中通过 req.getPathParameter("id") 拿到 "999"

请求: GET /posts/42/comments/7
      → 匹配 "/posts/:postId/comments/:commentId"
      → handler 中拿到 postId="42", commentId="7"
```

**同一个 handler，不同的输入值**——这就是"动态"的含义。

---

## 你的现状 vs 有 Router 之后

### 现状：所有请求走进 `do_request()` 的死胡同

```cpp
// http_conn.h  — 你现在的处理流程
StatusCode process_read() {
    auto ret = m_context_.parse_request(m_read_buffer_);
    if (ret == StatusCode::SUCCESS) {
        return do_request();  // ← 永恒的 return 文件
    }
    return ret;
}

StatusCode do_request() {
    // 无论请求的 path 是什么，都是打开文件
    m_file_path_ = m_context_.get_request().get_path();
    m_file_fd_ = open(m_file_path_.c_str(), O_RDONLY);
    return StatusCode::OK;
}
```

流程：`收到请求 → 读文件 → 发文件`。没有"处理业务逻辑"的空间。

### 有 Router 之后：请求被分发到不同的处理函数

```
收到请求 → 解析 HttpRequest
           ↓
        Router::route(req, resp)
           ↓
        ┌── GET /           → HomeHandler    → 返回 index.html
        ├── GET /login      → LoginHandler   → 返回登录页面
        ├── GET /api/users  → UserListHandler → 返回 JSON 数据
        ├── GET /users/123  → UserHandler(id=123) → 动态查用户
        ├── POST /api/login → LoginAPIHandler → 处理登录表单
        └── 都不匹配        → 404
```

**每个 handler 是一个独立的函数，可以有自己的逻辑**——这就是动态路由的核心价值。

---

## 接口定义

### Handler：处理函数的类型

你有两种选择：

```cpp
// 方案A：函数回调（简单，推荐起步用）
// 就是一个 std::function — 能用 lambda，能用普通函数，能用成员函数
using HandlerCallback = std::function<bool(const HttpRequest&, Buffer*)>;
//                                              请求进来       响应写到这里
//                                        返回 true = 成功
//                                        返回 false = handler 内部错误

// 方案B：抽象接口（更"正统"的 OOP，Kama 的方案）
class RouterHandler {
public:
    virtual ~RouterHandler() = default;
    virtual bool handle(const HttpRequest& req, Buffer* buf) = 0;
};
```

**建议从方案A开始**。写 lambda 注册路由最直观：

```cpp
// 静态路由
router.addRoute(GET, "/hello", [](const HttpRequest& req, Buffer* buf) {
    buf->append("HTTP/1.1 200 OK\r\nContent-Length:13\r\n\r\nHello, World!");
    return true;
});

// 动态路由 — handler 中通过 req.getPathParameter() 拿到 URL 中的变量
router.addRoute(GET, "/users/:id", [](const HttpRequest& req, Buffer* buf) {
    auto user_id = req.getPathParameter("id");  // ← 这就是 ":id" 捕获到的值
    std::string body = "User ID: " + user_id;
    // ... 查数据库、查缓存、格式化响应 ...
    buf->append(format_response(200, body));
    return true;
});
```

### Router 类完整接口

```cpp
class Router {
public:
    using HandlerCallback = std::function<bool(const HttpRequest&, Buffer*)>;

    // ===== 路由注册 =====
    // 静态路由: addRoute(GET, "/login", handler)
    // 动态路由: addRoute(GET, "/users/:id", handler)
    //            addRoute(GET, "/posts/:postId/comments/:commentId", handler)
    void addRoute(Method method, const std::string& pattern, HandlerCallback handler);

    // ===== 路由分发 =====
    // 输入：请求对象 → 匹配路由 → 调用对应 handler → 结果写入 buf
    // 返回：true = 匹配到路由并执行了 handler
    //       false = 未匹配（调用方设 404）
    bool route(const HttpRequest& req, Buffer* buf);

    // ===== 调试/监控 =====
    size_t routeCount() const;   // 已注册的路由总数

private:
    // 静态路由表：key = "METHOD:path"（如 "GET:/login"）
    std::unordered_map<std::string, HandlerCallback> static_routes_;

    // 动态路由表：每条路由包含预分割的 tokens 和 handler
    struct DynamicRoute {
        Method method;
        std::vector<std::string> tokens;  // 预分割 ["users", ":id"]
        HandlerCallback handler;
    };
    std::vector<DynamicRoute> dynamic_routes_;
};
```

---

## 路由匹配算法：逐步追踪

以请求 `GET /posts/42/comments/7` 匹配已注册路由 `/posts/:postId/comments/:commentId` 为例：

```
步骤 1 — 查静态路由表
  key = "GET:/posts/42/comments/7"
  static_routes_ 中没有 → 跳过

步骤 2 — 遍历动态路由表
  取出 DynamicRoute { method=GET, tokens=["posts",":postId","comments",":commentId"] }

  步骤 2a — token 数量快速检查
    请求 path 按 '/' 分割 → ["posts", "42", "comments", "7"]
    tokens.size() = 4 vs 请求 tokens = 4 → 通过

  步骤 2b — 逐段匹配
    i=0: "posts"  == "posts"      → 精确匹配 ✓
    i=1: ":postId" 匹配 "42"     → 动态段，存入 params["postId"] = "42"
    i=2: "comments" == "comments"  → 精确匹配 ✓
    i=3: ":commentId" 匹配 "7"   → 动态段，存入 params["commentId"] = "7"

  全部匹配! → 把 params 注入 req → 执行 handler
```

### 面试你为什么这样设计

> "我在数据结构上做了两个决策。第一，静态路由用 unordered_map 做 O(1) 精确匹配——大部分路由都是静态的，应该最快路径。第二，动态路由用预分割的 token vector 做逐段比较，而不是在每个请求时用正则。原因是正则引擎的初始化有开销，而 token 比较只有 O(k) 的字符串比较，k 是路径段数。对于高并发场景，这点差异会被放大。"

---

## 与 Kama 的对比（面试展示技术视野）

| 对比维度 | Kama Router | 你的 Router |
|---|---|---|
| 动态路由实现 | `std::regex` 正则 | Token 分割 + string 比较 |
| 路由注册方式 | Handler 抽象类 | `std::function` 回调 |
| 性能 | 每个请求跑一次正则引擎 | O(k) 字符串比较 |
| 复杂度 | 高（正则编写容易出错） | 低（只需理解 `:param` 占位符） |
| 表达能力 | 强（任意正则模式） | 中（仅支持 `:param` 占位符） |
| 上手难度 | 需要懂正则 | 看一遍就懂 |

你可以说："我选择了 token 分割而非正则，因为 RESTful API 的路由模式足够简单，`/users/:id/posts/:postId` 这种形式不需要正则的灵活性。如果未来需要更复杂的匹配（比如 `/files/*` 通配），可以用双表策略——先走 token 快速路径，匹配不到再用正则兜底。"

---

## 你需要给 HttpRequest 加的东西

当前你的 `HttpRequest` 只有存储 path 的字段，没有路径参数。Router 匹配到动态路由后，需要把 `:id` 对应的值 `"123"` 注入到 Request 对象中，handler 才能取到。

```cpp
// 需要添加到 HttpRequest 中：
class HttpRequest {
public:
    // 新增：路径参数存取（Router 匹配到动态路由后注入）
    void addPathParameter(const std::string& key, const std::string& value);
    std::string getPathParameter(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> path_parameters_;  // 新增
};
```

路由匹配完成后，Router 做：

```cpp
// Router::route() 内部，匹配到动态路由后
for (auto& [key, value] : matched_params) {
    const_cast<HttpRequest&>(req).addPathParameter(key, value);
}
```

这样 handler 里就能直接：

```cpp
auto userId = req.getPathParameter("id");  // 拿到 ":id" 的实际值
```

---

## 如何在 http_conn 中接入 Router

修改 `process_read()` 和 `process()`：

```cpp
// 原来：process_read() → do_request() → 永远读文件
// 现在：process_read() → router_.route() → handler 写入 buffer
//       ↓ 如果 route() 返回 false（未匹配）
//       → 兜底：尝试当静态文件处理（保留原来的文件服务能力）
```

关键改动点：

```cpp
// HttpConnect 新增成员
class HttpConnect {
    // ...
    Router* router_;  // 指向全局 Router 实例（由 WebServer 持有）
    Buffer* response_buf_;  // handler 写入响应的 buffer
    // ...
};

// 或者在 process_read() 中：
StatusCode process_read() {
    auto ret = m_context_.parse_request(m_read_buffer_);
    if (ret == StatusCode::SUCCESS) {
        // 先尝试路由分发
        if (router_.route(m_context_.get_request(), &m_response_buffer_)) {
            return StatusCode::OK;  // handler 已生成响应
        }
        // 未匹配路由 → 退回文件服务模式（兼容旧行为）
        return do_request();
    }
    return ret;
}
```

---

## 完整示例：注册 5 条路由 + curl 测试

以下是一个可以用在实际代码中的注册场景：

```cpp
// ===== 在 WebServer::start() 中初始化 Router =====

Router router;

// 1. 首页 — 返回 index.html（静态路由，兜底走文件服务）
router.addRoute(Method::GET, "/", [](const HttpRequest& req, Buffer* buf) {
    // 返回 false 让调用方退回到文件服务
    return false;
});

// 2. 一个简单的静态路由 — 返回纯文本
router.addRoute(Method::GET, "/hello", [](const HttpRequest& req, Buffer* buf) {
    std::string body = "<html><body><h1>Hello from Router!</h1></body></html>";
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n" + body;
    buf->append(response);
    return true;
});

// 3. 动态路由 — 用户详情页
router.addRoute(Method::GET, "/users/:id", [](const HttpRequest& req, Buffer* buf) {
    auto userId = req.getPathParameter("id");
    std::string body = "{ \"user_id\": " + userId + ", \"name\": \"User-" + userId + "\" }";
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n" + body;
    buf->append(response);
    return true;
});

// 4. 嵌套动态路由
router.addRoute(Method::GET, "/posts/:postId/comments/:commentId",
    [](const HttpRequest& req, Buffer* buf) {
        auto postId = req.getPathParameter("postId");
        auto commentId = req.getPathParameter("commentId");
        std::string body = "Post " + postId + ", Comment " + commentId;
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n" + body;
        buf->append(response);
        return true;
    });

// 5. 同一 path 不同 method 走不同 handler
router.addRoute(Method::GET,  "/api/data", GetDataHandler);
router.addRoute(Method::POST, "/api/data", PostDataHandler);
```

### curl 测试方法

```bash
# 编译并启动服务器后，在另一个终端：

# 测试 1：静态路由 /hello
curl -v http://localhost:8888/hello
# 预期：<h1>Hello from Router!</h1>

# 测试 2：动态路由 /users/123
curl -v http://localhost:8888/users/123
# 预期：{ "user_id": 123, "name": "User-123" }

# 测试 3：换个 id 值
curl -v http://localhost:8888/users/999
# 预期：{ "user_id": 999, "name": "User-999" }

# 测试 4：嵌套动态路由
curl -v http://localhost:8888/posts/42/comments/7
# 预期：Post 42, Comment 7

# 测试 5：未注册的路径 — 应返回 404
curl -v http://localhost:8888/notexist
# 预期：404 Not Found

# 测试 6：首页 — 退回文件服务
curl -v http://localhost:8888/
# 预期：返回 root/index.html 的内容
```

---

## 文件组织

```
router/
  Router.h      — Router 类声明 + DynamicRoute + 实现
```

建议全部写在 `Router.h` 头文件中（不到 200 行），方便后续集成。

---

## 实现步骤建议

1. **先给 HttpRequest 加 `path_parameters_`**（3 个方法，5 分钟）
2. **写 Router 的数据结构**：`static_routes_` 和 `dynamic_routes_`
3. **实现 `addRoute()`**：按 pattern 中是否含 `:` 来判断放静态表还是动态表
4. **实现 `route()` 匹配逻辑**：先查静态表 → 再遍历动态表
5. **写一个独立测试**：不需要启动服务器，直接 new Router → addRoute → 构造 HttpRequest → route → 验证结果

---

## 自检清单

完成后逐条过：

- [ ] `router.addRoute(GET, "/hello", handler)` — 能注册静态路由
- [ ] `router.addRoute(GET, "/users/:id", handler)` — 能注册动态路由，自动识别 `:` 前缀
- [ ] `GET /hello` → handler 被调用 → 响应正确
- [ ] `GET /users/42` → handler 拿到 `req.getPathParameter("id") == "42"`
- [ ] `GET /users/999` → 同一个 handler 拿到 `req.getPathParameter("id") == "999"`
- [ ] `GET /notexist` → `route()` 返回 false → 调用方设 404
- [ ] 同一个 path + 不同 method（如 GET 和 POST 都注册 `/api/data`）走不同的 handler
- [ ] 不同 method + 不同 path 可以各自注册独立的 handler
- [ ] `route()` 方法不抛异常——未匹配返回 false

---

## 完成标准

- 新建 `router/Router.h`，可以独立编译，不依赖 socket/epoll
- 写一个独立测试程序 `router_test.cpp`：
  - 注册 3 条不同的路由（1 条静态 + 2 条动态）
  - 用不同的 HttpRequest 对象测试路由分发
  - 验证 handler 被正确调用
  - 验证未匹配路径返回 false
- 跑通本章 6 个 curl 测试

# 05 · Router 路由系统

## 目标

替换 `http_conn::do_request()` 中"所有请求只返回 index.html"的硬编码逻辑，实现路径→处理函数的灵活分发。

---

## 你的现状

```cpp
// http_conn.h 当前
HTTP_CODE do_request() {
    m_file_ = m_index_path_;                   // 永远 "./root/index.html"
    stat(m_file_, &m_file_stat_);
    m_file_fd_ = open(m_file_, O_RDONLY);
    // ...
    return FILE_REQUEST;
}
```

无论请求什么路径，都返回同一个文件。这不是路由，这是单页硬编码。

---

## 设计要求

### 路由的两种模式

```
静态路由:  GET /login           → LoginHandler
           GET /api/users       → UserListHandler

动态路由:  GET /users/123       → UserHandler(id=123)
           GET /posts/456/comment/789  → CommentHandler(postId=456, commentId=789)
```

### 两种实现方式的选择

| 方式 | 优点 | 缺点 |
|------|------|------|
| 正则匹配（Kama 的方案） | 灵活，任意模式 | 性能差，面试可能问优化 |
| Token 分割（推荐你实现） | 简单，面试好讲 | 模式表达能力有限 |

**推荐你实现 Token 分割**。原理：

```
注册: router.addRoute(GET, "/users/:id", handler)
内部存储: ["users", ":id"]

请求: GET /users/123
匹配: "users" 对上 "users", ":id" 匹配 "123"
提取: pathParameters_["id"] = "123"
```

### 面试你为什么这样设计

> "我没有用正则，而是用 token 分割来匹配动态路由。原因是正则匹配在每个请求时都要做 O(n) 的正则引擎调用，而 token 分割只需要 O(k) 的字符串比较，k 是路径的段数。对于高并发场景，这点差异会被放大。如果需要更复杂的匹配模式，再引入正则作为补充方案。"

---

## 接口定义

### Handler 抽象（调用方怎么注册）

你有两种选择，二选一：

```cpp
// 方案A：函数回调（简单，推荐起步用）
using HandlerCallback = std::function<void(const HttpRequest&, HttpResponse*)>;

// 方案B：抽象接口（Kama 的方案，支持带状态的 handler）
class RouterHandler {
public:
    virtual ~RouterHandler() = default;
    virtual void handle(const HttpRequest& req, HttpResponse* resp) = 0;
};
```

**建议从方案A开始**，后面想升级再改成方案B。面试时能讲"为什么从 function 改成 interface"就是加分。

### Router 类接口

```cpp
class Router {
public:
    // ===== 路由注册 =====
    // 静态路由: addRoute(GET, "/login", handler)
    // 动态路由: addRoute(GET, "/users/:id", handler)
    //            addRoute(GET, "/posts/:postId/comments/:commentId", handler)
    void addRoute(HttpRequest::Method method,
                  const std::string& pattern,
                  HandlerCallback handler);

    // ===== 路由分发 =====
    // 输入：请求对象 → 匹配路由 → 调用对应 handler → 填充响应
    // 返回：true = 匹配到路由，false = 未匹配（404）
    bool route(const HttpRequest& req, HttpResponse* resp);

    // ===== 统计（调试/监控用） =====
    size_t routeCount() const;

private:
    // 你需要设计内部数据结构
    // 提示：静态路由用 unordered_map 或 map
    //       动态路由用 vector<RouteEntry>
    // 
    // struct RouteEntry {
    //     HttpRequest::Method method;
    //     std::vector<std::string> tokens;  // 预分割的路径 tokens
    //     HandlerCallback handler;
    // };
};
```

### 路由匹配算法（你来设计）

```
route(req):
  1. 先查静态路由表（O(1) 或 O(log n)）
     → 匹配到就执行 handler，return true

  2. 静态没匹配到 → 遍历动态路由表
     对每条 RouteEntry:
       将 req.path() 按 '/' 切成 tokens
       逐段和 RouteEntry.tokens 比较
         "users" == "users"     → 继续
         ":id"  匹配 "123"      → 存入 pathParameters["id"] = "123"
         不匹配 → 跳过这条路由
     全部匹配 → 执行 handler，return true

  3. 所有路由都不匹配 → return false
```

### 面试追问

> "动态路由里 URL 参数怎么传给 handler？"
> → 通过 `HttpRequest::addPathParameter(key, value)`，handler 从 req 里取。
> 这就是为什么 HttpRequest 里需要 `pathParameters_` 这个字段。

> "如果静态路由和动态路由都匹配了怎么办？"
> → 先查静态路由，匹配到就返回。静态路由优先。

---

## 你的 Router 需要支持的功能清单

- [ ] 注册静态路由（精确路径匹配）
- [ ] 注册动态路由（`:param` 占位符）
- [ ] 路由匹配时设置 `HttpRequest::addPathParameter()`
- [ ] 未匹配时设置 404 响应
- [ ] 同一 method + 不同 path 可以注册不同 handler
- [ ] 同一 path + 不同 method 可以注册不同 handler
- [ ] `route()` 方法不抛异常——未匹配返回 false，调用方设 404

---

## 与 Kama 的对比（面试展示技术视野）

| | Kama Router | 你的 Router（建议） |
|---|---|---|
| 动态路由实现 | `std::regex` 正则 | Token 分割 + string 比较 |
| 路由注册方式 | Handler 抽象类 | `std::function` 回调 |
| 性能 | 每个请求跑一次正则 | O(k) 字符串比较 |
| 复杂度 | 高（正则编写容易出错） | 低 |
| 表达能力 | 强（任意正则） | 中（仅支持 `:param` 占位符） |

面试时可以说："我选择了 token 分割而非正则，因为我的路由模式足够简单（RESTful 风格），不需要正则的灵活性。如果未来需要复杂的匹配模式，可以用双表策略——先走 token 匹配快速路径，匹配不到再走正则兜底。"

---

## 文件组织建议

```
router/
  Router.h      — Router 类声明 + RouteEntry 私有结构
```

不需要 .cpp — Router 可以在头文件中内联实现（模板和函数对象多，分离编译麻烦）。

---

## 完成标准

- `router.addRoute(GET, "/hello", handler)` — 能注册
- `GET /hello` → handler 被调用 → 响应正确
- `GET /users/42` → 动态路由 `/users/:id` → handler 拿到 `req.path()` + `req.getPathParameter("id") == "42"`
- `GET /notexist` → 无匹配 → 404
- 写 3 条不同路由的测试，每个 handler 设置不同的响应体来验证路由分发正确

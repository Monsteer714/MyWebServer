# 06 · Middleware 中间件链

## 目标

实现请求的前置处理链和后置处理链。这是**简历上含金量最高的设计模式实践**之一。

---

## 为什么需要中间件

你的服务器现在没有中间件，假设要加以下功能：

```
需求1: 打印每个请求的 method + path + 响应状态码（日志）
需求2: 返回 CORS 头，允许跨域访问
需求3: 限制每个 IP 的请求频率（限流）
需求4: 检查用户是否登录（鉴权）
```

**没有中间件的话**：每个 handler 里都要加这些逻辑，代码重复 N 处。

**有中间件的话**：注册一次，所有请求自动经过。

---

## 设计模式：Chain of Responsibility（责任链）

```
请求进来
  │
  ▼
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Middleware A │ → │ Middleware B │ → │ Middleware C │ → Router → handler
│  before()    │    │  before()    │    │  before()    │
└─────────────┘    └─────────────┘    └─────────────┘
                                               │
                                               ▼
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Middleware C │ ← │ Middleware B │ ← │ Middleware A │
│  after()     │    │  after()     │    │  after()     │
└─────────────┘    └─────────────┘    └─────────────┘
  │
  ▼
响应发出

关键设计：before() 按注册顺序执行，after() 按注册逆序执行。
         像剥洋葱——外层中间件最先处理请求，最后处理响应。
```

### 面试你为什么这样设计

> "中间件链本质是责任链模式。before() 正序遍历做预处理（鉴权、限流、CORS），after() 逆序遍历做后处理（压缩、日志）。逆序的关键在于——最外层的中间件应该最后看到响应，这样它才能记录完整的状态码和响应时间。如果不用中间件链，这些横切关注点就会散落在每个 handler 里，违背 DRY 原则。"

---

## 接口定义

### Middleware 抽象基类

```cpp
class Middleware {
public:
    virtual ~Middleware() = default;

    // 请求前置处理 — 在路由分发之前执行
    // 参数是 mutable 引用，可以修改请求（如添加属性、改写 header）
    // 返回 void — 如果中间件要拒绝请求，抛异常或直接设 resp
    virtual void before(HttpRequest& req) {}

    // 响应后置处理 — 在 handler 执行完之后执行
    // 参数是 mutable 引用，可以修改响应（如加 CORS 头、压缩 body）
    virtual void after(HttpResponse& resp) {}
};
```

### MiddlewareChain 容器

```cpp
class MiddlewareChain {
public:
    // 注册中间件。顺序很重要——先注册的先执行 before()
    void addMiddleware(std::unique_ptr<Middleware> mw);

    // 正序执行所有 before()
    void processBefore(HttpRequest& req);

    // 逆序执行所有 after()
    void processAfter(HttpResponse& resp);

    size_t size() const;  // 调试用

private:
    std::vector<std::unique_ptr<Middleware>> chain_;
    // 思考：为什么用 vector 而不是 list？
    // 提示：vector 缓存友好，随机遍历开销小；list 虽插入快但此处不频繁插入
};
```

---

## 你需要实现的示例中间件（验证框架正确性）

### 示例1：日志中间件

```cpp
class LoggingMiddleware : public Middleware {
public:
    void before(HttpRequest& req) override {
        // 记录请求到达时间、method、path
        // 你可以用自带的 LOG_INFO 宏
        // 思考：before 里只能记录请求信息，状态码怎么办？
        // 提示：把开始时间存在某个地方，after 里计算耗时
    }

    void after(HttpResponse& resp) override {
        // 记录响应状态码、处理耗时
    }
};
```

### 示例2：CORS 中间件

```cpp
class CorsMiddleware : public Middleware {
public:
    void before(HttpRequest& req) override {
        // OPTIONS 请求（预检）不需要走路由
        // 直接设好 CORS 响应头返回
    }

    void after(HttpResponse& resp) override {
        // 给每个响应加：
        // Access-Control-Allow-Origin: *
        // Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
        // Access-Control-Allow-Headers: Content-Type
    }
};
```

### 设计思考题

> "OPTIONS 预检请求应该在 before() 中如何拦截？"
> 
> 方案A：before() 检测到 OPTIONS → 直接设 resp → 抛异常跳过后续处理
> 方案B：before() 返回 bool，false 表示终止链
> 方案C：before() 检测到 OPTIONS → 设 resp 后，在 Router 之前检查 resp 是否已被填充
> 
> 各方案优劣是什么？你选哪个？为什么？

---

## 与主流程的集成点

```
HttpServer::handleRequest(req, resp):

  1. middlewareChain_.processBefore(req);   // 前置中间件

  2. router_.route(req, resp);             // 路由分发（你的 Router）

  3. middlewareChain_.processAfter(resp);   // 后置中间件
```

前置和后置之间是路由处理，中间件完全不懂路由细节，只关心请求和响应对象。

---

## 自检清单

- [ ] 空链（没注册任何中间件）→ processBefore/After 不崩溃
- [ ] 注册一个 LoggingMiddleware → before 和 after 都被调用
- [ ] 注册两个中间件 → before 顺序 = 注册顺序，after 顺序 = 注册逆序
- [ ] OPTIONS 请求被 CorsMiddleware.before() 正确拦截
- [ ] Middleware 不依赖 epoll / socket / 线程（可以独立测试）
- [ ] 中间件的 after() 在 handler 抛异常时是否还会执行？

---

## 完成标准

- 新建 `middleware/Middleware.h` + `middleware/MiddlewareChain.h`
- 至少实现 `LoggingMiddleware` 和 `CorsMiddleware` 两个示例
- 测试：请求 → before 日志 → 路由处理 → after 日志 + CORS 头
- 验证逆序：注册 [A, B, C] → before: A→B→C, after: C→B→A

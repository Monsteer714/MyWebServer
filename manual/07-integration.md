# 07 · 集成 — 把所有模块接入 http_conn

## 目标

把前六章写的新模块**接入你现有的 http_conn**，而不是重写整个类。

---

## 集成原则

```
http_conn 保留：
  ✅ Socket I/O（read_once, write, sendfile）
  ✅ epoll 管理（addfd, modfd, removefd）
  ✅ 连接生命周期（init, close_conn）
  ✅ 对外接口（m_state_, m_op_finish_flag_）—— 线程池需要

http_conn 不再自己做：
  ❌ HTTP 解析 → 委托给 HttpContext
  ❌ HTTP 响应生成 → 委托给 HttpResponse
  ❌ 路径分发 → 委托给 Router
  ❌ 中间件处理 → 委托给 MiddlewareChain
```

---

## 重构后的 http_conn 应该长什么样

```cpp
class http_conn {
public:
    // ===== 公开常量（保持兼容） =====
    constexpr static int READ_BUFFER_SIZE = 2048;
    constexpr static int WRITE_BUFFER_SIZE = 1024;

    // ===== 线程池接口（保持不变） =====
    inline static int m_epollfd_ = -1;
    int m_state_ = {};           // 0:read, 1:write
    int m_error_flag_ = {};
    int m_op_finish_flag_ = {};

    // ===== 生命周期 =====
    http_conn();
    ~http_conn();
    void init(int client_fd, int trigMode);
    void init();                 // Keep-Alive 复用重置

    // ===== I/O =====
    bool read_once();            // 保持不变
    bool write();                // 需要修改：用响应模型而非 m_write_buffer_

    // ===== 业务处理（被线程池调用） =====
    void process();              // 需要修改：委托给 Context + Router

private:
    // ===== I/O 底层（保持） =====
    int m_client_fd_;
    int m_TRIGMode_;
    bool m_linger_;
    char m_read_buffer_[READ_BUFFER_SIZE];
    char m_write_buffer_[WRITE_BUFFER_SIZE];
    // ... epoll 管理 / setNonBlocking / modfd / removefd ...

    // ===== 新增：HTTP 模块（替代之前散落的解析字段） =====
    HttpContext context_;        // 解析器
    // Router 和 MiddlewareChain 应该由谁持有？
    // 思考：每个连接一个 Router 还是一起共享一个？
    // 提示：路由表是全局的，所有连接共享；但 http_conn 需要引用它

    // ===== 文件发送（保持） =====
    SEND_STATE m_send_state_;
    int m_file_fd_;
    // ... 但 m_file_address / m_file_stat_ 可以移到 HttpResponse 中
};
```

---

## 关键设计决策（你需要自己思考并实现）

### 决策1：Router 放在哪里？

```
方案A：static Router（所有连接共享一个路由表）
  ✅ 内存只一份，路由注册在启动时完成
  ✅ 面试好讲："路由表是全局配置，不需要每个连接一份"
  ❌ 需要在 http_conn 中引用静态对象

方案B：每个 http_conn 持有一个 Router*
  ✅ 灵活
  ❌ 每个连接一个指针，意义不大
```

**建议**：方案A。`http_conn` 里加一个 `static Router* router_;`，`main()` 中注册路由后设置。

### 决策2：process() 函数怎么改？

```
现在的 process():
  ├── process_read()      — 解析
  │   ├── parse_line() → parse_request_line / parse_header_line / parse_content_line
  │   └── do_request()    — 路径分发 → 准备文件
  └── process_write()     — 生成响应字符串 → 设置发送状态

重构后 process():
  ├── context_.parse(m_read_buffer_, m_read_idx_)
  │   └── 解析字节流 → 填充 HttpRequest
  ├── if (context_.gotAll()):
  │   ├── middlewareChain_->processBefore(context_.request())
  │   ├── router_->route(context_.request(), &response_)
  │   ├── middlewareChain_->processAfter(response_)
  │   └── response_.appendToBuffer(m_write_buffer_)  // 序列化
  └── 设置 m_send_state_ 和 m_bytes_to_send_
```

### 决策3：文件发送逻辑怎么适配？

```
现在: m_file_address (mmap指针), m_file_stat_, m_file_fd_ 各自独立
重构后: HttpResponse::isFileResponse() + HttpResponse::fileFd() + HttpResponse::fileSize()
        write() 里根据 isFileResponse() 选择 sendfile vs 直接 write
```

---

## 需要修改的文件清单

| 文件 | 改动 |
|------|------|
| `http_conn/http_conn.h` | **核心重构**：去掉解析方法，持有 HttpContext + Router* + HttpResponse |
| `http/HttpRequest.h` | 新建（01 章完成） |
| `http/HttpResponse.h` | 新建（02 章完成） |
| `http/HttpContext.h` | 新建（03 章完成） |
| `http/MimeTypes.h` | 新建（04 章完成） |
| `router/Router.h` | 新建（05 章完成） |
| `middleware/Middleware.h` | 新建（06 章完成） |
| `middleware/MiddlewareChain.h` | 新建（06 章完成） |
| `main.cpp` | 小改：注册路由 |
| `webserver.h` | 小改：设置 Router 到 http_conn |

---

## 渐进式重构策略（按 commit 拆分）

不要一次改完所有文件。按以下顺序逐个 commit，每步都是可编译可运行的：

```
Commit 1: 新建 HttpRequest.h  → 不影响现有代码
Commit 2: 新建 HttpResponse.h  → 不影响现有代码
Commit 3: 新建 HttpContext.h + 单元测试 → 不影响现有代码
Commit 4: 新建 MimeTypes.h    → 不影响现有代码
Commit 5: 新建 Router.h       → 不影响现有代码
Commit 6: 新建 Middleware.h + MiddlewareChain.h → 不影响现有代码
Commit 7: 重构 http_conn.h → 集成上述所有模块 ← 核心 commit
Commit 8: 修改 main.cpp 注册路由 + 修改 webserver.h
Commit 9: 压测验证 + 火焰图对比
```

---

## 兼容性检查清单

重构完成后逐条验证：

- [ ] 服务器能正常启动，能接受连接
- [ ] `GET /` → 返回 200 + index.html（Content-Type 正确）
- [ ] `GET /nonexist.html` → 返回 404
- [ ] 浏览器访问 `http://localhost:8888/`，页面正常显示
- [ ] Keep-Alive 连接复用正常（同一个连接发多个请求）
- [ ] wrk 压测不崩溃
- [ ] 线程池的 Reactor/Proactor 模式都正常
- [ ] 定时器超时关闭仍然有效
- [ ] `m_state_` / `m_op_finish_flag_` 行为不变（线程池接口兼容）

---

## 完成标准

- 服务器功能完整：多路径路由、MIME 正确、404 正确
- 线程池接口不变，压测通过
- 代码行数：`http_conn.h` 从 550 行减少到 300 行以下
- http/ 目录下新增 3 个 .h 文件，每个 < 150 行

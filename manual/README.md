# HTTP 模块重构指引

## 目标

将 `http_conn.h` 从一个 550 行的"全能类"，拆分为职责清晰的分层模块。

### 现在的架构（一个类干所有事）

```
http_conn (550行)
  ├── Socket I/O (read/write/sendfile)
  ├── epoll 管理 (addfd/modfd/removefd)
  ├── HTTP 解析 (状态机, 嵌在类里)
  ├── 响应生成 (add_response 系列, 嵌在类里)
  ├── 文件服务 (open/mmap/sendfile, 嵌在类里)
  └── 连接生命周期 (close_conn/init, 嵌在类里)
```

### 目标架构（分层，每层可独立测试和面试讲解）

```
http/
  HttpRequest   — 请求数据模型（纯数据，不依赖任何 I/O）
  HttpResponse  — 响应数据模型 + 序列化（纯数据）
  HttpContext   — 解析器（byte stream → HttpRequest）
  Router        — 路由分发（path → handler）
  MiddlewareChain — 中间件链（请求/响应切面拦截）

http_conn       — 精简为 I/O + 生命周期管理
                  + 持有上述对象，委托工作
```

---

## 进度总览

| 章节 | 内容 | 难度 | 估时 | 状态 |
|------|------|------|------|------|
| [01](./01-httprequest.md) | HttpRequest 请求模型 | ⭐⭐ | 半天 | ⬜ |
| [02](./02-httpresponse.md) | HttpResponse 响应模型 | ⭐⭐ | 半天 | ⬜ |
| [03](./03-httpcontext.md) | HttpContext 解析器 | ⭐⭐⭐ | 1-2天 | ⬜ |
| [04](./04-mime-query.md) | MIME 映射 + Query String | ⭐ | 半天 | ⬜ |
| [05](./05-router.md) | Router 路由系统 | ⭐⭐⭐ | 2天 | ⬜ |
| [06](./06-middleware.md) | Middleware 中间件链 | ⭐⭐⭐ | 1-2天 | ⬜ |
| [07](./07-integration.md) | 集成到 http_conn | ⭐⭐ | 1天 | ⬜ |
| [08](./08-面试要点.md) | 面试怎么讲你的 HTTP 设计 | — | — | ⬜ |

---

## 使用方式

1. 从 01 开始，**读完"设计要求"和"接口定义"两节**
2. **关掉文件**，自己写代码实现
3. 实现完后，回来对照"自检清单"确认没有遗漏
4. 标记该章节 ✅ 完成
5. 进入下一章

> 每个章节只给接口签名和设计思路，不给你完整实现代码。
> 目的是让你自己思考"为什么这样设计"，面试时才能讲清楚。

---

## 前置条件

- 你已经完整理解 `http_conn.h` 的现有代码
- 你已经看过 Kama-HTTPServer 中 `HttpRequest`/`HttpResponse`/`HttpContext` 的设计
- 你的编辑器/IDE 已配置好 C++20

---

## 设计原则（贯穿全部章节）

以下原则面试时每条都能展开聊：

1. **单一职责** — 每个类只做一件事，改解析不用动 I/O，改 I/O 不用动解析
2. **依赖倒置** — 数据模型（Request/Response）不依赖 epoll/socket/文件系统
3. **接口先行** — 先定义"这个类对外暴露什么"，再想内部怎么实现
4. **可测试性** — 解析器可以脱离网络单独测试（喂字节流，检查解析结果）
5. **组合优于继承** — http_conn 不继承任何 HTTP 类，而是持有它们

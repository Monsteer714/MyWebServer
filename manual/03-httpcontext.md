# 03 · HttpContext 解析器

## 目标

把 `http_conn` 中 `parse_line()` / `process_read()` / `parse_request_line()` / `parse_header_line()` / `parse_content_line()` 这些**解析逻辑**提取为独立的 `HttpContext` 类。

---

## 你的现状

```cpp
// http_conn.h 当前解析逻辑链路
char m_read_buffer_[2048];   // 原始字节
ssize_t m_check_idx_;        // 解析光标
ssize_t m_start_line_;       // 当前行起始

LINE_STATE parse_line();     // 找 \r\n，置 \0
HTTP_CODE process_read();    // 循环 parse_line → 分发到三种状态
HTTP_CODE parse_request_line(char* text);   // "GET /path HTTP/1.1"
HTTP_CODE parse_header_line(char* text);    // "Host: ..."
HTTP_CODE parse_content_line(char* text);   // body 收集
```

**问题**：
1. 解析状态（m_check_idx_, m_start_line_, m_check_state_）和解析结果（m_method_, m_path, m_host_）都存在同一个类里
2. Keep-Alive 复用时 `init()` 只清零了索引，但解析状态机的 reset 逻辑和请求数据的 reset 混在一起
3. 解析器不可独立测试——必须构造完整的 http_conn + epoll + socket

---

## 设计要求

### 核心架构

```
HttpContext（解析器）
  ├── 持有 HttpRequest（解析结果）
  ├── 内部状态机：RequestLine → Headers → Body → Done
  ├── 接收字节流 → 填充 HttpRequest → 标记 "是否解析完成"
  └── 提供 reset() 用于连接复用

关键设计：HttpContext 是"过程"，HttpRequest 是"结果"
```

### 面试你为什么这样设计

> "我把解析器从连接类里拆出来，核心原因是'让解析逻辑可测试'。解析器只依赖字节流，输入是一段 `char* + length`，输出是一个 `HttpRequest` 对象。我可以在单元测试里喂各种边界情况——不完整的请求行、缺少 `\r\n`、超长的 header、大 body——全部脱离网络测试。这在生产环境非常重要，HTTP 解析器是安全攻击的第一道防线。"

---

## 接口定义

```cpp
class HttpContext {
public:
    // ===== 解析状态机 =====
    // 设计决策：为什么用 enum 而不是你现在的 enum CHECK_STATE？
    // 提示：状态名要自解释，别人读代码能猜到流程
    enum class ParseState {
        kExpectRequestLine,   // 等待请求行
        kExpectHeaders,       // 等待 Headers
        kExpectBody,          // 等待 Body
        kGotAll,              // 解析完成
    };

    // ===== 构造 =====
    HttpContext();

    // ===== 核心解析接口 =====
    // 输入：字节流起始指针 + 长度
    // 输出：bool — true 表示格式正确，false 表示语法错误
    // 副作用：填充内部持有的 HttpRequest 对象
    // 
    // 设计决策：多次调用 parse() 能否正确处理粘包/半包？
    // 提示：这是 HTTP over TCP 的核心挑战——TCP 是字节流，
    // 一次 read 可能拿到 0.3 个请求，也可能拿到 2.5 个请求。
    // 你的设计需要支持：状态机暂停 → 等下一批数据到来 → 继续解析
    bool parse(const char* data, size_t len);

    // ===== 状态查询 =====
    bool gotAll() const;            // 一个完整请求解析完成？
    ParseState state() const;       // 当前状态（调试用）

    // ===== 获取解析结果 =====
    const HttpRequest& request() const;
    HttpRequest& request();

    // ===== 重置 =====
    // 设计决策：为什么 reset() 需要特殊设计？
    // 提示：连接复用（Keep-Alive）场景下，同一个连接会发多个请求。
    // 旧请求的数据必须完全清空，但要避免频繁 new/delete 内存分配。
    // Kama 用 swap() 技巧：用一个临时空对象和旧对象交换。
    void reset();

private:
    // 你来设计状态机内部方法
    // 提示：至少需要：
    //   - 找行尾（处理 \r\n 和 \n 两种情况）
    //   - 解析请求行 → 填充 method/path/version/query
    //   - 解析一行 header → 填充 key-value
    //   - 检测空行 → 状态转换
    //   - 收集 body（按 Content-Length 精确读取）
};
```

---

## 状态机设计（你需要自己实现的逻辑）

```
                      ┌──────────────────────────────┐
                      │     kExpectRequestLine        │
                      │  找 \r\n → 按空格切三段       │
                      │  method / path / version      │
                      │  切 '?' → path + queryString  │
                      └──────────────┬───────────────┘
                                     │ 成功
                                     ▼
                      ┌──────────────────────────────┐
                      │     kExpectHeaders            │
                      │  逐行读 → 找到 ':' → 存 header│
                      │  空行 → 状态转换判断          │
                      └──────┬───────────┬───────────┘
                             │           │
                    GET/HEAD/     POST/PUT 且有
                    DELETE等      Content-Length > 0
                         │           │
                         ▼           ▼
                    kGotAll    kExpectBody
                                   │
                                   │ 读到 Content-Length 字节
                                   ▼
                                kGotAll
```

### 状态转换关键判断

```cpp
// 伪代码 — 你来实现

// 在 kExpectHeaders 中检测到空行（\r\n 在行首）：
if (当前行是空行) {
    if (method 是 POST || method 是 PUT) {
        if (Content-Length > 0) {
            → kExpectBody
        } else {
            → kGotAll
        }
    } else {
        → kGotAll  // GET/HEAD/DELETE 没有 body
    }
}
```

### 边界情况（面试必考）

设计你的解析器时处理以下情况：

| 场景 | 行为 |
|------|------|
| 数据不完整（请求行只收到了 `"GET /pa"` 没收到 `\r\n`） | parse() 返回 true（没有语法错），但 gotAll() 返回 false |
| 第一行不是 `METHOD PATH VERSION` 格式 | parse() 返回 false |
| header 行没有 `:` | parse() 返回 false |
| Content-Length 的值不是数字 | parse() 返回 false |
| body 数据不足（Content-Length=100 但只读到 60 字节） | 保持 kExpectBody，等下次 parse() 继续读 |
| 收到超出 Content-Length 的额外数据（粘包：一个请求 + 半个下一个请求） | 只取 Content-Length 长度作为 body，剩余数据留给下一个请求 |

---

## 与 HttpRequest 的接口对接

`HttpContext::parse()` 过程填充 HttpRequest：

```
parse(const char* data, size_t len):
  ├── 找 \r\n → 切出第一行
  │   ├── 按空格切: METHOD PATH VERSION
  │   │   └── request_.setMethod(start, space)
  │   │   └── PATH 中找 '?' → request_.setPath() + request_.addQueryParameter()
  │   │   └── request_.setVersion()
  │   └── state_ = kExpectHeaders
  ├── 逐行解析 header
  │   ├── 找 ':' → request_.addHeader(key, value)
  │   └── 空行 → 判断是否进 kExpectBody
  └── 解析 body
      └── request_.setBody(data, Content-Length)
```

---

## 与现有代码的映射

| 现在的 http_conn 方法 | 移到 HttpContext 的方法 |
|---|---|
| `parse_line()` | `findLineEnd()` 或直接内联在 parse() 里 |
| `process_read()` | `parse()` 内部循环 |
| `parse_request_line(char* text)` | `parseRequestLine(const char*, const char*)` |
| `parse_header_line(char* text)` | `parseHeaderLine(const char*, const char*)` |
| `parse_content_line(char* text)` | `parseBody(const char*, size_t available)` |
| `m_check_state_` (CHECK_REQUEST/CHECK_HEADER/CHECK_CONTENT) | `state_` (kExpectRequestLine/kExpectHeaders/kExpectBody/kGotAll) |
| `m_check_idx_` / `m_start_line_` | parse() 内部局部变量或成员（你来设计） |
| `m_read_buffer_[]` | 不再由 Context 持有，由调用方传入 |

---

## 自检清单

- [ ] 这个类能否脱离 http_conn / epoll / socket 编译？
- [ ] `reset()` 是否正确清空所有状态和请求数据？多次 reset → parse 是否稳定？
- [ ] 半个请求行（数据不完整）→ gotAll() 返回 false → 再来一批数据 → 继续解析成功
- [ ] 一个完整请求 + 半个下一个请求 → gotAll() 返回 true → reset() → 下次 parse() 能取到剩余数据
- [ ] 非法请求行 → parse() 返回 false
- [ ] POST 请求有 Content-Length → 正确进 kExpectBody → 读满 body → 进 kGotAll
- [ ] GET 请求无 Content-Length → 空行后直接 kGotAll
- [ ] 你测试了至少 3 个完整 HTTP 请求的解析

---

## 完成标准

- 新建 `http/HttpContext.h`，只依赖 `HttpRequest.h`
- 写一个独立测试（不需要启动服务器）：
  1. 构造一个完整 HTTP GET 请求的字节流
  2. `ctx.parse(data, len)` → `gotAll() == true`
  3. 检查 `ctx.request().method()`, `path()`, `getHeader("Host")`
  4. 测试半包场景
  5. 测试非法格式返回 false

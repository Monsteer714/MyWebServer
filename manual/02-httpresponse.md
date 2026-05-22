# 02 · HttpResponse 响应模型

## 目标

把 `http_conn` 中的 `add_response()` / `add_status_line()` 等用 `vsnprintf` 拼字符串的逻辑，替换为**模型化响应对象 + 统一序列化**。

---

## 你的现状

```cpp
// http_conn.h 现在的响应生成方式：
bool add_response(const char* format, ...) {   // va_list + vsnprintf
    vsnprintf(m_write_buffer_ + m_write_idx_, ..., format, args);
}

bool add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool add_headers(const int& content_length) {
    return add_content_length(content_length)
        && add_content_type()       // 永远 "text/html"
        && add_connection()         // keep-alive / close
        && add_blank_line();
}
// ... 每个 HTTP 元素一个函数，字符串拼接
```

**问题**：
1. 格式化字符串容易出错（少写一个 `\r\n` 就坏了）
2. `Content-Type` 硬编码为 `text/html`
3. 没法在生成响应后修改 header（比如中间件想加个 CORS 头）
4. 测试响应生成需要构造完整的 http_conn 对象（耦合重）

---

## 设计要求

### 这个类的职责边界

```
HttpResponse 只管：
  ✅ 存储响应状态（statusCode, statusMessage）
  ✅ 管理 headers（增/删/改/查）
  ✅ 管理 body（设置内容、计算 Content-Length）
  ✅ 序列化为字节流（写入 Buffer）
  ✅ 提供文件响应标记（区分"发文本"还是"发文件"）

HttpResponse 不管：
  ❌ 实际发送数据（那是 http_conn 的 I/O 的活）
  ❌ 解析请求
  ❌ 路由分发
```

### 面试你为什么这样设计

> "我把响应做成模型对象而不是字符串拼接，有三个好处。一是中间件可以在序列化之前修改任意 header——比如 CORS 中间件只需要 `resp->addHeader('Access-Control-Allow-Origin', '*')`，不用知道响应体的生成细节。二是序列化逻辑集中在一个函数里，万一需要优化（比如零拷贝），只需改一个地方。三是单元测试可以直接检查响应对象的状态码和 header，不需要解析输出的字节流。"

---

## 接口定义

### 必须包含的接口

```cpp
class HttpResponse {
public:
    // ===== 状态码枚举 =====
    // 思考：为什么枚举值要显式赋值？哪些是面试中常问的状态码？
    enum class HttpStatusCode {
        kUnknown = 0,
        k200Ok = 200,
        k204NoContent = 204,
        k301MovedPermanently = 301,
        k400BadRequest = 400,
        k401Unauthorized = 401,
        k403Forbidden = 403,
        k404NotFound = 404,
        k500InternalServerError = 500,
    };

    // ===== 构造 =====
    // 设计决策：构造时指定是否默认关闭连接
    // 提示：HTTP/1.0 默认 close，HTTP/1.1 默认 keep-alive
    explicit HttpResponse(bool close = true);

    // ===== 状态行 =====
    // 设计决策：version + statusCode + statusMessage 三个字段一起设
    // 为什么不是分开设？因为状态行是一个整体，分设容易出现不一致
    void setStatusLine(const std::string& version,
                       HttpStatusCode code,
                       const std::string& message);
    void setStatusCode(HttpStatusCode code);
    HttpStatusCode statusCode() const;
    void setStatusMessage(const std::string& msg);

    // ===== 连接管理 =====
    void setCloseConnection(bool on);
    bool closeConnection() const;

    // ===== Headers 管理 =====
    // 设计决策：和 HttpRequest 一样用 map，但这里需要增/删/查
    // 思考：为什么 Response 的 headers 需要"删"，Request 不需要？
    void addHeader(const std::string& key, const std::string& value);
    void removeHeader(const std::string& key);
    std::string getHeader(const std::string& key) const;

    // ===== 便捷 Header 设置 =====
    // 设计决策：这两个是最常设的 header，给快捷方式减少调用方写错 key 的风险
    void setContentType(const std::string& type);
    void setContentLength(uint64_t length);

    // ===== Body =====
    void setBody(const std::string& body);
    const std::string& body() const;

    // ===== 文件响应 =====
    // 设计决策：文件响应的处理不同于文本响应。
    // body 存响应头，文件内容用 sendfile/writev 单独发。
    // 需要区分"这个响应包含文件"还是"只有文本"
    void setFileInfo(const std::string& filePath, int fd, off_t size);
    bool isFileResponse() const;
    const std::string& filePath() const;
    int fileFd() const;
    off_t fileSize() const;

    // ===== 序列化 =====
    // 设计决策：序列化为字节流，输出到 string 或自定义 buffer
    // 思考：为什么返回 string 而不是直接写 socket fd？
    // 提示：writev 需要 header 和 body 分开，不能混在一个 buffer 里
    void appendToBuffer(std::string& output) const;

private:
    // 你来设计内部字段
    // 提示：statusCode_, statusMessage_, version_,
    //       closeConnection_, headers_, body_,
    //       isFile_, filePath_, fileFd_, fileSize_
};
```

### 状态码 → 默认状态消息的映射

你不需要手写每个 if-else。思考用 `switch` 还是静态 `unordered_map`？

面试追问："为什么 204 No Content 不需要 body？和 200 的区别是什么？"

---

## 序列化格式（必须正确）

`appendToBuffer()` 输出的格式：

```
HTTP/1.1 200 OK\r\n
Connection: keep-alive\r\n
Content-Type: text/html\r\n
Content-Length: 1234\r\n
\r\n
<html>...</html>
```

注意：
- 状态行格式：`版本 状态码 状态消息\r\n`
- 每个 header：`Key: Value\r\n`
- 所有 header 后空一行：`\r\n`
- body 紧跟空行后

---

## 与现有代码的映射

| 现在的 http_conn 方法 | 替换为 HttpResponse |
|---|---|
| `add_status_line(200, ok_200_title)` | `resp.setStatusLine("HTTP/1.1", k200Ok, "OK")` |
| `add_content_length(n)` + `add_content_type()` + `add_connection()` + `add_blank_line()` | `resp.setContentLength(n); resp.setContentType("text/html")` |
| `add_response("%s", content)` | `resp.setBody(content)` |
| `add_headers(m_file_stat_.st_size)` | `resp.setContentLength(size); resp.setContentType(...)` |
| 整个 `process_write()` switch | `resp` 模型化 + `appendToBuffer()` |
| `m_write_buffer_` 手动拼 | `resp.appendToBuffer(buffer)` |
| `m_file_fd_` / `m_file_address` | `resp.setFileInfo(...)` |

---

## 设计自检

- [ ] 这个类能否脱离 `http_conn` 独立编译？（不依赖 socket/epoll/线程）
- [ ] `setContentType("text/html")` 和 `addHeader("Content-Type", "text/html")` 是否等价？
- [ ] 调用方先设 body 再设 Content-Length 还是反过来？`setContentLength` 是否能自动从 body.size() 计算？
- [ ] 文件响应时，`appendToBuffer()` 只输出 header 还是连文件内容一起？为什么？
- [ ] 如果 body 是二进制数据（含 `\0`），`appendToBuffer()` 是 append string 还是 append 指定长度？

---

## 完成标准

- 新建 `http/HttpResponse.h`，可独立编译
- 写一个测试：创建 200 响应 → 设 header → 设 body → 序列化 → 检查输出格式正确（`\r\n` 都在）
- 测试 404 错误响应同样流程

# 10 · 文件上传 / 下载

## 目标

在 WebServer 上实现文件上传和下载功能。上传通过 `multipart/form-data` 接收文件存入磁盘，下载通过已有的 sendfile 零拷贝返回文件。这是服务器从"只读"升级为"读写"的关键一步。

---

## 数据流全景

```
浏览器                         服务器                             磁盘
  │                              │                                 │
  │  POST /api/upload            │                                 │
  │  Content-Type: multipart     │                                 │
  │  ┌──────────────────────┐    │                                 │
  │  │ --boundary            │    │                                 │
  │  │ Content-Disposition:  │    │  HttpContext::parse_request()   │
  │  │   form-data;          │───▶│  ├─ parse_header: Content-Type  │
  │  │   name="file";        │    │  │  = multipart/form-data       │
  │  │   filename="a.jpg"    │    │  ├─ parse_header: Content-Length│
  │  │ Content-Type: image/  │    │  ├─ parse_content: 二进制body   │
  │  │                       │    │  └─ 存入 m_body_ (string_view) │
  │  │ <字节流>               │    │                                 │
  │  │ --boundary--          │    │  Router::route() → handler      │
  │  └──────────────────────┘    │  ├─ 解析 multipart 边界         │
  │                              │  ├─ 提取文件名字段              │
  │                              │  ├─ open("root/files/<uuid>",  │
  │                              │  │        O_WRONLY|O_CREAT)     │
  │                              │  ├─ write(body)                 │
  │                              │  └─ resp.send_body(JSON)        │
  │                              │                                 │
  │  响应:                       │                                 │
  │  {"status":"ok",             │                                 │
  │   "uuid":"a1b2c3d4...",      │                                 │
  │   "size":102400}             │                                 │
  │                              │                                 │
  │  GET /files/a1b2c3d4...      │                                 │
  │──────────────────────────────▶│  Router::route() → handler      │
  │                              │  ├─ 从路径取 uuid                │
  │                              │  ├─ resp.set_file_body(path)    │
  │                              │  │   = open("root/files/<uuid>")│
  │                              │  └─ sendfile(out_fd, file_fd)   │
  │                              │                                 │
  │  ◀────────────────────────── │  sendfile 零拷贝               │
  │  <文件字节流>                 │                                 │
```

---

## multipart/form-data 解析

### 请求格式

```
POST /api/upload HTTP/1.1
Content-Type: multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW
Content-Length: 102456

------WebKitFormBoundary7MA4YWxkTrZu0gW
Content-Disposition: form-data; name="file"; filename="photo.jpg"
Content-Type: image/jpeg

<这里就是文件的二进制字节，不是 base64>
------WebKitFormBoundary7MA4YWxkTrZu0gW--
```

### 解析步骤

1. 从 `Content-Type` 头提取 `boundary=` 后面的分隔字符串
2. 在 body 中按 `--boundary` 分割各个 part
3. 对每个 part，解析 `Content-Disposition` 头取 `name` 和 `filename`
4. 空行（`\r\n\r\n`）之后到下一个 `--boundary` 之前 = 文件内容
5. 将文件内容写入磁盘

### 解析代码模板（放在 handler 内或独立的 multipart_parser.h）

```cpp
// 从 Content-Type 头提取 boundary
// "multipart/form-data; boundary=----WebKit..." → "----WebKit..."
std::string extract_boundary(std::string_view content_type) {
    auto pos = content_type.find("boundary=");
    if (pos == std::string_view::npos) return "";
    return std::string(content_type.substr(pos + 9));
}

// 解析 multipart body，提取文件信息
struct UploadedFile {
    std::string filename;
    std::string mime_type;
    const char* data;       // 指向 body 中文文件内容的起始
    size_t size;            // 文件内容的字节数
};

bool parse_multipart(std::string_view body, const std::string& boundary,
                     UploadedFile& file) {
    std::string delim = "--" + boundary;
    auto start = body.find(delim);
    if (start == std::string_view::npos) return false;

    // 跳到第一个 part
    start = body.find("\r\n", start);
    if (start == std::string_view::npos) return false;
    start += 2;

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
```

---

## UUID 生成

每个上传的文件需要一个唯一标识符。简单方案：

```cpp
#include <random>
#include <sstream>
#include <iomanip>

// 生成 32 字符的 hex UUID（类似 git commit hash）
std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(16) << dis(gen);   // 前 16 位
    oss << std::setw(16) << dis(gen);   // 后 16 位
    return oss.str();
}
```

**为什么不用标准 UUID（8-4-4-4-12 格式）？**
32 字符 hex 在 URL 中更简洁，碰撞概率 = 1/2^128，和标准 UUID 一样安全。

---

## 磁盘存储

```
root/files/          ← 新建目录
  a1b2c3d4e5f6...    ← 以 UUID 为文件名，无后缀
  1234abcd5678...
  ...
```

**为什么不保留原始文件名？**
- 文件名可能含中文 / 空格 / 特殊字符，做路径处理麻烦
- 同名文件冲突需要额外逻辑
- UUID 作为文件名天然唯一，且不暴露用户信息

原始文件名只存在数据库 / JSON 元数据中，不用于磁盘路径。

---

## API 设计

### 上传 — `POST /api/upload`

```
Content-Type: multipart/form-data

请求体: 一个 form-data 字段 "file"

成功响应 (201):
{
    "status": "ok",
    "uuid": "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6",
    "original_name": "photo.jpg",
    "mime_type": "image/jpeg",
    "size_bytes": 102400
}

失败响应 (400, 没有文件):
{
    "status": "error",
    "message": "未找到上传文件"
}

失败响应 (413, 文件过大):
{
    "status": "error",
    "message": "文件大小超过限制 (50MB)"
}
```

### 下载 — `GET /files/:uuid`

成功时直接返回文件二进制流 + 正确的 Content-Type，走 sendfile 零拷贝。

```
失败响应 (404):
{"status":"error","message":"文件不存在"}
```

---

## 后端 Handler 实现

### 上传 handler（在 webserver.h registerRoutes 中添加）

```cpp
// ---- POST /api/upload → 文件上传 ----
m_router_.addRoute(Method::POST, "/api/upload",
    [](HttpRequest& req, HttpResponse& resp) {
        // 1. 获取 body
        auto body = req.get_content();
        if (body.empty()) {
            return resp.send_body(
                "{\"status\":\"error\",\"message\":\"未找到上传文件\"}",
                0, "application/json");
        }

        // 2. 提取 boundary
        auto ct_header = req.get_headers("Content-Type");
        std::string boundary = extract_boundary(ct_header);
        if (boundary.empty()) {
            return resp.send_error(StatusCode::BAD_REQUEST);
        }

        // 3. 解析 multipart
        UploadedFile file;
        if (!parse_multipart(body, boundary, file)) {
            return resp.send_error(StatusCode::BAD_REQUEST);
        }

        // 4. 大小限制 (50MB)
        if (file.size > 50 * 1024 * 1024) {
            return resp.send_body(
                "{\"status\":\"error\",\"message\":\"文件大小超过限制 (50MB)\"}",
                "application/json");
        }

        // 5. 生成 UUID + 写磁盘
        std::string uuid = generate_uuid();
        std::string filepath = "./root/files/" + uuid;

        int fd = open(filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return resp.send_error(StatusCode::INTERNAL_ERROR);
        }
        write(fd, file.data, file.size);
        close(fd);

        // 6. 返回 JSON
        std::string json = "{"
            "\"status\":\"ok\","
            "\"uuid\":\"" + uuid + "\","
            "\"original_name\":\"" + file.filename + "\","
            "\"mime_type\":\"" + file.mime_type + "\","
            "\"size_bytes\":" + std::to_string(file.size) +
        "}";
        return resp.send_body(json.c_str(), json.size(), "application/json");
    });
```

### 下载 handler

```cpp
// ---- GET /files/:uuid → 文件下载 (sendfile) ----
m_router_.addRoute(Method::GET, "/files/:uuid",
    [](HttpRequest& req, HttpResponse& resp) {
        auto uuid = req.get_path_parameters("uuid");
        std::string filepath = "./root/files/" + uuid;

        // send_file 自动：打开文件 → 写 Content-Type + Content-Length → sendfile 发送
        // 文件不存在时自动返回 404
        return resp.send_file(filepath, "application/octet-stream");
    });
```

---

## 文件存储目录

```bash
# 创建文件存储目录
mkdir -p root/files
```

这个目录应该加入 `.gitignore`（上传的文件不需要入版本控制）。

---

## 和现有功能的复用

| 已有能力 | 复用方式 |
|---------|---------|
| `HttpContext::parse_request()` | 已解析 Content-Length + body 到 `m_body_`，POST handler 直接用 `req.get_content()` |
| `Router` 动态路由 | `POST /api/upload` 和 `GET /files/:uuid` 都走 Router |
| `HttpResponse::send_body()` | handler 返回 JSON 响应 |
| `HttpResponse::send_file()` | download handler 一行搞定文件下载（含 404 自动处理） |
| `sendfile` 零拷贝 | 下载路径自动走 SEND_FILE |
| MIME 映射 | 上传时记录文件的 MIME 类型，下载时按该类型设 Content-Type |

---

## 前端测试说明

前端测试面板中"文件上传 / 下载测试"卡片提供：

- **拖拽 + 点击上传**：支持拖拽文件到上传区或点击选择，用 `XMLHttpRequest` 发 `multipart/form-data` 到 `POST /api/upload`。显示实时进度条和响应 JSON。
- **UUID 下载**：输入文件 UUID 后 `GET /files/:uuid`，文本文件直接预览，二进制文件触发浏览器下载。
- **自动填入**：上传成功后自动将返回的 UUID 填入下载框，方便同一个文件上传后立即下载验证。

当前后端路由未实现时，上传和下载都显示蓝色提示"后端路由未实现 (404)"。后端实现后同一页面可直接用于功能验证。

---

## 自检清单

完成后逐条验证：

- [ ] `mkdir -p root/files` 目录存在
- [ ] 用 curl 上传一个文本文件：`curl -X POST http://localhost:8888/api/upload -F "file=@test.txt"`，返回 201 + UUID
- [ ] 用返回的 UUID 下载：`curl http://localhost:8888/files/<uuid>`，内容一致
- [ ] 上传一个图片文件，浏览器访问下载链接能正常显示图片
- [ ] 上传超过 50MB 的大文件，返回 413
- [ ] 下载不存在的 UUID，返回 404
- [ ] 打开 `root/index.html`，在文件上传卡片中拖拽文件上传，验证预览和进度条
- [ ] 上传成功后自动填入 UUID，点下载按钮验证文件内容一致
- [ ] 用 wrk 或其他工具并发上传 100 个文件，无崩溃
- [ ] 检查 `root/files/` 目录下有对应 UUID 文件

---

## 后续扩展方向

| 方向 | 说明 |
|------|------|
| **分片上传** | 大文件切片上传，前端用 `Blob.slice()` 分片，后端合并 |
| **断点续传** | 记录已上传分片，重传只补缺失部分 |
| **文件去重** | 上传时先 SHA256，相同哈希的文件只存一份 |
| **临时文件清理** | 定时器扫描 `root/files/`，删除超时未确认的文件 |
| **元数据持久化** | 上传信息写入 MySQL（结合 09-mysql-auth） |
| **访问控制** | 上传者 token 鉴权，只有上传者能删除自己的文件 |

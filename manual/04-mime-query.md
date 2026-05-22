# 04 · MIME 映射 + Query String 解析

## 本章分两部分：MIME 类型映射 + URL Query String 解析

这两个功能都很小（各 < 50 行），但都是 HTTP 服务器的必要组件，也能在面试中展示你对标准的熟悉程度。

---

## Part A：MIME 类型映射

### 目标

替换现在 `add_content_type()` 中硬编码的 `"text/html"`，改为按文件后缀查表。

### 你的现状

```cpp
// http_conn.h 当前
bool add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
    //                                     ^^^^^^^^^ 永远 text/html
}
```

### 设计要求

```cpp
// 功能：输入文件路径 → 输出 MIME 类型字符串
// 设计决策：函数还是类？这功能太简单，一个自由函数 + 一个 static map 就够了

const std::string& getMimeType(const std::string& path);
```

### 你需要考虑的 MIME 类型

面试时要求至少能说出以下 8 种，代码里至少覆盖 12-15 种：

| 后缀 | MIME 类型 | 常见程度 |
|------|----------|---------|
| `.html` `.htm` | `text/html` | 必选 |
| `.css` | `text/css` | 必选 |
| `.js` | `application/javascript` | 必选 |
| `.json` | `application/json` | 必选 |
| `.png` | `image/png` | 必选 |
| `.jpg` `.jpeg` | `image/jpeg` | 必选 |
| `.gif` | `image/gif` | 必选 |
| `.svg` | `image/svg+xml` | 推荐 |
| `.ico` | `image/x-icon` | 推荐 |
| `.txt` | `text/plain` | 推荐 |
| `.pdf` | `application/pdf` | 推荐 |
| `.mp4` | `video/mp4` | 可选 |
| `.woff` `.woff2` | `font/woff2` | 可选 |

### 设计自检

- [ ] 未知后缀（如 `.xyz`）返回什么？提示：`application/octet-stream`
- [ ] 文件名无后缀时返回什么？
- [ ] 后缀大小写（`Index.HTML`）能正确匹配吗？
- [ ] 后缀提取逻辑：`path.rfind('.')` 还是手动找？

### 面试追问预备

> "为什么 MIME 类型用 map 而不是 if-else 链？"
> → 查表 O(log n)，if-else O(n)，更重要的是**修改和新增不需要改代码逻辑**。

---

## Part B：Query String 解析

### 目标

解析 URL 中 `?` 后面的 `key=value&key=value` 参数。

### 你的现状

**完全缺失。** 你的 `parse_request_line` 现在的逻辑：

```cpp
std::istringstream ss(text);
ss >> method >> path >> version;  // path 是整个 "/index.html?a=1&b=2"
```

`?` 后面的部分没有被识别和解析。

### 设计要求

```cpp
// 输入：query string 原始字符串（不含 '?'） 如 "q=hello&page=1&sort=asc"
// 输出：解析并存入 HttpRequest 的 queryParameters map
// 返回：bool — 格式正确？

void HttpRequest::parseQueryString(const std::string& queryString);
```

### URL 编码 (%XX) 的处理

面试必问：`%20` 是什么？`%E4%B8%AD` 是什么？

```
空格 → %20
中文 → UTF-8 编码后每个字节 %XX
& → %26
= → %3D
% → %25
```

**你需要处理**：至少实现空格 `%20` → `' '` 的解码，其他 %XX 可选。

### 设计自检

- [ ] `key` 和 `value` 的 URL 解码（%XX → 原字符）你处理了吗？
- [ ] 空 value（`?name=`）怎么处理？
- [ ] 没有 `=` 的 key（`?debug`）怎么处理？
- [ ] 连续的 `&`（`?a=1&&b=2`）怎么处理？
- [ ] 这个函数放在 HttpRequest 里还是独立的工具函数？

---

## 完成标准

- MIME 映射：`getMimeType("index.html")` → `"text/html"`；`getMimeType("photo.png")` → `"image/png"`
- Query：`parseQueryString("q=hello%20world&page=1")` → `queryParameters_["q"] == "hello world"` && `queryParameters_["page"] == "1"`
- 后缀大小写不敏感
- 两个功能可以独立单元测试，不需要启动服务器

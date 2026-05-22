# 01 · HttpRequest 请求模型

## 目标

把 `http_conn` 中散落的请求相关字段提取为一个**纯数据类**。

---

## 你的现状

`http_conn.h` 中这些字段目前散落在类的 private 区域：

```cpp
// 现在散落在 http_conn 各处
enum METHOD { GET = 0, POST };
METHOD m_method_;
std::string m_path;
std::string m_version;
std::string m_host_;
ssize_t m_content_length_;
char m_read_buffer_[2048];  // 原始字节，不属于请求模型
// 缺失：query string、请求体 body、headers 的完整 map
```

**问题**：
1. 想查看"一个请求有哪些属性"，需要翻遍整个类定义
2. 解析逻辑和请求数据混在同一个类里，改解析可能破坏数据
3. 没法在不启动 epoll 的情况下测试请求解析

---

## 设计要求

### 这个类的职责边界

```
HttpRequest 只管：
  ✅ 存储请求数据（method, path, query, headers, body）
  ✅ 提供只读访问接口
  ✅ 支持 swap/reset（连接复用时快速清空）

HttpRequest 不管：
  ❌ 字节流解析（那是 HttpContext 的活）
  ❌ Socket I/O
  ❌ 响应生成
  ❌ epoll 操作
```

### 面试你为什么这样设计

> "我把请求模型做成纯数据类，这样解析器可以脱离网络单独单元测试——喂一坨字节流，检查解析出的 method/path/headers 对不对。如果数据模型耦合了 I/O，单元测试就写不了。"

---

## 接口定义

你来实现以下类。下面给出**接口签名**和**设计意图**，具体实现自己完成。

### 必须包含的接口

```cpp
class HttpRequest {
public:
    // ===== 方法枚举 =====
    // 思考：为什么用 enum class 而不是你现在 http_conn 里的裸 enum？
    // 提示：作用域污染、类型安全
    enum class Method { kInvalid, kGet, kPost, kHead, kPut, kDelete, kOptions };

    // ===== 构造 / 重置 =====
    HttpRequest();
    void swap(HttpRequest& other);  // 思考：为什么用 swap 而不是 = 赋值？
                                    // 提示：避免深拷贝 string/map，O(1) 交换

    // ===== Method =====
    void setMethod(Method m);
    Method method() const;

    // ===== Path =====
    // 设计决策：path() 返回的是纯路径，不含 query string
    void setPath(const std::string& path);
    const std::string& path() const;

    // ===== Query Parameters =====
    // 设计决策：query 参数在解析 path 时提取，存储为 key-value map
    // 思考：用 unordered_map 还是 map？为什么？
    void addQueryParameter(const std::string& key, const std::string& value);
    std::string getQueryParameter(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& queryParameters() const;

    // ===== HTTP 版本 =====
    void setVersion(const std::string& version);
    const std::string& version() const;

    // ===== Headers =====
    // 设计决策：用 map 还是 unordered_map？
    // 提示：HTTP header 不区分大小写——你的 key 存储策略是什么？
    //       Kama 用 std::map（自动排序，RFC 无要求但方便调试）
    void addHeader(const std::string& key, const std::string& value);
    std::string getHeader(const std::string& key) const;
    const std::map<std::string, std::string>& headers() const;

    // ===== Body (POST/PUT) =====
    void setBody(const std::string& body);
    const std::string& body() const;

    // ===== Content-Length =====
    void setContentLength(uint64_t len);
    uint64_t contentLength() const;

    // ===== 接收时间 =====
    // 设计决策：要不要存时间戳？
    // 提示：对日志和性能分析有用，但不是 HTTP 协议本身的字段
    void setReceiveTime(time_t t);
    time_t receiveTime() const;

private:
    // 你来设计内部存储字段
    // 提示：至少需要 method_, version_, path_, queryParameters_,
    //       headers_, body_, contentLength_
};
```

### 可选扩展（加分项，不改也行）

- `bool isKeepAlive() const` — 从 `Connection` header 判断，避免调用方自己解析
- `std::string toString() const` — 调试用，打印完整请求摘要

---

## 与现有代码的映射关系

你写新类时对照这个表，确保没有遗漏字段：

| 现在的 http_conn 字段/逻辑 | HttpRequest 对应接口 |
|---|---|
| `m_method_` (GET/POST) | `method_` (扩展为 7 种) |
| `m_path` (std::string) | `path_` + `queryParameters_` |
| `m_version` | `version_` |
| `m_host_` (单个) | `headers_["Host"]`（统一进 map） |
| `m_content_length_` | `contentLength_` |
| 缺失的 query string | `queryParameters_` |
| 缺失的 body | `body_` |
| `m_linger_` | 从 `headers_["Connection"]` 实时判断 |

---

## 自检清单

完成后逐条过：

- [ ] 能否在不 include 任何 socket/epoll 头文件的情况下编译此头文件？
- [ ] `swap()` 是否是 O(1)？（只交换指针，不拷贝内容）
- [ ] header key 的大小写问题怎么处理的？（存时统一小写 or 查时忽略大小写）
- [ ] `GET /path?key=value HTTP/1.1` — path() 返回 `/path`，query 参数正确解析
- [ ] 请求体为空时 `body()` 返回空串不崩溃
- [ ] 这个类只有 getter/setter，没有任何解析逻辑（解析逻辑属于 HttpContext）

---

## 完成标准

- 新建 `http/HttpRequest.h`，可以独立编译
- 不依赖任何 socket / epoll / pthread 头文件
- 写一个简单测试：创建请求对象 → set 各字段 → get 验证 → swap 换空 → 旧对象字段全部为空

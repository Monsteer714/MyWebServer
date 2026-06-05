# 09 · MySQL 用户认证（登录 / 注册）

## 目标

在 WebServer 的基础上集成 MySQL，实现最简用户注册和登录功能。这是后端三大件（HTTP + 数据库 + 缓存）的第二步，也是面试中"业务后端能力"的基本功。

---

## 数据库设计

### 建表 SQL

```sql
CREATE DATABASE IF NOT EXISTS webserver;
USE webserver;

CREATE TABLE IF NOT EXISTS users (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    username    VARCHAR(64)  NOT NULL UNIQUE,
    password    VARCHAR(128) NOT NULL,         -- SHA256 哈希结果
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

**设计决策：**

| 决策 | 选择 | 理由 |
|------|------|------|
| 主键 | 自增 INT | 最简，用户量不大时完全够用 |
| 密码存储 | SHA256(username + password) | 加盐防彩虹表，足够学习用途 |
| 密码哈希位置 | MySQL `SHA2()` 函数 | 不用引入 C++ 密码库，SQL 里一行完成 |
| 用户名 | UNIQUE + VARCHAR(64) | 防止重名，64 字符合理 |

**为什么不在 C++ 里做 SHA256？**
因为 MySQL 内置了 `SHA2()` 函数。注册时直接 `INSERT INTO users (username, password) VALUES ('alice', SHA2('alice:123456', 256))`，一行 SQL 搞定哈希 + 存储。省掉一个 OpenSSL/CommonCrypto 依赖。

---

## API 设计

### 注册 — `POST /api/register`

```
Content-Type: application/json

请求:
{
    "username": "alice",
    "password": "123456"
}

成功响应 (201):
{
    "status": "ok",
    "message": "注册成功",
    "user_id": 1
}

失败响应 (409, 用户名已存在):
{
    "status": "error",
    "message": "用户名已存在"
}

失败响应 (400, 参数不合法):
{
    "status": "error",
    "message": "用户名长度应为 3~32, 密码至少 6 位"
}
```

### 登录 — `POST /api/login`

```
Content-Type: application/json

请求:
{
    "username": "alice",
    "password": "123456"
}

成功响应 (200):
{
    "status": "ok",
    "message": "登录成功",
    "user_id": 1,
    "username": "alice"
}

失败响应 (401):
{
    "status": "error",
    "message": "用户名或密码错误"
}
```

### 查询用户 — `GET /api/user/:id`

```
成功响应 (200):
{
    "status": "ok",
    "user": {
        "id": 1,
        "username": "alice",
        "created_at": "2026-06-05 12:00:00"
    }
}
```

---

## 后端实现

### 文件结构

```
database/
  mysql_client.h   — MySQL 连接封装 (RAII)
  user_repo.h      — 用户数据访问层 (注册/登录/查询)
```

### mysql_client.h — 连接封装

```cpp
#include <mysql/mysql.h>
#include <string>
#include <stdexcept>

// 简单的 MySQL 连接 RAII 封装
class MySQLClient {
    MYSQL* conn_;
public:
    MySQLClient(const char* host, const char* user,
                const char* pass, const char* db) {
        conn_ = mysql_init(nullptr);
        if (!mysql_real_connect(conn_, host, user, pass, db, 0, nullptr, 0)) {
            throw std::runtime_error(mysql_error(conn_));
        }
    }

    ~MySQLClient() { if (conn_) mysql_close(conn_); }

    MYSQL* get() { return conn_; }
};
```

**面试追问：为什么不搞连接池？**
> "当前阶段每个请求创建连接开销可接受（MySQL 本地连接 < 1ms）。后续可以用一个简单的连接池（和线程池设计类似：预创建 N 个连接 + 互斥锁 + 条件变量），线程从池中借 / 还连接。"

### user_repo.h — 用户数据访问

```cpp
#include "mysql_client.h"
#include <string>
#include <cstring>

struct UserInfo {
    int id;
    std::string username;
    std::string created_at;
};

// 注册：插入新用户，返回 user_id。若用户名已存在返回 -1
inline int user_register(MySQLClient& db,
                          const std::string& username,
                          const std::string& password) {
    // SHA2(CONCAT(username, ':', password), 256) 作为密码哈希
    // 加 username 做盐，防彩虹表
    auto sql =
        "INSERT INTO users (username, password) "
        "VALUES ('" + username + "', SHA2(CONCAT('" + username + "',':','"
        + password + "'), 256))";
    // 注意：生产环境必须用 prepared statement 防 SQL 注入，此处为学习简化
    if (mysql_query(db.get(), sql.c_str())) {
        unsigned int err = mysql_errno(db.get());
        if (err == 1062) return -1;  // ER_DUP_ENTRY: 用户名已存在
        throw std::runtime_error(mysql_error(db.get()));
    }
    return static_cast<int>(mysql_insert_id(db.get()));
}

// 登录：验证用户名密码。成功返回 user_info，失败返回空
inline std::optional<UserInfo> user_login(MySQLClient& db,
                                           const std::string& username,
                                           const std::string& password) {
    auto sql =
        "SELECT id, username, created_at FROM users "
        "WHERE username = '" + username + "' "
        "AND password = SHA2(CONCAT('" + username + "',':','"
        + password + "'), 256)";
    if (mysql_query(db.get(), sql.c_str())) {
        throw std::runtime_error(mysql_error(db.get()));
    }

    MYSQL_RES* res = mysql_store_result(db.get());
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return std::nullopt; }

    UserInfo info;
    info.id         = std::stoi(row[0]);
    info.username   = row[1];
    info.created_at = row[2] ? row[2] : "";

    mysql_free_result(res);
    return info;
}
```

**面试追问：为什么不用 Prepared Statement？**
> "当前代码用字符串拼接 SQL 仅为学习阶段简化。生产代码必须用 `mysql_stmt_init()` + `mysql_stmt_bind_param()` 的 prepared statement 接口，防止 SQL 注入。这个改进在下一步做。"

### 集成到 WebServer

在 `webserver.h` 的 `registerRoutes()` 中添加：

```cpp
// ---- POST /api/register → 用户注册 ----
m_router_.addRoute(Method::POST, "/api/register",
    [&db](HttpRequest& req, HttpResponse& resp) {
        // 1. 解析请求 body（JSON）
        auto body = req.get_content();
        // TODO: 用简单的 JSON 解析取 username / password

        // 2. 参数校验
        if (username.size() < 3 || username.size() > 32) {
            return resp.send_error(StatusCode::BAD_REQUEST);
        }

        // 3. 调 user_repo
        int uid = user_register(db, username, password);
        if (uid == -1) {
            return resp.send_body(
                "{\"status\":\"error\",\"message\":\"用户名已存在\"}",
                "application/json");
        }

        // 4. 返回成功响应
        std::string body = "{\"status\":\"ok\",\"user_id\":" +
                           std::to_string(uid) + "}";
        return resp.send_body(body.c_str(), body.size(), "application/json");
    });
```

### CMakeLists.txt 改动

```cmake
# 添加 MySQL 库
if(APPLE)
    # macOS: brew install mysql
    set(MYSQL_HINTS /usr/local/opt/mysql /opt/homebrew/opt/mysql)
    find_library(MYSQL_LIB mysqlclient HINTS ${MYSQL_HINTS} PATH_SUFFIXES lib)
    find_path(MYSQL_INCLUDE mysql/mysql.h HINTS ${MYSQL_HINTS} PATH_SUFFIXES include/mysql)
else()
    find_library(MYSQL_LIB mysqlclient)
    find_path(MYSQL_INCLUDE mysql/mysql.h)
endif()

if(MYSQL_LIB AND MYSQL_INCLUDE)
    include_directories(${MYSQL_INCLUDE})
    target_link_libraries(server ${MYSQL_LIB})
    message(STATUS "MySQL support enabled")
else()
    message(WARNING "MySQL not found — auth routes disabled")
endif()
```

### 环境准备

```bash
# macOS: 安装并启动 MySQL 服务器
brew install mysql
brew services start mysql
brew services start mysql

# 创建数据库和表
mysql -u root <<SQL
CREATE DATABASE IF NOT EXISTS webserver;
USE webserver;
CREATE TABLE IF NOT EXISTS users (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    username    VARCHAR(64)  NOT NULL UNIQUE,
    password    VARCHAR(128) NOT NULL,
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
SQL
```

---

## 自检清单

完成后逐条验证：

- [ ] MySQL 能正常连接，`CREATE TABLE` 执行无报错
- [ ] `POST /api/register` — 注册成功返回 201 + user_id
- [ ] `POST /api/register` — 重复用户名返回 409
- [ ] `POST /api/register` — 参数不合法返回 400
- [ ] `POST /api/login` — 正确密码返回 200 + 用户信息
- [ ] `POST /api/login` — 错误密码返回 401
- [ ] `GET /api/user/:id` — 返回用户信息 JSON
- [ ] 密码在数据库中存的是 SHA256 哈希值，非明文
- [ ] 打开 `root/index.html`，在登录/注册卡片中测试完整流程
- [ ] 用 curl 做全套测试：
  ```bash
  # 注册
  curl -X POST http://localhost:8888/api/register \
       -H 'Content-Type: application/json' \
       -d '{"username":"alice","password":"123456"}'

  # 登录
  curl -X POST http://localhost:8888/api/login \
       -H 'Content-Type: application/json' \
       -d '{"username":"alice","password":"123456"}'
  ```

---

## 后续扩展方向

| 方向 | 说明 |
|------|------|
| **Prepared Statement** | 替换字符串拼接 SQL，防注入 |
| **Redis Session** | 登录后返回 token，存入 Redis。后续请求带 token 鉴权 |
| **JWT** | 无状态 token，不需要 Redis 存 session |
| **密码强度校验** | 正则检查大小写+数字+特殊字符 |
| **数据库连接池** | 预创建 N 个连接，线程池 borrow/return |
| **用户头像上传** | 结合文件托管服务，`/api/upload-avatar` |

---

## 面试追问预备

> "为什么不在 C++ 层做 SHA256，而是用 MySQL 的 SHA2()？"
>
> 两个原因。第一，学习阶段减少依赖——不需要引入 OpenSSL/CommonCrypto 做哈希。第二，SQL 里做哈希意味着：密码从客户端到 MySQL 的传输是明文的，实际生产应该用 HTTPS + bcrypt 在应用层做哈希。这里用 MySQL SHA2() 是"知道自己不知道什么"——清楚地知道这一层的局限，下一步升级时把它提到应用层。

> "SQL 注入风险你怎么考虑的？"
>
> 当前学习阶段用字符串拼接 SQL 走通流程。下一步会把所有用户输入的 SQL 查询改为 `mysql_stmt_bind_param()` 的 prepared statement，彻底杜绝注入。这个改造在代码里已经留了接口：user_repo 的函数签名不变，只改内部实现。

> "为什么用 MySQL 而不是 SQLite？"
>
> MySQL 是面试和生产中最常见的关系型数据库。练手时就接触 MySQL C API（`mysql_real_connect`、`mysql_query`、`mysql_store_result`），面试时能讲出"数据怎么从 C++ 代码流到 MySQL 引擎、再流回来"。SQLite 更适合嵌入式 / 单文件场景，但面试官更可能追问 MySQL 的索引、事务、锁机制。

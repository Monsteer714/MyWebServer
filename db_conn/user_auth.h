//
// Created by Hanhong Wong on 2026/6/5.
//

#ifndef MYWEBSERVER_USER_AUTH_H
#define MYWEBSERVER_USER_AUTH_H
#include "mysql_client.h"
#include <string>
#include <cstring>

struct UserInfo {
    int id;
    std::string username;
    std::string created_at;
};

inline int user_register(MySQLClient& db,
                          const std::string& username,
                          const std::string& password) {
    auto sql =
        "INSERT INTO users (username, password) "
        "VALUES ('" + username + "', SHA2(CONCAT('" + username + "',':','"
        + password + "'), 256))";
    if (mysql_query(db.get_conn(), sql.c_str())) {
        unsigned int err = mysql_errno(db.get_conn());
        if (err == 1062) return -1;  // ER_DUP_ENTRY: 用户名已存在
        throw std::runtime_error(mysql_error(db.get_conn()));
    }
    return static_cast<int>(mysql_insert_id(db.get_conn()));
}

inline std::optional<UserInfo> user_login(MySQLClient& db,
                                           const std::string& username,
                                           const std::string& password) {
    auto sql =
        "SELECT id, username, created_at FROM users "
        "WHERE username = '" + username + "' "
        "AND password = SHA2(CONCAT('" + username + "',':','"
        + password + "'), 256)";
    if (mysql_query(db.get_conn(), sql.c_str())) {
        throw std::runtime_error(mysql_error(db.get_conn()));
    }

    MYSQL_RES* res = mysql_store_result(db.get_conn());
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
#endif //MYWEBSERVER_USER_AUTH_H
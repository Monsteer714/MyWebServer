//
// Created by Hanhong Wong on 2026/6/5.
//

#ifndef MYWEBSERVER_MYSQL_CLIENT_H
#define MYWEBSERVER_MYSQL_CLIENT_H

#include <stdexcept>
#include <string>
#include <mysql/mysql.h>

class MySQLClient {
private:
    MYSQL* m_mysql_conn_;

public:
    MySQLClient(const char* host, const char* user, const char* password, const char* db) {
        m_mysql_conn_ = mysql_init(nullptr);
        if (!mysql_real_connect(m_mysql_conn_, host, user, password, db, 0, nullptr, 0)) {
            throw std::runtime_error(std::string("mysql_real_connect failed: ") + mysql_error(nullptr));
        }
    }

    ~MySQLClient() {
        if (m_mysql_conn_) {
            mysql_close(m_mysql_conn_);
        }
    }

    MYSQL* get_conn() {
        return m_mysql_conn_;
    }
};
#endif //MYWEBSERVER_MYSQL_CLIENT_H

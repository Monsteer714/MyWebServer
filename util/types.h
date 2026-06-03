//
// Created by Hanhong Wong on 2026/6/3.
//

#ifndef MYWEBSERVER_TYPES_H
#define MYWEBSERVER_TYPES_H


constexpr static int READ_BUFFER_SIZE = 2048;
constexpr static int WRITE_BUFFER_SIZE = 8192;  // 8KB，足够容纳路由 handler 的完整 HTTP 响应


enum class CHECK_STATE {
    CHECK_REQUEST = 0,
    CHECK_HEADER,
    CHECK_CONTENT,
};

enum class LINE_STATE {
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN,
};


enum class StatusCode {
    UNKNOWN = 0,
    SUCCESS = 1,
    OK = 200,
    BAD_REQUEST = 400,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500,
    BAD_GATEWAY = 502,
};

enum class SEND_STATE {
    SEND_HEAD = 0,
    SEND_FILE,
};

enum class Method {
    GET = 0,
    POST = 1,
};
#endif //MYWEBSERVER_TYPES_H


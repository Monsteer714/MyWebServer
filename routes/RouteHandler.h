//
// Created by Hanhong Wong on 2026/6/6.
//
// RouteHandler — 面向对象路由处理器抽象基类
// 每个 URL 路由对应一个继承自 RouteHandler 的具体类，实现 handle() 方法
//

#ifndef MYWEBSERVER_ROUTEHANDLER_H
#define MYWEBSERVER_ROUTEHANDLER_H

#include "../http_conn/HttpRequest.h"
#include "../http_conn/HttpResponse.h"
#include "../util/types.h"

class RouteHandler {
public:
    RouteHandler() = default;
    virtual ~RouteHandler() = default;

    // 处理 HTTP 请求，将响应写入 resp
    // 返回 true  = 响应已构建完毕
    // 返回 false = 未处理（退回文件服务）或内部错误
    virtual bool handle(HttpRequest& req, HttpResponse& resp) = 0;
};

#endif //MYWEBSERVER_ROUTEHANDLER_H

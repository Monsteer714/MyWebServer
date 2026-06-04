//
// Created by Hanhong Wong on 2026/6/4.
//

#ifndef MYWEBSERVER_MIDDLEWARE_H
#define MYWEBSERVER_MIDDLEWARE_H
#include "../http_conn/HttpRequest.h"
#include "../http_conn/HttpResponse.h"

class MiddleWare {
private:
public:
    virtual ~MiddleWare();
    virtual bool before(HttpRequest& req) = 0;
    virtual bool after(HttpResponse& resp) = 0;
};

#endif //MYWEBSERVER_MIDDLEWARE_H
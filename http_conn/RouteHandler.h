//
// Created by Hanhong Wong on 2026/6/6.
//

#ifndef MYWEBSERVER_ROUTEHANDLER_H
#define MYWEBSERVER_ROUTEHANDLER_H
#include <string>

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "../util/types.h"

class RouteHandler {
private:
public:
    RouteHandler();
    virtual ~RouteHandler();
    virtual bool handle(HttpRequest& req, HttpResponse& resp) = 0;
};
#endif //MYWEBSERVER_ROUTEHANDLER_H
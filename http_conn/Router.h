//
// Created by Hanhong Wong on 2026/5/26.
//

#ifndef MYWEBSERVER_ROUTER_H
#define MYWEBSERVER_ROUTER_H
#include <functional>

#include "HttpRequest.h"
#include "HttpResponse.h"


class Router {
public:
    using HandlerCallback = std::function<bool(HttpRequest&, HttpResponse&)>;

    void addRoute(Method method, const std::string& pattern, HandlerCallback handler);

    bool route(const HttpRequest& req, HttpResponse& resp);

    size_t get_route_count() const;
private:
    std::unordered_map<std::string, HandlerCallback> static_routers;

    struct DynamicRoute {
        Method method;
        std::vector<std::string> tokens;
        HandlerCallback handler;
    };

    std::vector<DynamicRoute> dynamic_routes;
};

#endif //MYWEBSERVER_ROUTER_H

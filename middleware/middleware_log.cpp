//
// Created by Hanhong Wong on 2026/6/4.
//

#include "middleware.h"
#include "../log/async_log.h"

class LogMiddleWare : public MiddleWare {
private:
public:
    LogMiddleWare();
    ~LogMiddleWare();

    bool before(HttpRequest& req) override {

    }

    bool after(HttpResponse& resp) override {

    }
};

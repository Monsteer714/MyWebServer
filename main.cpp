#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <csignal>
#include "webserver.h"
#include "config.h"

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    Config config;
    config.parse_args(argc, argv);

    WebServer server;
    server.init(config.TRIG_MODE, config.ACTOR_MODEL);

    server.setTrigMode();

    server.createThreadPool();

    server.createLog();

    server.start();

    server.loop();
    return 0;
}

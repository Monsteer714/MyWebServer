#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <csignal>
#include "webserver.h"

int main() {
    signal(SIGPIPE, SIG_IGN);
    WebServer server;
    server.start();
    server.loop();
    return 0;
}

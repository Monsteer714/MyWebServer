#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include "webserver.h"

int main() {
    WebServer server;
    server.start();
    server.loop();
    return 0;
}

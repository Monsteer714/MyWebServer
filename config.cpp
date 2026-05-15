//
// Created by Hanhong Wong on 2026/5/12.
//
#include "config.h"

#include <cstdlib>
#include <unistd.h>

Config::Config() {
    Config::PORT = 8888;

    Config::LOG_WRITE = 0;

    Config::TRIG_MODE = 0;

    Config::ACTOR_MODEL = 0;

    Config::LOG_CLOSE = 0;
}

void Config::parse_args(int argc, char* argv[]) {
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            Config::PORT = atoi(optarg);
            break;
        }
        case 'l':
        {
            Config::LOG_WRITE = std::atoi(optarg);
            break;
        }
        case 'm':
        {
            Config::TRIG_MODE = atoi(optarg);
            break;
        }
        case 'a':
        {
            Config::ACTOR_MODEL = atoi(optarg);
            break;
        }
        case 'c':
            Config::LOG_CLOSE = atoi(optarg);
            break;
        default:
            break;
        }
    }
}

//
// Created by Hanhong Wong on 2026/5/12.
//
#include "config.h"

#include <cstdlib>
#include <unistd.h>

Config::Config() {
    //设置监听端口
    Config::PORT = 8888;

    //日志模型，0: 阻塞队列，1:双缓冲；
    Config::LOG_MODEL = 0;

    //组合触发方式，0:服务端LT，客户端LT；1:服务端ET，客户端LT；2:服务端LT，客户端ET；3:服务端ET，客户端ET；
    Config::TRIG_MODE = 0;

    //并发模型，0：Reactor，1:Proactor;
    Config::ACTOR_MODEL = 0;

    //日志开关，0:开启；1:关闭；
    Config::LOG_CLOSE = 0;

    //定时器模式，0:升序链表；1:时间轮；2:时间堆；3：共享红黑树；
    Config::TIMER_MODEL = 0;
}

void Config::parse_args(int argc, char* argv[]) {
    int opt;
    const char* str = "p:l:o:s:t:c:a:t:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
        case 'p': {
            Config::PORT = atoi(optarg);
            break;
        }
        case 'l': {
            Config::LOG_MODEL = atoi(optarg);
            break;
        }
        case 'a': {
            Config::ACTOR_MODEL = atoi(optarg);
            break;
        }
        case 'c':
            Config::LOG_CLOSE = atoi(optarg);
            break;
        case 't':
            Config::TIMER_MODEL = atoi(optarg);
            break;
        default:
            break;
        }
    }
}

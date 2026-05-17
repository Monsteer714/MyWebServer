//
// Created by Hanhong Wong on 2026/5/12.
//

#ifndef MYWEBSERVER_CONFIG_H
#define MYWEBSERVER_CONFIG_H

class Config {
private:
public:
    Config();
    ~Config() {}

    void parse_args(int argc, char *argv[]);

    int PORT;

    int LOG_WRITE;

    int LOG_CLOSE;

    int TRIG_MODE;

    int ACTOR_MODEL;

    int TIMER_MODEL;
};
#endif //MYWEBSERVER_CONFIG_H

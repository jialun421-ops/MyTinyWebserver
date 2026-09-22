#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "http_conn.h"
#include "threadpool.h"
#include "timer.h"
#include "log.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, int thread_num, int close_log, int trig_mode);
    void thread_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    // 所有静态资源——供定时器回调访问
    static int m_epollfd;
    static int m_listenfd;
    static int m_pipefd[2];
    static http_conn *users;
    static client_data *users_timer;
    static sort_timer_lst m_timer_lst;
    static int m_close_log;

private:
    int m_port;
    int m_thread_num;
    int m_trig_mode;

    // epoll 事件数组
    epoll_event events[MAX_EVENT_NUMBER];

    // 线程池
    threadpool<http_conn> *m_pool;

    // 定时器
    struct sockaddr_in m_address;

    int m_listen_trig_mode;
    int m_conn_trig_mode;
};

#endif
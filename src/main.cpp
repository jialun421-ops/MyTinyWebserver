#include "webserver.h"

int main(int argc, char *argv[]) {
    int port = 9006;        // 默认端口 
    int thread_num = 8;     // 默认线程数
    int close_log = 0;      // 0 = 同步日志，1 = 异步日志
    int trig_mode = 3;      // 3 = listenET + connET // 0 = listenLT + connLT, 1 = listenLT + connET, 2 = listenET + connLT

    // 也支持命令行：./server [port] [thread_num] [close_log] [trig_mode]
    if (argc >= 2) port = atoi(argv[1]);    // 端口号
    if (argc >= 3) thread_num = atoi(argv[2]);
    if (argc >= 4) close_log = atoi(argv[3]);
    if (argc >= 5) trig_mode = atoi(argv[4]);

    WebServer server;       // 创建服务器对象
    server.init(port, thread_num, close_log, trig_mode); // 初始化服务器参数
    server.log_write();     // 初始化日志
    server.thread_pool();   // 初始化线程池
    server.trig_mode();     // 初始化触发模式
    server.eventListen();   // 初始化事件监听
    server.eventLoop();     // 运行事件循环

    return 0;
}
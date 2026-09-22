#ifndef LOG_H
#define LOG_H

#include <cstdio>
#include <iostream>
#include <cstring>
#include <cstdarg>
#include <pthread.h>
#include <ctime>
#include <sys/time.h>
#include <sys/stat.h>
#include "block_queue.h"

class Log {
public:
    // 单例：C++11 起局部静态变量初始化是线程安全的
    static Log *get_instance() { // 获取日志实例
        static Log instance;
        return &instance;
    }

    // 异步写线程入口
    static void *flush_log_thread(void *args) {
        Log::get_instance()->async_write_log();
        return NULL;
    }

    // 初始化：参数含义见函数体注释
    bool init(const char *file_name, int close_log,
              int log_buf_size = 8192, int split_lines = 5000000,
              int max_queue_size = 0);

    void write_log(int level, const char *format, ...); // 写日志：level 日志级别，format 格式化字符串
    void flush(); // 刷新缓冲区，强制写入文件
    int get_close_log() const { return m_close_log; } // 获取日志关闭标志

private:
    Log();
    virtual ~Log();

    void *async_write_log(); // 异步写日志线程主函数

private:
    char m_dir_name[128];      // 日志目录
    char m_log_name[128];      // 日志文件名（不含日期和后缀）
    int m_split_lines;         // 每个文件最大行数
    int m_log_buf_size;        // 缓冲区大小
    long long m_count;         // 当前行数
    int m_today;               // 今天的日期（年月日）
    FILE *m_fp;                // 文件指针
    char *m_buf;               // 临时缓冲区
    block_queue<char *> *m_log_queue;  // 异步队列
    bool m_is_async;           // 是否异步
    locker m_mutex;            // 保护文件操作
    int m_close_log;           // 是否关闭日志
};

// 日志级别
#define LOG_DEBUG(format, ...) \
    if (0 == Log::get_instance()->get_close_log()) { \
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
        Log::get_instance()->flush(); \
    }
#define LOG_INFO(format, ...) \
    if (0 == Log::get_instance()->get_close_log()) { \
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
        Log::get_instance()->flush(); \
    }
#define LOG_WARN(format, ...) \
    if (0 == Log::get_instance()->get_close_log()) { \
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
        Log::get_instance()->flush(); \
    }
#define LOG_ERROR(format, ...) \
    if (0 == Log::get_instance()->get_close_log()) { \
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
        Log::get_instance()->flush(); \
    }

#endif
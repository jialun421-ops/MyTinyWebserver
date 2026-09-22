#include "log.h"
#include <unistd.h>

Log::Log() {
    m_count = 0;
    m_is_async = false;
    m_fp = NULL;
    m_buf = NULL;
    m_log_queue = NULL;
    m_close_log = 0;
}

Log::~Log() {
    if (m_fp) {
        fclose(m_fp);
    }
    if (m_buf) {
        delete[] m_buf;
        m_buf = NULL;
    }
    if (m_log_queue) {
        delete m_log_queue;
        m_log_queue = NULL;
    }
}

bool Log::init(const char *file_name, int close_log,
               int log_buf_size, int split_lines, int max_queue_size) {
    m_close_log = close_log;

    // 如果 max_queue_size > 0，说明要用异步模式
    if (max_queue_size >= 1) {
        m_is_async = true;
        m_log_queue = new block_queue<char *>(max_queue_size);
        pthread_t tid;
        // 创建后台写线程
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    // 计算今天日期
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 从 file_name 中提取路径和文件名
    const char *p = strrchr(file_name, '/');
    char log_full_name[512] = {0};

    if (p == NULL) {
        // 没有路径，直接用文件名
        snprintf(log_full_name, 511, "%d_%02d_%02d_%s",
                 my_tm.tm_year + 1900, my_tm.tm_mon + 1,
                 my_tm.tm_mday, file_name);
    } else {
        // 有路径，分离目录和文件名
        strcpy(m_log_name, p + 1);
        strncpy(m_dir_name, file_name, p - file_name + 1);
        m_dir_name[p - file_name + 1] = '\0';
        snprintf(log_full_name, 511, "%s%d_%02d_%02d_%s",
                 m_dir_name, my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday, m_log_name);
    }

    m_today = my_tm.tm_mday;
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL) {
        return false;
    }
    return true;
}

// 写日志：level 日志级别，format 格式化字符串
void Log::write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch (level) {
    case 0: strcpy(s, "[debug]:"); break;
    case 1: strcpy(s, "[info]:");  break;
    case 2: strcpy(s, "[warn]:");  break;
    case 3: strcpy(s, "[error]:"); break;
    default: strcpy(s, "[info]:"); break;
    }

    m_mutex.lock();
    m_count++;

    // 跨天 或 超过最大行数，需要新建文件
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[512] = {0};
        fflush(m_fp);
        fclose(m_fp);

        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_",
                 my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        // 根据是否跨天决定新文件名
        if (m_today != my_tm.tm_mday) {
            snprintf(new_log, 511, "%s%s%s",
                     m_dir_name, tail, m_log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        } else { // 超过最大行数，按序号生成新文件名
            snprintf(new_log, 511, "%s%s%s.%lld",
                     m_dir_name, tail, m_log_name,
                     m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    // 生成一行日志的字符串
    char log_str[1024] = {0};
    int n = snprintf(log_str, 512, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec,
                     (long)now.tv_usec, s);

    m_mutex.lock();
    if (m_is_async) {
        // 异步模式：把生成的字符串 push 到队列
        va_list valst2;
        va_start(valst2, format);
        int m = vsnprintf(log_str + n, 1023 - n, format, valst2);
        va_end(valst2);
        log_str[n + m] = '\n';
        log_str[n + m + 1] = '\0';

        char *buf = new char[strlen(log_str) + 1];
        strcpy(buf, log_str);
        while (!m_log_queue->push(buf)) {
            // 队列满，等一会再试
            usleep(1000);
        }
    } else {
        // 同步模式：直接写文件
        int m = vsnprintf(log_str + n, 1023 - n, format, valst);
        log_str[n + m] = '\n';
        log_str[n + m + 1] = '\0';
        fputs(log_str, m_fp);
        fputs(log_str, stdout);   // 同时打印到终端
    }
    m_mutex.unlock();
    va_end(valst);
}

// 刷新缓冲区，强制写入文件
void Log::flush() {
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}

// 异步模式：后台线程从队列取字符串并写文件
void *Log::async_write_log() {
    char *log_str = NULL;
    // 从队列中取出日志字符串并写入文件
    while (m_log_queue->pop(log_str)) {
        m_mutex.lock();
        fputs(log_str, m_fp);
        m_mutex.unlock();
        delete[] log_str;
        log_str = NULL;
    }
    return NULL;
}
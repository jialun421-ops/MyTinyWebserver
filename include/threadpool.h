#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>     // 使用链表作为任务队列
#include <cstdio>   // 使用 printf 输出日志
#include <exception>
#include <pthread.h>
#include "locker.h"

// 线程池类，T 为任务类型
template <typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T *request);          // 向请求队列中添加任务

private:
    static void *worker(void *arg);   // 静态函数，作为 pthread_create 的入口
    void run();                       // 线程实际执行的循环

private:
    int m_thread_number;              // 线程数量
    int m_max_requests;               // 队列最大长度
    pthread_t *m_threads;             // 线程数组
    std::list<T *> m_workqueue;       // 任务队列
    locker m_queuelocker;             // 保护队列的互斥锁
    sem m_queuestat;                  // 队列状态信号量，表示有多少任务
    bool m_stop;                      // 是否结束线程
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : 
    m_thread_number(thread_number),
    m_max_requests(max_requests),
    m_threads(NULL),
    m_stop(false) {

    // 检查参数合法性
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }

    // 创建线程数组
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    // 创建 thread_number 个线程，并设为分离态（detach）
    for (int i = 0; i < thread_number; ++i) {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) { // 创建线程失败
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) { // 设置线程为分离态失败
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构函数，释放线程数组
template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true; // 通知所有线程结束
}

// 向请求队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request) {
    m_queuelocker.lock();                       // 保护队列的互斥锁
    if (m_workqueue.size() > (size_t)m_max_requests) {  // 队列满了，拒绝请求
        m_queuelocker.unlock();                 // 释放互斥锁
        return false;
    }
    m_workqueue.push_back(request);             // 添加任务到队列
    m_queuelocker.unlock();                     // 释放互斥锁
    m_queuestat.post();                         // 通知工作线程：有任务了
    return true;
}

// 线程池工作线程的入口函数
template <typename T>
void *threadpool<T>::worker(void *arg) {        // arg 是线程池对象的指针
    threadpool *pool = (threadpool *)arg;       // 将 void* 转为 threadpool*，以便调用成员函数
    pool->run();                                // 调用线程池的 run() 方法，执行任务循环
    return pool;
}

// 线程池工作线程的循环函数
template <typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();          // 等待新任务
        m_queuelocker.lock();        // 保护队列的互斥锁
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();   // 获取队列头部的任务
        m_workqueue.pop_front();            // 移除队列头部的任务
        m_queuelocker.unlock();

        if (!request) {                     // 如果任务为空，继续等待下一个任务
            continue;
        }
        request->process();                 // 执行任务
    }
}

#endif
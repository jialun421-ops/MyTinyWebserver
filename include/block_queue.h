#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <cstdlib>
#include <pthread.h>
#include <sys/time.h>
#include "locker.h"

template <typename T>
class block_queue {
public:
    block_queue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }
        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~block_queue() {
        m_mutex.lock();
        if (m_array != NULL) {
            delete[] m_array;
            m_array = NULL;
        }
        m_mutex.unlock();
    }
    // 队列清空
    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    // 判断队列是否满
    bool full() {
        m_mutex.lock();
        bool ret = (m_size >= m_max_size);
        m_mutex.unlock();
        return ret;
    }
    // 判断队列是否为空
    bool empty() {
        m_mutex.lock();
        bool ret = (m_size == 0);
        m_mutex.unlock();
        return ret;
    }
    // 获取队首元素
    bool front(T &value) {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    // 获取队尾元素
    bool back(T &value) {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
    // 获取当前队列元素个数
    int size() {
        m_mutex.lock();
        int ret = m_size;
        m_mutex.unlock();
        return ret;
    }
    // 获取队列的最大容量
    int max_size() {
        m_mutex.lock();
        int ret = m_max_size;
        m_mutex.unlock();
        return ret;
    }

    // 生产者：往队列里放
    bool push(const T &item) {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            // 队列满，通知消费者来取，自己先返回失败
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    // 消费者：从队列里取（阻塞）
    bool pop(T &item) {
        m_mutex.lock();
        while (m_size <= 0) {
            // 队列为空，等待生产者通知
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    // 带超时的 pop
    bool pop(T &item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000000;
            if (!m_cond.timedwait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }
        if (m_size <= 0) {
            m_mutex.unlock();
            return false;
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;
    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif
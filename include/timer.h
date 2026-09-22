#ifndef TIMER_H
#define TIMER_H

#include <unistd.h>
#include <netinet/in.h>
#include <ctime>
#include <cstring>
#include <cstdlib>

class util_timer;

// 客户端数据结构：每个连接对应一份
struct client_data {
    sockaddr_in address;        // 客户端地址
    int sockfd;                 // socket 文件描述符
    util_timer *timer;          // 指向对应的定时器
};

// 定时器节点
class util_timer {
public:
    // util_timer() : prev(NULL), next(NULL), user_data(NULL), cb_func(NULL) {}
    util_timer() : expire(0), cb_func(NULL), user_data(NULL), prev(NULL), next(NULL) {}

public:
    time_t expire;                       // 绝对过期时间
    void (*cb_func)(client_data *);      // 过期回调
    client_data *user_data;              // 指向客户端数据
    util_timer *prev;
    util_timer *next;
};

// 升序定时器链表
class sort_timer_lst {
public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

// ---------- 实现 ----------
// 析构函数：删除所有定时器
inline sort_timer_lst::~sort_timer_lst() {
    util_timer *tmp = head;
    while (tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}
// 添加定时器：升序插入
inline void sort_timer_lst::add_timer(util_timer *timer) {
    if (!timer) return;
    if (!head) {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}
// 调整定时器：向后移动
inline void sort_timer_lst::adjust_timer(util_timer *timer) { 
    if (!timer) return;
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire)) {
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    } else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}
// 删除定时器
inline void sort_timer_lst::del_timer(util_timer *timer) {
    if (!timer) return;
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = tail = NULL;
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
// 定时器处理函数：触发回调，删除过期定时器
inline void sort_timer_lst::tick() {
    if (!head) return;
    time_t cur = time(NULL);
    util_timer *tmp = head;
    while (tmp) {
        if (cur < tmp->expire) break;
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if (head) head->prev = NULL;
        delete tmp;
        tmp = head;
    }
}
//  添加定时器：内部函数，升序插入
inline void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) {
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            return;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    prev->next = timer;
    timer->prev = prev;
    timer->next = NULL;
    tail = timer;
}

#endif
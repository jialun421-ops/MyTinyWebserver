#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include "locker.h"

// 共享变量
int shared_value = 0;
locker mtx;          // 互斥锁
sem sem_full(0);     // 信号量，初始值0
sem sem_empty(1);    // 信号量，初始值1
cond cv;             // 条件变量
bool ready = false;

// 生产者：对 shared_value 加1
void *producer(void *arg) {
    for (int i = 0; i < 5; ++i) {
        mtx.lock();
        shared_value++;
        std::cout << "[producer] shared_value = " << shared_value << std::endl;
        ready = true;
        cv.signal();    // 唤醒消费者
        mtx.unlock();
        sem_full.post();   // V操作
        sleep(10);           // 模拟生产者的工作
    }
    return NULL;
}

// 消费者：等待信号后读取
void *consumer(void *arg) {
    for (int i = 0; i < 5; ++i) {
        sem_full.wait();          // P操作，等待生产者
        mtx.lock();
        while (!ready) {
            cv.wait(mtx.get());
        }
        std::cout << "[consumer] got shared_value = " << shared_value << std::endl;
        ready = false;
        mtx.unlock();
    }
    return NULL;
}

int main() {
    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, producer, NULL);
    pthread_create(&tid2, NULL, consumer, NULL);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    std::cout << "final shared_value = " << shared_value << std::endl;
    return 0;
}
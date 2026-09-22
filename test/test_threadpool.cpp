#include <iostream>
#include <vector>
#include <unistd.h>
#include "threadpool.h"

// 定义一个任务类，必须有 process() 方法
class Task {
public:
    Task(int id) : m_id(id) {}
    void process() {                   // 任务处理函数
        std::cout << "Task " << m_id
                  << " processing by thread " << pthread_self()
                  << std::endl;
        sleep(1);
        std::cout << "Task " << m_id << " done" << std::endl;
    }
private:
    int m_id;
};

int main() {
    // 创建 4 个线程，队列最大 100
    threadpool<Task> pool(4, 100);

    // 用 vector 保存任务对象，避免内存泄漏
    std::vector<Task> tasks;
    tasks.reserve(10);
    for (int i = 1; i <= 10; ++i) {
        tasks.emplace_back(i);           //向vector中添加任务对象
    }

    // 把任务丢进队列
    for (auto &t : tasks) {
        pool.append(&t);                // pool是线程池对象，append()方法将任务添加到线程池的任务队列中
    }

    // 等所有任务执行完
    sleep(5);
    std::cout << "All tasks submitted, main exit." << std::endl;
    return 0;
}
#include "block_queue.h"

// 编译验证用，不运行
int main() {
    block_queue<int> q(10);
    q.push(1);
    q.push(2);

    int v;
    q.pop(v);
    q.pop(v, 100);

    return 0;
}
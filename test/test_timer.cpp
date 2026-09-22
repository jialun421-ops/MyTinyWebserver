#include "timer.h"

// 空测试：仅用于编译验证，不需要运行
int main() {
    sort_timer_lst lst;

    // 造几个定时器，不做实际效果
    util_timer t1, t2, t3;
    t1.expire = time(NULL) + 10;
    t2.expire = time(NULL) + 5;
    t3.expire = time(NULL) + 20;
    lst.add_timer(&t1);
    lst.add_timer(&t2);
    lst.add_timer(&t3);
    lst.del_timer(&t2);

    return 0;
}
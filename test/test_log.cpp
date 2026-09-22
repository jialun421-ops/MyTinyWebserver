#include "log.h"
#include <unistd.h>

// 编译验证用，不运行
int main() {
    // 同步模式
    Log::get_instance()->init("./log/ServerLog", 0, 8192, 5000000, 0);
    LOG_INFO("sync mode: hello %d", 1);
    LOG_ERROR("sync mode: error %s", "test");

    // 异步模式（要新建一个 Log 吗？其实单例不能重 init，这里只为编译）
    return 0;
}
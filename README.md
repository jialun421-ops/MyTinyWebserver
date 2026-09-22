# TinyWebServer

一个基于 **epoll + Reactor 模式** 的轻量级 Linux C++ Web 服务器，支持静态文件服务，采用线程池处理并发请求，内置定时器管理非活跃连接，并实现了同步/异步双模式日志系统。

> 本项目为个人学习项目，从零手写核心模块，用于理解 Linux 网络编程与高并发服务器设计。

## 目录

- [功能特性](#功能特性)
- [技术栈](#技术栈)
- [系统架构](#系统架构)
- [模块说明](#模块说明)
- [项目结构](#项目结构)
- [编译运行](#编译运行)
- [压测数据](#压测数据)
- [开发计划](#开发计划)

## 功能特性

- **Reactor 事件驱动模型**：主线程负责监听与事件分发，工作线程负责请求处理
- **epoll I/O 多路复用**：支持 LT / ET 两种触发模式，配合 `EPOLLONESHOT` 保证连接同一时刻只被一个线程处理
- **线程池**：基于互斥锁 + 信号量实现的任务队列，避免频繁创建销毁线程
- **HTTP 请求解析**：主从状态机解析请求行、请求头、请求体，支持 GET / POST
- **静态文件服务**：基于 `mmap` + `writev` 实现零拷贝响应
- **定时器**：升序双向链表管理连接超时，自动关闭非活跃连接，防止连接泄漏
- **日志系统**：单例模式，支持同步 / 异步双模式，按日期和行数自动切分日志文件
- **RAII 封装**：互斥锁、条件变量、信号量统一封装，避免手动释放导致的死锁与资源泄漏

## 技术栈

| 类别 | 内容 |
|---|---|
| 语言 | C++11 |
| 操作系统 | Ubuntu 22.04 / Linux |
| 网络 | epoll、TCP、非阻塞 socket |
| 并发 | pthread、线程池、互斥锁、条件变量、信号量 |
| 构建 | CMake 3.10+ |
| 调试 | GDB、curl、ab / webbench |

## 系统架构

```
                  ┌──────────────────────┐
                  │      Main Thread     │
                  │  epoll_wait + accept │
                  └──────────┬───────────┘
                             │
                ┌────────────┴────────────┐
                │                         │
        EPOLLIN │                         │ EPOLLOUT
                ▼                         ▼
     ┌──────────────────┐       ┌──────────────────┐
     │   Task Queue     │       │  http_conn::write│
     │ (block_queue)    │       └──────────────────┘
     └────────┬─────────┘
              │
     ┌────────┴─────────┐
     │   Thread Pool    │
     │  (worker × N)    │
     └────────┬─────────┘
              │
              ▼
     ┌──────────────────┐
     │ http_conn::process│
     │  read → parse    │
     │  → write response│
     └──────────────────┘
```

**请求处理流程：**

1. 主线程 `epoll_wait` 监听到新连接，`accept` 后注册到 epoll
2. 连接可读时，主线程将 `http_conn` 对象加入线程池任务队列
3. 工作线程从队列取出连接，调用 `process()`：读数据 → 解析请求 → 生成响应
4. 工作线程将 fd 重新注册为 `EPOLLOUT`
5. 主线程监听到可写事件，调用 `write()` 发送响应
6. 每次读写后重置定时器；超时连接由定时器回调关闭

## 模块说明

### 1. 线程同步（`include/locker.h`）

RAII 风格封装 `pthread_mutex_t`、`pthread_cond_t`、`sem_t`，构造时初始化，析构时销毁，异常安全。

### 2. 线程池（`include/threadpool.h`）

- 预创建 N 个 worker 线程，全部 `detach`
- 任务队列用 `std::list` + 互斥锁保护
- 用信号量通知「有新任务」，避免忙等
- `worker` 为静态函数，通过 `void*` 传入 `this` 指针

### 3. HTTP 连接处理（`include/http_conn.h` / `src/http_conn.cpp`）

- **主从状态机**：从状态机 `parse_line()` 逐字符解析出完整行；主状态机根据当前状态决定调用 `parse_request_line` / `parse_headers` / `parse_content`
- **缓冲区设计**：读缓冲区 `m_read_buf` 用于接收请求，写缓冲区 `m_write_buf` 用于构造响应头
- **零拷贝发送**：静态文件通过 `mmap` 映射到内存，配合 `iovec` + `writev` 一次性发送响应头与文件内容
- **部分写处理**：非阻塞 socket 下 `writev` 可能写不全，需要调整 `iovec` 并等待下次 `EPOLLOUT`

### 4. 定时器（`include/timer.h`）

- 升序双向链表，按过期时间排序
- 每次读写活动后 `adjust_timer` 延后过期时间
- `tick()` 从头遍历，过期的执行回调并删除，未过期直接 break
- 回调 `cb_func` 关闭对应 socket 并从 epoll 移除

### 5. 日志系统（`include/log.h` / `src/log.cpp`）

- 单例模式，局部静态变量实现（C++11 起线程安全）
- 支持同步 / 异步两种模式：异步模式基于 `block_queue`，业务线程只 push，日志线程负责写文件
- 按日期命名日志文件，按行数自动切分
- 提供 `LOG_DEBUG` / `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` 四个宏

### 6. 服务器主逻辑（`include/webserver.h` / `src/webserver.cpp`）

- `eventListen()`：创建监听 socket，初始化 epoll
- `eventLoop()`：主事件循环，处理新连接、读事件、写事件、异常事件
- `dealwithread()` / `dealwithwrite()`：读/写事件分发
- 定时器与连接生命周期绑定

## 项目结构

```
TinyWebServer/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── include/
│   ├── locker.h           # 锁 / 条件变量 / 信号量封装
│   ├── threadpool.h       # 线程池
│   ├── http_conn.h        # HTTP 连接处理
│   ├── timer.h            # 定时器
│   ├── log.h              # 日志系统
│   ├── block_queue.h      # 阻塞队列（异步日志用）
│   └── webserver.h        # 服务器主类
├── src/
│   ├── http_conn.cpp
│   ├── log.cpp
│   ├── webserver.cpp
│   └── main.cpp
├── test/                  # 各模块单元测试
│   ├── test_locker.cpp
│   ├── test_threadpool.cpp
│   ├── test_http.cpp
│   ├── test_timer.cpp
│   ├── test_block_queue.cpp
│   └── test_log.cpp
├── root/
│   └── index.html         # 静态资源根目录
└── log/                   # 日志输出目录（运行时生成）
```

## 编译运行

### 环境要求

```bash
# Ubuntu 22.04
sudo apt update
sudo apt install build-essential cmake -y
```

### 编译

```bash
git clone git@github.com:你的用户名/TinyWebServer.git
cd TinyWebServer
mkdir -p build && cd build
cmake ..
make server
```

### 运行

```bash
# 回到项目根目录运行（do_request 使用相对路径 ./root）
cd ..
./build/server
```

默认监听 `9006` 端口，线程数 `8`，ET 模式。

### 参数（可选）

```bash
./build/server [port] [thread_num] [close_log] [trig_mode]
```

| 参数 | 默认值 | 说明 |
|---|---|---|
| port | 9006 | 监听端口 |
| thread_num | 8 | 线程池大小 |
| close_log | 0 | 0 开启日志，1 关闭 |
| trig_mode | 3 | 0=LT+LT，1=LT+ET，2=ET+LT，3=ET+ET |

### 验证

```bash
curl -v http://127.0.0.1:9006/
```

浏览器访问 `http://虚拟机IP:9006/`。

## 压测数据

**环境**：VMware 虚拟机 + Ubuntu 22.04，单核 / 2GB 内存

**工具**：`ab`（Apache Bench）

```bash
ab -n 10000 -c 100 http://127.0.0.1:9006/
```

| 并发数 | 总请求 | QPS | 平均延迟 | 失败请求 |
|---|---|---|---|---|
| 10 | 10000 | 待填 | 待填 | 0 |
| 100 | 10000 | 待填 | 待填 | 0 |

> 压测数据在虚拟机环境下，受 CPU 与虚拟化层影响，仅供参考。

## 开发计划

- [x] 线程池
- [x] HTTP 请求解析与响应
- [x] 定时器
- [x] 同步 / 异步日志
- [x] epoll Reactor 主循环
- [x] 静态文件服务
- [ ] MySQL 连接池
- [ ] POST 登录 / 注册
- [ ] 压力测试脚本
- [ ] 单元测试框架（Google Test）

## 参考

- 《Linux 高性能服务器编程》—— 游双
- 《UNIX 网络编程》—— W. Richard Stevens
- [qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer)（学习参考）

## License

MIT
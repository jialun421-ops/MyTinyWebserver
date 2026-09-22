#include "webserver.h"

int WebServer::m_epollfd = -1;
int WebServer::m_listenfd = -1;
int WebServer::m_pipefd[2] = {0};
http_conn *WebServer::users = NULL;
client_data *WebServer::users_timer = NULL;
sort_timer_lst WebServer::m_timer_lst;
int WebServer::m_close_log = 0;

WebServer::WebServer() {
    users = new http_conn[MAX_FD];
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

// 初始化服务器参数
void WebServer::init(int port, int thread_num, int close_log, int trig_mode) {
    m_port = port;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_trig_mode = trig_mode;

    if (trig_mode == 0) {
        m_listen_trig_mode = 0;  // LT
        m_conn_trig_mode = 0;    // LT
    } else if (trig_mode == 1) {
        m_listen_trig_mode = 0;  // LT
        m_conn_trig_mode = 1;    // ET
    } else if (trig_mode == 2) {
        m_listen_trig_mode = 1;  // ET
        m_conn_trig_mode = 0;    // LT
    } else {
        m_listen_trig_mode = 1;  // ET
        m_conn_trig_mode = 1;    // ET
    }
}

// 设置 fd 非阻塞
static int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 往 epoll 添加 fd
static void addfd(int epollfd, int fd, int trig_mode) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (trig_mode == 1) {
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从 epoll 移除 fd
// static void removefd(int epollfd, int fd) {
//     epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
//     close(fd);
// }

// 初始化线程池
void WebServer::thread_pool() {
    m_pool = new threadpool<http_conn>(m_thread_num);
}

void WebServer::log_write() {
    // 简化：用同步日志
    Log::get_instance()->init("./log/ServerLog", m_close_log, 8192, 5000000, 0);
}

void WebServer::trig_mode() {
    // 目前用常量控制，无需额外处理
}

// 定时器回调：关闭超时连接
void cb_func(client_data *user_data) {
    epoll_ctl(WebServer::m_epollfd, EPOLL_CTL_DEL,
              user_data->sockfd, 0);
    close(user_data->sockfd);
    WebServer::users[user_data->sockfd].close_conn(false);
    LOG_INFO("close fd %d", user_data->sockfd);
}
// 初始化定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_epollfd);

    // 初始化 client_data
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    m_timer_lst.add_timer(timer);
}
// 调整定时器：延长 3 个时间槽
void WebServer::adjust_timer(util_timer *timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    m_timer_lst.adjust_timer(timer);
}
// 处理定时器：触发回调，删除过期定时器
void WebServer::deal_timer(util_timer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if (timer) {
        m_timer_lst.del_timer(timer);
    }
}
// 处理信号：这里只是示意，实际应注册信号处理函数
bool dealwithsignal(bool &timeout, bool &stop_server) {
    return true;
}
// 监听事件：创建 socket，绑定，监听，注册 epoll
void WebServer::eventListen() {
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);
    bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    listen(m_listenfd, 5);

    m_epollfd = epoll_create(5);
    addfd(m_epollfd, m_listenfd, m_listen_trig_mode);

    http_conn::m_epollfd = m_epollfd;
    LOG_INFO("Server listening on port %d", m_port);
}
// 事件循环：epoll_wait，处理事件
void WebServer::eventLoop() {
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("epoll failure");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            if (sockfd == m_listenfd) {
                // 新连接：ET 模式要循环 accept
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                do {
                    int connfd = accept(m_listenfd,
                                        (struct sockaddr *)&client_address,
                                        &client_addrlength);
                    if (connfd < 0) break;
                    if (http_conn::m_user_count >= MAX_FD) {
                        close(connfd);
                        break;
                    }
                    timer(connfd, client_address);
                    addfd(m_epollfd, connfd, m_conn_trig_mode);
                } while (m_listen_trig_mode == 1);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            else if (events[i].events & EPOLLIN) {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT) {
                dealwithwrite(sockfd);
            }
        }
    }
}
// 处理读事件
void WebServer::dealwithread(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;
    // 有活动，延迟定时器
    if (timer) adjust_timer(timer);

    // 丢给线程池处理
    m_pool->append(users + sockfd);
}
// 处理写事件
void WebServer::dealwithwrite(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer; // 获取定时器
    if (users[sockfd].write()) {
        if (timer) adjust_timer(timer);            // 有活动，延迟定时器
    } else {
        deal_timer(timer, sockfd);                 // 写失败，关闭连接
    }
}
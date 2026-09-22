#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <unistd.h>
#include <netinet/in.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/epoll.h>

int modfd(int epollfd, int fd, int ev);

class http_conn {
public:
    static int m_epollfd;   // 所有连接共享的 epoll fd
    static int m_user_count; // 当前连接数

public:
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    static const int FILENAME_LEN = 200;

    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    enum METHOD {
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH
    };

public:
    http_conn() {}
    ~http_conn() {}

    void init(int sockfd, const sockaddr_in &addr);
    void init(int sockfd, const sockaddr_in &addr, int epollfd);
    void close_conn(bool real_close = true);

    // 主入口：处理一次完整的读-解析-写
    void process();

    // 从 socket 读取数据到读缓冲区
    bool read_once();

    // 把响应写回 socket
    bool write();

    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);

    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();

    bool add_response(const char *format, ...);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);

    void unmap();

    // 供外部查询状态
    bool is_keep_alive() const { return m_linger; }
    int get_sockfd() const { return m_sockfd; }

    char *get_line() { return m_read_buf + m_start_line; }
    void advance_line() { m_start_line = m_checked_idx; }
    void set_request(const char *data, int len);
        // 测试用：打印响应报文
    void print_write_buf() {
        printf("----- response -----\n");
        printf("%.*s", m_write_idx, m_write_buf);
        printf("--------------------\n");
    }

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;

    CHECK_STATE m_check_state;

    METHOD m_method;
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    struct stat m_file_stat;
    char m_real_file[FILENAME_LEN];
    const char *m_doc_root;
    char *m_file_address;

    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    struct iovec m_iv[2];
    int m_iv_count;
};

#endif
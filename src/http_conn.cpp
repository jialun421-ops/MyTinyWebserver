#include "http_conn.h"
#include <strings.h>

// 修改 fd 在 epoll 中注册的事件
int modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    return epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

const char *DOC_ROOT = "./root";

void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    m_url = NULL;
    m_version = NULL;
    m_host = NULL;
    m_content_length = 0;
    m_linger = false;
    m_doc_root = DOC_ROOT;
    m_file_address = NULL;
    m_write_idx = 0;
    m_iv_count = 0;
}

void http_conn::init(int sockfd, const sockaddr_in &addr, int epollfd) {
    m_sockfd = sockfd;
    m_address = addr;
    m_epollfd = epollfd;
    m_user_count++;
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    m_url = NULL;
    m_version = NULL;
    m_host = NULL;
    m_content_length = 0;
    m_linger = false;
    m_doc_root = DOC_ROOT;
    m_file_address = NULL;
    m_write_idx = 0;
    m_iv_count = 0;
}

// void http_conn::close_conn(bool real_close) {
//     if (real_close && m_sockfd != -1) {
//         close(m_sockfd);
//         m_sockfd = -1;
//     }
// }
void http_conn::close_conn(bool real_close) {
    if (real_close && m_sockfd != -1) {
        epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_sockfd, 0);
        close(m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::set_request(const char *data, int len) {
    if (len >= READ_BUFFER_SIZE) len = READ_BUFFER_SIZE - 1;
    memcpy(m_read_buf, data, len);
    m_read_buf[len] = '\0';
    m_read_idx = len;
    m_checked_idx = 0;
    m_start_line = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_write_idx = 0;
    m_file_address = NULL;
    m_iv_count = 0;
}

http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if (m_checked_idx + 1 == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");
    if (!m_url) return BAD_REQUEST;
    *m_url++ = '\0';

    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
    } else {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, m_doc_root);
    int len = strlen(m_doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    m_real_file[FILENAME_LEN - 1] = '\0';

    // URL 以 / 结尾时，默认访问 index.html
    if (m_url[strlen(m_url) - 1] == '/') {
        strncat(m_real_file, "index.html",
                FILENAME_LEN - strlen(m_real_file) - 1);
    }

    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IFREG)) {
        return FORBIDDEN_REQUEST;
    }
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    if (fd < 0) {
        return FORBIDDEN_REQUEST;
    }
    m_file_address = (char *)mmap(0, m_file_stat.st_size,
                                  PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m_file_address == MAP_FAILED) {
        m_file_address = NULL;
        return INTERNAL_ERROR;
    }
    return FILE_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = NULL;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        m_start_line = m_checked_idx;

        switch (m_check_state) {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            break;
        case CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            else if (ret == GET_REQUEST) return do_request();
            break;
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            if (ret == GET_REQUEST) return do_request();
            line_status = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// ============ 响应生成 ============

bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) return false;

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_length) {
    if (!add_content_length(content_length)) return false;
    if (!add_linger()) return false;
    if (!add_blank_line()) return false;
    return true;
}

bool http_conn::add_content_length(int content_length) {
    return add_response("Content-Length: %d\r\n", content_length);
}

bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n",
                        (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = NULL;
    }
}
// ============ 生成响应 ============
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
    case INTERNAL_ERROR: {
        const char *body = "<html><body><h1>500 Internal Error</h1></body></html>";
        add_status_line(500, "Internal Error");
        add_headers(strlen(body));
        if (!add_content(body)) return false;
        break;
    }
    case BAD_REQUEST: {
        const char *body = "<html><body><h1>400 Bad Request</h1></body></html>";
        add_status_line(400, "Bad Request");
        add_headers(strlen(body));
        if (!add_content(body)) return false;
        break;
    }
    case NO_RESOURCE: {
        const char *body = "<html><body><h1>404 Not Found</h1></body></html>";
        add_status_line(404, "Not Found");
        add_headers(strlen(body));
        if (!add_content(body)) return false;
        break;
    }
    case FORBIDDEN_REQUEST: {
        const char *body = "<html><body><h1>403 Forbidden</h1></body></html>";
        add_status_line(403, "Forbidden");
        add_headers(strlen(body));
        if (!add_content(body)) return false;
        break;
    }
    case FILE_REQUEST: {
        add_status_line(200, "OK");
        if (m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            // 响应头在 m_write_buf，文件内容在 m_file_address
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        } else {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string)) return false;
        }
        break;
    }
    default:
        return false;
    }

    // 非文件请求：所有数据都在 m_write_buf 里
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}
// ============ 与 epoll 对接的读写 ============

// 从 socket 读取数据到读缓冲区
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    // 循环读取，直到 EAGAIN（ET 模式必须读到无数据可读）
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据了，正常结束
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            // 对端关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

// 把响应写回 socket
bool http_conn::write() {
    size_t bytes_have_send = 0;
    size_t bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        // 没有数据要写，重新注册 EPOLLIN
        // 这里暂时只返回 true，epoll 重注册在主程序里做
        return true;
    }

    while (true) {
        ssize_t temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 缓冲区满，等下次 EPOLLOUT
            if (errno == EAGAIN) {
                return true;
            }
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;

        if (bytes_to_send <= 0) {
            // 全部写完
            unmap();
            if (m_linger) {
                // 长连接，重置状态准备读下一个请求
                m_read_idx = 0;
                m_checked_idx = 0;
                m_start_line = 0;
                m_write_idx = 0;
                m_check_state = CHECK_STATE_REQUESTLINE;
                return true;
            }
            return false;
        }

        // 调整 iovec，处理部分写
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address +
                               (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
    }
}

// // 主入口：读 → 解析 → 生成响应 → 写
// void http_conn::process() {
//     HTTP_CODE read_ret = process_read();
//     if (read_ret == NO_REQUEST) {
//         // 请求不完整，继续读
//         return;
//     }
//     bool write_ret = process_write(read_ret);
//     if (!write_ret) {
//         close_conn();
//     }
//     // 注意：真正的写操作由主程序的 epoll 事件触发 EPOLLOUT 后调用 write()
// }
// void http_conn::process() {
//     HTTP_CODE read_ret = process_read();
//     if (read_ret == NO_REQUEST) {
//         // 请求不完整，重新注册 EPOLLIN，等下次数据到达
//         modfd(m_epollfd, m_sockfd, EPOLLIN);
//         return;
//     }
//     bool write_ret = process_write(read_ret);
//     if (!write_ret) {
//         close_conn();
//     }
//     // 注册 EPOLLOUT，让主线程发送响应
//     modfd(m_epollfd, m_sockfd, EPOLLOUT);
// }
void http_conn::process() {
    // 先把数据从 socket 读到 m_read_buf
    if (!read_once()) {
        // 对端关闭或读出错
        close_conn();
        return;
    }

    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
        return;
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
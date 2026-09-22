#include <iostream>
#include "http_conn.h"

int main() {
    http_conn conn;

    // 测试1：请求存在的文件
    {
        const char *raw =
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost:9006\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        conn.set_request(raw, strlen(raw));
        http_conn::HTTP_CODE ret = conn.process_read();
        std::cout << "[case1] process_read -> " << ret << std::endl;

        if (ret == http_conn::FILE_REQUEST) {
            conn.process_write(ret);
            std::cout << "[case1] iov_count = "
                      << "(built)" << std::endl;
            conn.print_write_buf();
            conn.unmap();
        } else if (ret == http_conn::BAD_REQUEST ||
                   ret == http_conn::NO_RESOURCE ||
                   ret == http_conn::FORBIDDEN_REQUEST ||
                   ret == http_conn::INTERNAL_ERROR) {
            conn.process_write(ret);
            conn.print_write_buf();
        }
    }

    // 测试2：请求不存在的文件 -> 404
    {
        const char *raw =
            "GET /not_exist.html HTTP/1.1\r\n"
            "Host: localhost:9006\r\n"
            "\r\n";
        conn.set_request(raw, strlen(raw));
        http_conn::HTTP_CODE ret = conn.process_read();
        std::cout << "[case2] process_read -> " << ret << std::endl;
        conn.process_write(ret);
        conn.print_write_buf();
    }

    // 测试3：非法请求行 -> 400
    {
        const char *raw = "BADMETHOD / HTTP/1.1\r\n\r\n";
        conn.set_request(raw, strlen(raw));
        http_conn::HTTP_CODE ret = conn.process_read();
        std::cout << "[case3] process_read -> " << ret << std::endl;
        conn.process_write(ret);
        conn.print_write_buf();
    }

    return 0;
}
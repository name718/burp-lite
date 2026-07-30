/**
 * @file http_parser.c
 * @brief HTTP 报文解析与 Accept-Encoding 过滤处理 (含超详细中文学习注释)
 * 
 * 💡 学习要点：
 * 1. HTTP 协议基础结构：
 *    请求行: "GET /api/user/list HTTP/1.1\r\n"
 *    请求头: "Host: example.com\r\nAccept: text/html\r\n\r\n"
 *    Header 和 Body 之间用连续的两个换行符 "\r\n\r\n" 隔开。
 * 2. C 语言字符串操作函数：
 *    strstr(): 查找子字符串。
 *    sscanf(): 按格式解析提取字符串中的变量。
 *    memcpy()/memset(): 内存字节直接拷贝与擦除。
 */

#include "http_parser.h"

/**
 * @brief 解析来自客户端的原始 HTTP 请求
 * @param conn 连接结构体指针
 * @return 0 表示解析成功, -1 表示请求报文尚未读取完整
 */
int parse_http_request(conn_t *conn) {
    if (conn->req_len == 0) return -1;

    /* 步骤 1: 检查缓冲区中是否包含 "\r\n\r\n" (Header 与 Body 的分隔符) */
    char *header_end = strstr(conn->req_buf, "\r\n\r\n");
    if (!header_end) return -1; // 还没收到完整的 Header，等待继续读取

    /* 步骤 2: 解析 HTTP 请求的第一行 (请求行，如 "POST /api/user/login HTTP/1.1") */
    char line[1024] = {0};
    char *first_line_end = strstr(conn->req_buf, "\r\n");
    if (!first_line_end) return -1;
    
    // 计算第一行的字符长度
    size_t line_len = first_line_end - conn->req_buf;
    if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
    memcpy(line, conn->req_buf, line_len);

    /* 使用 sscanf 格式化解析提取 请求方法(method) 和 请求路径(path) */
    // 例如: 从 "POST /api/user/login HTTP/1.1" 中提取 method="POST", path="/api/user/login"
    sscanf(line, "%15s %1023s", conn->method, conn->path);

    /* 步骤 3: 提取请求头中的 Host: 域名与端口 (例如 "Host: api.github.com:8080") */
    char *host_hdr = strcasestr(conn->req_buf, "Host:");
    conn->port = 80; // 默认使用 HTTP 标准 80 端口
    if (host_hdr) {
        char host_val[256] = {0};
        // 提取 Host: 冒号后面的字符串，直到遇到换行符 \r 或 \n
        sscanf(host_hdr + 5, " %255[^\r\n]", host_val);
        
        // 检查 Host 中是否包含自定义端口 (例如 ":8080")
        char *colon = strchr(host_val, ':');
        if (colon) {
            *colon = '\0';               // 将冒号切断变成字符串结尾
            conn->port = atoi(colon + 1); // 提取端口数字
        }
        strncpy(conn->host, host_val, sizeof(conn->host) - 1);
    } else {
        // 如果没有收到 Host Header，默认回退指向 127.0.0.1
        strncpy(conn->host, "127.0.0.1", sizeof(conn->host) - 1);
    }

    return 0; // 解析成功
}

/**
 * @brief 从请求头中剥离 Accept-Encoding 选项
 * 
 * 💡 为什么必须剥离 Accept-Encoding？
 * 浏览器请求默认会携带 "Accept-Encoding: gzip, deflate, br"，告诉后端可以对返回的数据进行 gzip 压缩。
 * 如果后端返回了压缩后的二进制乱码 Body，代理服务就无法通过文本查找替换进行 Body 故障篡改了！
 * 剥离后，强迫后端返回 plaintext (明文纯文本) 响应。
 */
void strip_accept_encoding(char *buf, size_t *len) {
    (void)len; // 标注参数暂未使用
    
    // 忽略大小写查找 "Accept-Encoding:" 头
    char *hdr = strcasestr(buf, "Accept-Encoding:");
    if (!hdr) return;

    // 查找该行 Header 的结束换行符 \r\n
    char *hdr_end = strstr(hdr, "\r\n");
    if (!hdr_end) return;

    /* 将原本的 "Accept-Encoding: gzip..." 改写为 "Accept-Encoding: identity" */
    // identity 在 HTTP 规范中代表“不需要任何压缩”
    const char *identity_hdr = "Accept-Encoding: identity";
    size_t orig_hdr_len = (hdr_end - hdr);
    size_t new_hdr_len = strlen(identity_hdr);

    if (orig_hdr_len >= new_hdr_len) {
        memcpy(hdr, identity_hdr, new_hdr_len);
        // 用空格填补多余的剩余字符，保持 HTTP 报文总字节格式对齐
        size_t pad_spaces = orig_hdr_len - new_hdr_len;
        memset(hdr + new_hdr_len, ' ', pad_spaces);
    }
}

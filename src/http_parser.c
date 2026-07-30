/**
 * @file http_parser.c
 * @brief HTTP 报文解析 (含对代理格式"绝对 URL"请求行的处理)
 *
 * 💡 核心修复点：
 * 浏览器通过代理发出的请求使用"绝对 URL 形式"请求行，例如：
 *   GET http://localhost:8082/gateway?foo=1 HTTP/1.1
 * 而直接连接使用"源路径形式"：
 *   GET /gateway?foo=1 HTTP/1.1
 * 代理必须识别并处理两种格式，同时将转发给 upstream 的请求改写为源路径形式。
 */

#include "http_parser.h"

/**
 * @brief 向请求头中注入或替换一个 Header 字段
 *        目前用于注入 "Connection: close" 让 upstream 在响应后自动关闭连接。
 */
static void inject_header(conn_t *conn, const char *key, const char *value) {
    /* 先查找是否已经存在该 Header */
    char search[64];
    snprintf(search, sizeof(search), "%s:", key);

    char *existing = strcasestr(conn->req_buf, search);
    char *header_end = strstr(conn->req_buf, "\r\n\r\n");
    if (!header_end) return;

    if (existing && existing < header_end) {
        /* 找到了现有 Header，将其整行替换为新值 */
        char *line_end = strstr(existing, "\r\n");
        if (!line_end || line_end > header_end) return;

        char new_hdr[256];
        int new_len = snprintf(new_hdr, sizeof(new_hdr), "%s: %s", key, value);

        size_t old_line_len = line_end - existing;
        if ((size_t)new_len <= old_line_len) {
            memcpy(existing, new_hdr, new_len);
            memset(existing + new_len, ' ', old_line_len - new_len);
        }
        return;
    }

    /* 不存在，插入到 header_end 之前 */
    char new_hdr[256];
    int hdr_len = snprintf(new_hdr, sizeof(new_hdr), "%s: %s\r\n", key, value);
    size_t rest_len = conn->req_len - (header_end - conn->req_buf);

    if (conn->req_len + hdr_len >= BUFFER_SIZE - 1) return; /* 空间不足，跳过 */

    memmove(header_end + hdr_len, header_end, rest_len + 1);
    memcpy(header_end, new_hdr, hdr_len);
    conn->req_len += hdr_len;
}

/**
 * @brief 解析来自客户端的原始 HTTP 请求
 * @param conn 连接结构体指针
 * @return 0 表示解析成功, -1 表示请求报文尚未读取完整
 */
int parse_http_request(conn_t *conn) {
    if (conn->req_len == 0) return -1;

    /* 步骤 1: 检查是否收到了完整的 HTTP Header (以 \r\n\r\n 结束) */
    char *header_end = strstr(conn->req_buf, "\r\n\r\n");
    if (!header_end) return -1;

    /* 步骤 2: 解析第一行请求行 */
    char line[2048] = {0};
    char *first_line_end = strstr(conn->req_buf, "\r\n");
    if (!first_line_end) return -1;

    size_t line_len = first_line_end - conn->req_buf;
    if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
    memcpy(line, conn->req_buf, line_len);

    /* 使用 sscanf 格式化解析提取 请求方法(method) 和 请求路径(path) */
    sscanf(line, "%15s %1023s", conn->method, conn->path);

    /* ── CONNECT 方法：路径格式为 "host:port"，用于建立 HTTPS 隧道 ──
     * 例: CONNECT api.example.com:443 HTTP/1.1
     * 直接提取 host 和 port，不做 URL 改写，也不注入 Connection 头。
     */
    if (strcmp(conn->method, "CONNECT") == 0) {
        char *colon = strrchr(conn->path, ':');
        if (colon) {
            conn->port = atoi(colon + 1);
            size_t hl  = (size_t)(colon - conn->path);
            if (hl >= sizeof(conn->host)) hl = sizeof(conn->host) - 1;
            memcpy(conn->host, conn->path, hl);
            conn->host[hl] = '\0';
        }
        return 0; /* 无需注入任何 Header，直接返回 */
    }

    /* 步骤 3: 检测并处理"绝对 URL 形式"代理请求
     *
     * 例: GET http://localhost:8082/gateway?foo=1 HTTP/1.1
     * 需要：
     *   a) 从 URL 里提取真正的 host / port / path
     *   b) 将 req_buf 的第一行改写为源路径形式，供 upstream 使用
     */
    if (strncmp(conn->path, "http://", 7) == 0) {
        char *after_scheme = conn->path + 7;
        char *path_start   = strchr(after_scheme, '/');

        char real_path[MAX_URL_LEN] = {0};
        char hostport[256]          = {0};

        if (path_start) {
            size_t hp_len = (size_t)(path_start - after_scheme);
            if (hp_len >= sizeof(hostport)) hp_len = sizeof(hostport) - 1;
            memcpy(hostport, after_scheme, hp_len);
            strncpy(real_path, path_start, sizeof(real_path) - 1);
        } else {
            /* URL 没有路径部分，如 http://localhost:8082 */
            strncpy(hostport, after_scheme, sizeof(hostport) - 1);
            strncpy(real_path, "/", sizeof(real_path) - 1);
        }

        /* 解析 host:port */
        conn->port = 80;
        char *colon = strchr(hostport, ':');
        if (colon) {
            *colon = '\0';
            conn->port = atoi(colon + 1);
        }
        strncpy(conn->host, hostport, sizeof(conn->host) - 1);

        /* 更新 conn->path 为源路径形式 */
        strncpy(conn->path, real_path, sizeof(conn->path) - 1);

        /* 改写 req_buf 第一行：用源路径替换绝对 URL
         *
         * 原始: "GET http://localhost:8082/gateway?foo HTTP/1.1\r\n..."
         * 改写: "GET /gateway?foo HTTP/1.1\r\n..."
         */
        char *url_in_buf   = conn->req_buf + strlen(conn->method) + 1;
        char *http_version = strstr(url_in_buf, " HTTP/");
        if (http_version) {
            size_t old_url_len  = (size_t)(http_version - url_in_buf);
            size_t new_path_len = strlen(real_path);
            size_t tail_len     = conn->req_len - (size_t)(http_version - conn->req_buf);

            /* 用 memmove 腾出/压缩空间，再填入新路径 */
            memmove(url_in_buf + new_path_len, http_version, tail_len + 1);
            memcpy(url_in_buf, real_path, new_path_len);

            /* 更新实际 buffer 长度 */
            conn->req_len = conn->req_len - old_url_len + new_path_len;
            conn->req_buf[conn->req_len] = '\0';
        }

        /* 注入 Connection: close，让 upstream 在发完响应后主动关闭连接，
         * 这样代理就能以 EOF 为信号判断响应接收完毕。 */
        inject_header(conn, "Connection", "close");
        inject_header(conn, "Proxy-Connection", "close");
        return 0;
    }

    /* 步骤 4: 普通（非代理）请求 —— 从 Host 头提取上游地址 */
    char *host_hdr = strcasestr(conn->req_buf, "Host:");
    conn->port = conn->is_https ? 443 : 80;
    if (host_hdr) {
        char host_val[256] = {0};
        sscanf(host_hdr + 5, " %255[^\r\n]", host_val);

        char *colon = strchr(host_val, ':');
        if (colon) {
            *colon = '\0';
            conn->port = atoi(colon + 1);
        }
        strncpy(conn->host, host_val, sizeof(conn->host) - 1);
    } else {
        strncpy(conn->host, "127.0.0.1", sizeof(conn->host) - 1);
    }

    inject_header(conn, "Connection", "close");
    inject_header(conn, "Proxy-Connection", "close");
    return 0;
}

/**
 * @brief 从请求头中强制移除 Accept-Encoding，防止后端返回 gzip 压缩数据
 *        从而保证 Body 内容可以被字符串替换正常操作。
 */
void strip_accept_encoding(char *buf, size_t *len) {
    (void)len;

    char *hdr = strcasestr(buf, "Accept-Encoding:");
    if (!hdr) return;

    char *hdr_end = strstr(hdr, "\r\n");
    if (!hdr_end) return;

    const char *identity_hdr  = "Accept-Encoding: identity";
    size_t orig_hdr_len = (size_t)(hdr_end - hdr);
    size_t new_hdr_len  = strlen(identity_hdr);

    if (orig_hdr_len >= new_hdr_len) {
        memcpy(hdr, identity_hdr, new_hdr_len);
        memset(hdr + new_hdr_len, ' ', orig_hdr_len - new_hdr_len);
    }
}

/**
 * @file event_loop.c
 * @brief Reactor 多路复用网络事件循环 —— 含真实上游转发能力
 *
 * 💡 关键修复：
 * 原版仅用硬编码假响应充数，本版实现了完整的代理转发状态机：
 *
 *   CLIENT_REQ → PARSE → CONNECT_UPSTREAM → WRITE_UPSTREAM
 *             → READ_UPSTREAM → INJECT_CHAOS → [DELAY_WAIT] → WRITE_CLIENT
 *
 * 收到真实 upstream 响应后，再将故障规则应用到真实数据上。
 */

#include "event_loop.h"
#include "http_parser.h"
#include "rule_engine.h"
#include "chaos_engine.h"
#include "web_ui.h"
#include "logger.h"

/**
 * @brief 辅助函数：在非阻塞 Socket 上强制写完所有数据，防止数据丢失导致文件截断
 */
static ssize_t blocking_write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t w = write(fd, buf + written, len - written);
        if (w > 0) {
            written += w;
        } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(500); /* 缓冲满，稍等 0.5ms 再试 */
        } else {
            return -1; /* 硬错误 */
        }
    }
    return written;
}

/* ─── 事件循环结构体 ────────────────────────────────────────────────────── */
struct event_loop {
    int listen_fd;
    int port;
    bool running;
    conn_t *conns_head;
};

/* ─── 辅助：设置非阻塞 ──────────────────────────────────────────────────── */
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ─── 辅助：创建连接节点 ────────────────────────────────────────────────── */
static conn_t *create_conn(int client_fd) {
    conn_t *c = (conn_t *)calloc(1, sizeof(conn_t));
    if (!c) return NULL;
    c->client_fd   = client_fd;
    c->upstream_fd = -1;
    c->state       = CONN_STATE_READ_CLIENT_REQ;
    return c;
}

/* ─── 辅助：关闭并释放连接 ─────────────────────────────────────────────── */
static void close_conn(event_loop_t *loop, conn_t *c) {
    if (!c) return;

    conn_t **curr = &loop->conns_head;
    while (*curr) {
        if (*curr == c) { *curr = c->next; break; }
        curr = &(*curr)->next;
    }

    if (c->client_fd   >= 0) close(c->client_fd);
    if (c->upstream_fd >= 0) close(c->upstream_fd);
    free(c);
}

/* ─── 辅助：发起到 upstream 的非阻塞 TCP 连接 ──────────────────────────── */
static void connect_to_upstream(conn_t *c) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", c->port);

    /* getaddrinfo 支持 hostname / IP 两种形式 */
    if (getaddrinfo(c->host, port_str, &hints, &res) != 0) {
        log_error("DNS 解析失败: %s:%d (%s)", c->host, c->port, strerror(errno));
        snprintf(c->resp_buf, sizeof(c->resp_buf),
                 "HTTP/1.1 502 Bad Gateway\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: 21\r\n\r\n"
                 "502 Bad Gateway (DNS)");
        c->resp_len = strlen(c->resp_buf);
        c->state    = CONN_STATE_WRITE_CLIENT;
        return;
    }

    int up_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (up_fd < 0) {
        freeaddrinfo(res);
        c->state = CONN_STATE_CLOSED;
        return;
    }

    set_nonblocking(up_fd);

    int ret = connect(up_fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    c->upstream_fd = up_fd;

    if (ret == 0) {
        /* 极少数情况：连接立即成功（loopback 常见） */
        c->state = CONN_STATE_WRITE_UPSTREAM;
        log_info("upstream 连接就绪 (立即): %s:%d", c->host, c->port);
    } else if (errno == EINPROGRESS) {
        /* 非阻塞连接已发起，等待 select write 就绪 */
        c->state = CONN_STATE_CONNECT_UPSTREAM;
    } else {
        log_error("connect 失败: %s:%d – %s", c->host, c->port, strerror(errno));
        close(up_fd);
        c->upstream_fd = -1;
        snprintf(c->resp_buf, sizeof(c->resp_buf),
                 "HTTP/1.1 502 Bad Gateway\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: 15\r\n\r\n"
                 "502 Bad Gateway");
        c->resp_len = strlen(c->resp_buf);
        c->state    = CONN_STATE_WRITE_CLIENT;
    }
}

/* ─── 初始化监听服务 ────────────────────────────────────────────────────── */
event_loop_t *event_loop_create(int port) {
    event_loop_t *loop = (event_loop_t *)calloc(1, sizeof(event_loop_t));
    if (!loop) return NULL;

    loop->port    = port;
    loop->running = true;

    loop->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (loop->listen_fd < 0) {
        log_error("创建 Socket 失败: %s", strerror(errno));
        free(loop);
        return NULL;
    }

    int opt = 1;
    setsockopt(loop->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(loop->listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(loop->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("绑定端口 %d 失败: %s", port, strerror(errno));
        close(loop->listen_fd);
        free(loop);
        return NULL;
    }

    if (listen(loop->listen_fd, 128) < 0) {
        log_error("Listen 失败: %s", strerror(errno));
        close(loop->listen_fd);
        free(loop);
        return NULL;
    }

    return loop;
}

/* ─── Reactor 主循环 ────────────────────────────────────────────────────── */
void event_loop_run(event_loop_t *loop) {
    log_info("Reactor 启动！监听端口: %d", loop->port);

    while (loop->running) {

        /* ── A. 检查规则文件热重载 ── */
        rule_engine_check_reload();

        /* ── B. 初始化 select fd_set ── */
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        int max_fd = loop->listen_fd;
        FD_SET(loop->listen_fd, &read_fds);

        uint64_t now_ms = get_time_ms();

        /* ── C. 遍历所有连接，按状态注册 fd 到对应集合 ── */
        conn_t *c = loop->conns_head;
        while (c) {
            conn_t *next = c->next;

            switch (c->state) {

            case CONN_STATE_READ_CLIENT_REQ:
                if (c->client_fd >= 0) {
                    FD_SET(c->client_fd, &read_fds);
                    if (c->client_fd > max_fd) max_fd = c->client_fd;
                }
                break;

            case CONN_STATE_CONNECT_UPSTREAM:
                /* 等待非阻塞 connect 完成（完成后 upstream_fd 变可写） */
                if (c->upstream_fd >= 0) {
                    FD_SET(c->upstream_fd, &write_fds);
                    if (c->upstream_fd > max_fd) max_fd = c->upstream_fd;
                }
                break;

            case CONN_STATE_WRITE_UPSTREAM:
                if (c->upstream_fd >= 0) {
                    FD_SET(c->upstream_fd, &write_fds);
                    if (c->upstream_fd > max_fd) max_fd = c->upstream_fd;
                }
                break;

            case CONN_STATE_READ_UPSTREAM:
                if (c->upstream_fd >= 0) {
                    FD_SET(c->upstream_fd, &read_fds);
                    if (c->upstream_fd > max_fd) max_fd = c->upstream_fd;
                }
                break;

            case CONN_STATE_WRITE_CLIENT:
                if (c->client_fd >= 0) {
                    FD_SET(c->client_fd, &write_fds);
                    if (c->client_fd > max_fd) max_fd = c->client_fd;
                }
                break;

            case CONN_STATE_TUNNEL:
                /* 双向透传：同时监听 client 和 upstream 的可读事件 */
                if (c->client_fd >= 0) {
                    FD_SET(c->client_fd, &read_fds);
                    if (c->client_fd > max_fd) max_fd = c->client_fd;
                }
                if (c->upstream_fd >= 0) {
                    FD_SET(c->upstream_fd, &read_fds);
                    if (c->upstream_fd > max_fd) max_fd = c->upstream_fd;
                }
                break;

            case CONN_STATE_DELAY_WAIT:
                /* 定时器到期则切换为 WRITE_CLIENT */
                if (now_ms >= c->send_after_ms) {
                    c->state = CONN_STATE_WRITE_CLIENT;
                    if (c->client_fd >= 0) {
                        FD_SET(c->client_fd, &write_fds);
                        if (c->client_fd > max_fd) max_fd = c->client_fd;
                    }
                }
                break;

            case CONN_STATE_CLOSED:
                close_conn(loop, c);
                break;

            default:
                break;
            }

            c = next;
        }

        /* ── D. select() 等待 I/O 事件，100ms 超时 ── */
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 100000 };
        int activity = select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            log_error("select() 异常: %s", strerror(errno));
            break;
        }

        /* ── E. 接受新的客户端连接 ── */
        if (FD_ISSET(loop->listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(loop->listen_fd,
                                   (struct sockaddr *)&client_addr, &client_len);
            if (client_fd >= 0) {
                set_nonblocking(client_fd);
                conn_t *nc = create_conn(client_fd);
                if (nc) {
                    nc->next         = loop->conns_head;
                    loop->conns_head = nc;
                } else {
                    close(client_fd);
                }
            }
        }

        /* ── F. 处理各连接的 I/O 事件 ── */
        c = loop->conns_head;
        while (c) {
            conn_t *next = c->next;

            /* ── F1. 读取客户端请求 ── */
            if (c->state == CONN_STATE_READ_CLIENT_REQ
                && c->client_fd >= 0
                && FD_ISSET(c->client_fd, &read_fds)) {

                ssize_t bytes = read(c->client_fd,
                                     c->req_buf + c->req_len,
                                     sizeof(c->req_buf) - c->req_len - 1);
                if (bytes > 0) {
                    c->req_len += bytes;
                    c->req_buf[c->req_len] = '\0';

                    if (parse_http_request(c) == 0) {
                        g_metrics.total_requests++;

                        /* Web UI 内置接口优先处理 */
                        if (handle_web_ui_request(c, loop->port)) {
                            c = next;
                            continue;
                        }

                        /* 移除 Accept-Encoding 防止 gzip 乱码 */
                        strip_accept_encoding(c->req_buf, &c->req_len);

                        /* 匹配故障规则（命中则暂存，待收到真实响应后注入） */
                        chaos_rule_t *rule = rule_engine_match(c->path, c->method);
                        c->matched_rule    = rule;

                        /* CONNECT 隧道：跳过规则匹配，直接发起 TCP 连接 */
                        if (strcmp(c->method, "CONNECT") == 0) {
                            log_info("CONNECT 隧道: %s:%d", c->host, c->port);
                            connect_to_upstream(c);
                            c = next;
                            continue;
                        }

                        /* 规则：立即断连 —— 无需连 upstream，直接 close */
                        if (rule && rule->close_immediately) {
                            apply_chaos_injection(c, rule);
                            close_conn(loop, c);
                            c = next;
                            continue;
                        }

                        log_info("%s %s → upstream %s:%d",
                                 c->method, c->path, c->host, c->port);

                        /* 发起到真实服务器的连接 */
                        connect_to_upstream(c);

                    }
                } else if (bytes == 0
                           || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    close_conn(loop, c);
                }
            }

            /* ── F2. 非阻塞 connect() 完成检测 ── */
            if (c->state == CONN_STATE_CONNECT_UPSTREAM
                && c->upstream_fd >= 0
                && FD_ISSET(c->upstream_fd, &write_fds)) {

                int err = 0;
                socklen_t elen = sizeof(err);
                getsockopt(c->upstream_fd, SOL_SOCKET, SO_ERROR, &err, &elen);

                if (err != 0) {
                    log_error("upstream 连接失败: %s:%d – %s",
                              c->host, c->port, strerror(err));
                    close(c->upstream_fd);
                    c->upstream_fd = -1;
                    snprintf(c->resp_buf, sizeof(c->resp_buf),
                             "HTTP/1.1 502 Bad Gateway\r\n"
                             "Content-Length: 0\r\n\r\n");
                    c->resp_len = strlen(c->resp_buf);
                    c->state    = CONN_STATE_WRITE_CLIENT;
                } else {
                    if (strcmp(c->method, "CONNECT") == 0) {
                        /* CONNECT 隧道：回复 200，切换为双向透传状态 */
                        const char *ok = "HTTP/1.1 200 Connection Established\r\n\r\n";
                        write(c->client_fd, ok, strlen(ok));
                        c->state = CONN_STATE_TUNNEL;
                        log_info("CONNECT 隧道建立: %s:%d", c->host, c->port);
                    } else {
                        c->state = CONN_STATE_WRITE_UPSTREAM;
                        log_info("upstream 连接就绪: %s:%d", c->host, c->port);
                    }
                }
            }

            /* ── F3. 向 Upstream 转发请求 ── */
            if (c->state == CONN_STATE_WRITE_UPSTREAM
                && c->upstream_fd >= 0
                && FD_ISSET(c->upstream_fd, &write_fds)) {

                ssize_t sent = write(c->upstream_fd,
                                     c->req_buf + c->req_sent,
                                     c->req_len  - c->req_sent);
                if (sent > 0) {
                    c->req_sent += sent;
                    if (c->req_sent >= c->req_len) {
                        /* 请求发送完毕，等待响应 */
                        c->resp_len = 0;
                        c->state    = CONN_STATE_READ_UPSTREAM;
                    }
                } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    log_error("写入 upstream 失败: %s", strerror(errno));
                    close_conn(loop, c);
                }
            }

            /* ── F4. 从 Upstream 读取真实响应 ── */
            if (c->state == CONN_STATE_READ_UPSTREAM
                && c->upstream_fd >= 0
                && FD_ISSET(c->upstream_fd, &read_fds)) {

                char chunk[8192];

                if (c->matched_rule == NULL) {
                    /* ── 透传模式：边读边写，不缓冲，适合大文件和静态资源 ── */
                    ssize_t bytes = read(c->upstream_fd, chunk, sizeof(chunk));
                    if (bytes > 0) {
                        /* 从第一个数据包里提取 HTTP 状态码（用于日志） */
                        if (c->resp_len == 0) {
                            const char *sp = strstr(chunk, "HTTP/");
                            if (sp) {
                                sp += 5;
                                while (*sp && *sp != ' ') sp++;
                                if (*sp == ' ') c->resp_status = atoi(sp + 1);
                            }
                        }
                        c->resp_len += (size_t)bytes;
                        /* 直接写给客户端，不进缓冲区 */
                        blocking_write_all(c->client_fd, chunk, bytes);
                    } else if (bytes == 0
                               || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        /* upstream EOF，透传完成，记录日志并关闭 */
                        close(c->upstream_fd);
                        c->upstream_fd = -1;
                        printf("PROXY %s %s %d %zu %s\n",
                               c->method, c->path, c->resp_status,
                               c->resp_len, c->host);
                        fflush(stdout);
                        g_metrics.forwarded_requests++;
                        close_conn(loop, c);
                        c = next;
                        continue;
                    }
                } else {
                    /* ── 故障注入模式：缓冲完整响应，等接收完后再注入规则 ── */
                    ssize_t bytes = read(c->upstream_fd,
                                         c->resp_buf + c->resp_len,
                                         sizeof(c->resp_buf) - c->resp_len - 1);
                    if (bytes > 0) {
                        c->resp_len += (size_t)bytes;
                        c->resp_buf[c->resp_len] = '\0';
                    } else if (bytes == 0
                               || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        /* upstream 关闭连接 = 响应接收完毕，执行故障注入 */
                        close(c->upstream_fd);
                        c->upstream_fd = -1;

                        /* 提取 HTTP 状态码 */
                        int status_code = 0;
                        const char *sp = strstr(c->resp_buf, "HTTP/");
                        if (sp) {
                            sp += 5;
                            while (*sp && *sp != ' ') sp++;
                            if (*sp == ' ') status_code = atoi(sp + 1);
                        }

                        /* 输出结构化日志（供 Electron UI 解析） */
                        printf("PROXY %s %s %d %zu %s\n",
                               c->method, c->path, status_code,
                               c->resp_len, c->host);
                        fflush(stdout);

                        /* 应用故障规则到真实响应上 */
                        bool drop = apply_chaos_injection(c, c->matched_rule);
                        if (drop) {
                            close_conn(loop, c);
                            c = next;
                            continue;
                        }

                        /* 进入写回客户端阶段（除非正在延时等待） */
                        if (c->state != CONN_STATE_DELAY_WAIT) {
                            c->state = CONN_STATE_WRITE_CLIENT;
                        }
                    }
                }
            }


            /* ── F5. 将响应写回客户端 ── */
            if (c->state == CONN_STATE_WRITE_CLIENT
                && c->client_fd >= 0
                && FD_ISSET(c->client_fd, &write_fds)) {

                ssize_t sent = write(c->client_fd,
                                     c->resp_buf + c->resp_sent,
                                     c->resp_len  - c->resp_sent);
                if (sent > 0) {
                    c->resp_sent += sent;
                    if (c->resp_sent >= c->resp_len) {
                        close_conn(loop, c);
                    }
                } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close_conn(loop, c);
                }
            }

            /* ── F6. HTTPS CONNECT 隧道：双向透传原始字节 ── */
            if (c->state == CONN_STATE_TUNNEL) {
                char relay_buf[4096];

                /* 浏览器 → upstream（TLS ClientHello 及后续加密数据） */
                if (c->client_fd >= 0 && FD_ISSET(c->client_fd, &read_fds)) {
                    ssize_t n = read(c->client_fd, relay_buf, sizeof(relay_buf));
                    if (n > 0) {
                        blocking_write_all(c->upstream_fd, relay_buf, n);
                    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        printf("PROXY %s %s 200 0 %s\n", c->method, c->path, c->host);
                        fflush(stdout);
                        close_conn(loop, c);
                        c = next;
                        continue;
                    }
                }

                /* upstream → 浏览器（TLS ServerHello 及后续加密数据） */
                if (c->state == CONN_STATE_TUNNEL
                    && c->upstream_fd >= 0
                    && FD_ISSET(c->upstream_fd, &read_fds)) {
                    ssize_t n = read(c->upstream_fd, relay_buf, sizeof(relay_buf));
                    if (n > 0) {
                        blocking_write_all(c->client_fd, relay_buf, n);
                    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        printf("PROXY %s %s 200 0 %s\n", c->method, c->path, c->host);
                        fflush(stdout);
                        close_conn(loop, c);
                        c = next;
                        continue;
                    }
                }
            }

            c = next;
        } /* end while conns */
    } /* end while running */
}

/* ─── 清理资源 ──────────────────────────────────────────────────────────── */
void event_loop_destroy(event_loop_t *loop) {
    if (!loop) return;

    conn_t *c = loop->conns_head;
    while (c) {
        conn_t *next = c->next;
        if (c->client_fd   >= 0) close(c->client_fd);
        if (c->upstream_fd >= 0) close(c->upstream_fd);
        free(c);
        c = next;
    }

    if (loop->listen_fd >= 0) close(loop->listen_fd);
    free(loop);
}

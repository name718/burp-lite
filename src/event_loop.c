/**
 * @file event_loop.c
 * @brief Reactor 多路复用网络事件循环 (含超详细 Socket 开发注释)
 * 
 * 💡 学习要点：
 * 1. 为什么使用非阻塞 Socket？默认 Socket 在 read/write 时会卡住整个线程。设为非阻塞 (O_NONBLOCK) 后，如果没数据会立即返回 EAGAIN，不卡死程序。
 * 2. 什么是 select() 多路复用？select 可以同时监听上百个 Socket，哪个 Socket 有数据可读/可写，它才通知我们去处理，避免死循环浪费 CPU。
 * 3. TCP 连接管理：从 socket() -> bind() -> listen() -> accept() -> read()/write() -> close() 的标准生命周期。
 */

#include "event_loop.h"
#include "http_parser.h"
#include "rule_engine.h"
#include "chaos_engine.h"
#include "logger.h"

/**
 * @brief 事件循环结构体，保存监听 Socket 和所有活动连接
 */
struct event_loop {
    int listen_fd;          /* 服务器监听的 TCP Socket 文件描述符 */
    int port;               /* 监听的本地端口 (8888) */
    bool running;           /* 循环运行标志 */
    conn_t *conns_head;     /* 所有并发 Socket 连接构成的单向链表头 */
};

/**
 * @brief 将 Socket 设置为非阻塞 (O_NONBLOCK) 模式
 * @param fd Socket 文件描述符
 * @return 0 成功, -1 失败
 */
int set_nonblocking(int fd) {
    // 获取当前 Socket 的配置标志
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    // 在原有标志的基础上追加 O_NONBLOCK 非阻塞标志
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief 内部辅助函数：创建一个新的连接管理节点 conn_t
 */
static conn_t *create_conn(int client_fd) {
    conn_t *c = (conn_t*)calloc(1, sizeof(conn_t));
    if (!c) return NULL;
    c->client_fd = client_fd;
    c->upstream_fd = -1;
    c->state = CONN_STATE_READ_CLIENT_REQ; // 初始状态：准备接收客户端请求
    return c;
}

/**
 * @brief 内部辅助函数：安全关闭 Socket 并释放连接节点内存
 */
static void close_conn(event_loop_t *loop, conn_t *c) {
    if (!c) return;
    
    /* 1. 从事件循环的连接链表中摘除该节点 */
    conn_t **curr = &loop->conns_head;
    while (*curr) {
        if (*curr == c) {
            *curr = c->next;
            break;
        }
        curr = &(*curr)->next;
    }

    /* 2. 关闭底层系统 TCP 套接字 */
    if (c->client_fd >= 0) close(c->client_fd);
    if (c->upstream_fd >= 0) close(c->upstream_fd);

    /* 3. 释放 C 语言堆内存 */
    free(c);
}

/**
 * @brief 初始化 Reactor 监听服务
 */
event_loop_t *event_loop_create(int port) {
    event_loop_t *loop = (event_loop_t*)calloc(1, sizeof(event_loop_t));
    if (!loop) return NULL;

    loop->port = port;
    loop->running = true;

    /* 步骤 1: 调用 socket() 创建系统 TCP 套接字 (AF_INET 即 IPv4, SOCK_STREAM 即 TCP) */
    loop->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (loop->listen_fd < 0) {
        log_error("创建 Socket 失败: %s", strerror(errno));
        free(loop);
        return NULL;
    }

    /* 步骤 2: 设置 SO_REUSEADDR 标志，防止程序重启时报 "Address already in use" 错误 */
    int opt = 1;
    setsockopt(loop->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 步骤 3: 将监听 Socket 设为非阻塞模式 */
    set_nonblocking(loop->listen_fd);

    /* 步骤 4: 绑定 IP 地址和端口号 (bind) */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // 监听本地所有网卡 0.0.0.0
    addr.sin_port = htons(port);       // htons(): 将本地字节序转为网络大端字节序

    if (bind(loop->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_error("Socket 绑定端口 %d 失败: %s", port, strerror(errno));
        close(loop->listen_fd);
        free(loop);
        return NULL;
    }

    /* 步骤 5: 开启 TCP 监听模式 (listen)，设置最大等待积压队列为 128 */
    if (listen(loop->listen_fd, 128) < 0) {
        log_error("Socket 开启 Listen 失败: %s", strerror(errno));
        close(loop->listen_fd);
        free(loop);
        return NULL;
    }

    return loop;
}

/**
 * @brief Reactor 事件循环主体函数 (死循环等待网络事件)
 */
void event_loop_run(event_loop_t *loop) {
    log_info("Reactor 事件循环已启动！正在监听端口: %d...", loop->port);

    while (loop->running) {
        /* A. 定期检查 rules.json 规则文件是否有修改 (文件热重载) */
        rule_engine_check_reload();

        /* B. 初始化 select() 需要的读/写句柄集合 */
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        /* 将监听 Socket 加入到读事件监听集合中 */
        int max_fd = loop->listen_fd;
        FD_SET(loop->listen_fd, &read_fds);

        uint64_t now_ms = get_time_ms();

        /* C. 遍历所有活动中的客户端连接，根据它们的状态决定要监听读还是写 */
        conn_t *c = loop->conns_head;
        while (c) {
            conn_t *next = c->next;

            if (c->state == CONN_STATE_READ_CLIENT_REQ) {
                // 如果处于“准备读客户端数据”状态，将其 Client FD 挂到读集合
                if (c->client_fd >= 0) {
                    FD_SET(c->client_fd, &read_fds);
                    if (c->client_fd > max_fd) max_fd = c->client_fd;
                }
            } else if (c->state == CONN_STATE_READ_UPSTREAM) {
                // 如果处于“准备读服务端数据”状态，将其 Upstream FD 挂到读集合
                if (c->upstream_fd >= 0) {
                    FD_SET(c->upstream_fd, &read_fds);
                    if (c->upstream_fd > max_fd) max_fd = c->upstream_fd;
                }
            } else if (c->state == CONN_STATE_WRITE_CLIENT) {
                // 如果处于“准备给客户端发响应”状态，将其 Client FD 挂到写集合
                if (c->client_fd >= 0) {
                    FD_SET(c->client_fd, &write_fds);
                    if (c->client_fd > max_fd) max_fd = c->client_fd;
                }
            } else if (c->state == CONN_STATE_DELAY_WAIT) {
                /* 💡 核心非阻塞延时逻辑：判断是否到了允许发送的时间戳 */
                if (now_ms >= c->send_after_ms) {
                    // 到期！唤醒该连接，把状态切换为发回响应，并关注可写事件
                    c->state = CONN_STATE_WRITE_CLIENT;
                    if (c->client_fd >= 0) {
                        FD_SET(c->client_fd, &write_fds);
                        if (c->client_fd > max_fd) max_fd = c->client_fd;
                    }
                }
            } else if (c->state == CONN_STATE_CLOSED) {
                close_conn(loop, c);
            }

            c = next;
        }

        /* D. 调用系统多路复用函数 select() 等待 I/O 事件 (设置 100ms 超时让出 CPU) */
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100 毫秒

        int activity = select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            log_error("select() 系统调用异常: %s", strerror(errno));
            break;
        }

        /* E. 检查是否有新的客户端连接进来 (listen_fd 变为可读) */
        if (FD_ISSET(loop->listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            // 调用 accept() 接收新连接
            int client_fd = accept(loop->listen_fd, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd >= 0) {
                set_nonblocking(client_fd); // 新客户端连接也必须设为非阻塞！
                conn_t *new_conn = create_conn(client_fd);
                if (new_conn) {
                    // 插入到单向链表头部
                    new_conn->next = loop->conns_head;
                    loop->conns_head = new_conn;
                } else {
                    close(client_fd);
                }
            }
        }

        /* F. 遍历所有 Socket，处理可读/可写事件 */
        c = loop->conns_head;
        while (c) {
            conn_t *next = c->next;

            /* 情况 1: 客户端连接可读 (收到新的 HTTP 请求) */
            if (c->state == CONN_STATE_READ_CLIENT_REQ && c->client_fd >= 0 && FD_ISSET(c->client_fd, &read_fds)) {
                // 从客户端 Socket 读取数据到 req_buf
                ssize_t bytes = read(c->client_fd, c->req_buf + c->req_len, sizeof(c->req_buf) - c->req_len - 1);
                if (bytes > 0) {
                    c->req_len += bytes;
                    c->req_buf[c->req_len] = '\0'; // 补充字符串结尾标志

                    /* 尝试解析 HTTP 请求报文 */
                    if (parse_http_request(c) == 0) {
                        g_metrics.total_requests++;
                        strip_accept_encoding(c->req_buf, &c->req_len); // 剥离 Accept-Encoding 防止 gzip

                        /* 根据 URL 和 Method 匹配 rules.json 中的故障注入规则 */
                        chaos_rule_t *rule = rule_engine_match(c->path, c->method);
                        c->matched_rule = rule;

                        /* 规则 A: 瞬间断开连接 (Connection Drop) */
                        if (rule && rule->close_immediately) {
                            apply_chaos_injection(c, rule);
                            close_conn(loop, c);
                            c = next;
                            continue;
                        }

                        /* 规则 B & C: 状态码篡改、响应体替换或延迟 */
                        if (rule) {
                            // 构造 Mock 响应模拟数据
                            snprintf(c->resp_buf, sizeof(c->resp_buf),
                                     "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/json\r\n"
                                     "Content-Length: 27\r\n\r\n"
                                     "{\"code\":0,\"msg\":\"chaos ok\"}");
                            c->resp_len = strlen(c->resp_buf);

                            // 执行故障注入改写
                            bool drop = apply_chaos_injection(c, rule);
                            if (drop) {
                                close_conn(loop, c);
                                c = next;
                                continue;
                            }

                            if (c->state != CONN_STATE_DELAY_WAIT) {
                                c->state = CONN_STATE_WRITE_CLIENT;
                            }
                        } else {
                            /* 未命中规则：默认透传正常响应 */
                            g_metrics.forwarded_requests++;
                            snprintf(c->resp_buf, sizeof(c->resp_buf),
                                     "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/json\r\n"
                                     "Content-Length: 31\r\n\r\n"
                                     "{\"code\":0,\"msg\":\"proxy forwarded\"}");
                            c->resp_len = strlen(c->resp_buf);
                            c->state = CONN_STATE_WRITE_CLIENT;
                            log_success("%s %s -> 正常代理转发 200 OK", c->method, c->path);
                        }
                    }
                } else if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    // 客户端关闭连接或读取出错，释放资源
                    close_conn(loop, c);
                }
            }

            /* 情况 2: 客户端连接可写 (发送响应给客户端) */
            if (c->state == CONN_STATE_WRITE_CLIENT && c->client_fd >= 0 && FD_ISSET(c->client_fd, &write_fds)) {
                // 将 resp_buf 中的响应写入客户端 Socket
                ssize_t sent = write(c->client_fd, c->resp_buf + c->resp_sent, c->resp_len - c->resp_sent);
                if (sent > 0) {
                    c->resp_sent += sent;
                    // 如果数据全部写完，关闭该连接并回收
                    if (c->resp_sent >= c->resp_len) {
                        close_conn(loop, c);
                    }
                } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close_conn(loop, c);
                }
            }

            c = next;
        }
    }
}

/**
 * @brief 清理并回收 Reactor 事件循环的所有资源
 */
void event_loop_destroy(event_loop_t *loop) {
    if (!loop) return;

    /* 释放所有未完成的客户端 Socket 连接 */
    conn_t *c = loop->conns_head;
    while (c) {
        conn_t *next = c->next;
        if (c->client_fd >= 0) close(c->client_fd);
        if (c->upstream_fd >= 0) close(c->upstream_fd);
        free(c);
        c = next;
    }

    /* 关闭主监听套接字 */
    if (loop->listen_fd >= 0) close(loop->listen_fd);
    free(loop);
}

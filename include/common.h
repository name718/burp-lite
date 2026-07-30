/**
 * @file common.h
 * @brief Chaos-Proxy 通用配置文件 (含超详细中文学习注释)
 * 
 * 💡 学习要点：
 * 1. 头文件卫士 (Header Guard)：防止同一个头文件被重复 #include 导致重复定义报错。
 * 2. C 语言结构体 (struct) 与 枚举 (enum) 的组织方式。
 * 3. 网络编程 Socket 抽象数据结构设计。
 */

#ifndef CHAOS_COMMON_H
#define CHAOS_COMMON_H

/* 特性测试宏：开启 GNU/POSIX 扩展函数（如 strcasestr 等字符串查找函数） */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* 引入 C 语言标准输入输出、内存管理、字符串处理头文件 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

/* 引入 Linux/macOS POSIX 网络 Socket 编程核心头文件 */
#include <sys/types.h>
#include <sys/socket.h>  /* 提供 socket(), bind(), listen(), accept() 等底层函数 */
#include <sys/stat.h>    /* 提供 stat() 函数查询文件修改时间 */
#include <sys/time.h>    /* 提供 gettimeofday() 高精度时间函数 */
#include <netinet/in.h>  /* 提供 struct sockaddr_in, htons() 等网络地址结构与字节序转换 */
#include <arpa/inet.h>   /* 提供 inet_ntoa() IP 地址转换 */
#include <netdb.h>

/* 常量宏定义 */
#define DEFAULT_PORT        8888         /* 代理默认监听端口 */
#define DEFAULT_RULES_PATH  "rules.json" /* 默认规则配置文件 */
#define MAX_EVENTS          128          /* 允许处理的最大并发事件数 */
#define BUFFER_SIZE         16384        /* 读写缓冲区大小 (16KB) */
#define MAX_URL_LEN         1024         /* URL 最大长度 */
#define MAX_HEADER_LEN      8192         /* HTTP Header 最大长度 */

/**
 * @brief Socket 连接有限状态机 (Finite State Machine, FSM)
 * 
 * 💡 为什么要设计状态机？
 * 网络数据是通过 TCP 协议分块传输的，可能一次只读到了半个 HTTP 请求。
 * 我们不能阻塞程序去死等，必须记录“当前这个连接处理到哪一步了”。
 */
typedef enum {
    CONN_STATE_UNUSED = 0,         /* 状态 0：空闲闲置状态 */
    CONN_STATE_READ_CLIENT_REQ,    /* 状态 1：正在接收来自浏览器/客户端的请求数据 */
    CONN_STATE_PARSE_REQ,          /* 状态 2：请求读取完毕，正在解析 HTTP 报文与匹配规则 */
    CONN_STATE_CONNECT_UPSTREAM,   /* 状态 3：非阻塞建立到目标真实服务器的 TCP 连接 */
    CONN_STATE_WRITE_UPSTREAM,     /* 状态 4：将客户端请求转发给目标服务器 */
    CONN_STATE_READ_UPSTREAM,      /* 状态 5：正在读取目标服务器返回的响应 */
    CONN_STATE_INJECT_CHAOS,       /* 状态 6：读取完毕，正在执行故障注入 (篡改 Body/修改状态码) */
    CONN_STATE_DELAY_WAIT,         /* 状态 7：延迟注入中，正在等待非阻塞定时器到期 */
    CONN_STATE_WRITE_CLIENT,       /* 状态 8：正在将最终响应数据写回给浏览器/客户端 */
    CONN_STATE_CLOSED              /* 状态 9：处理完成，准备关闭并回收 Socket */
} conn_state_t;

/**
 * @brief 代表一个 TCP 双向代理连接的结构体
 * 
 * 包含：客户端 Socket、目标服务端 Socket、数据缓冲区和匹配到的规则
 */
typedef struct conn {
    int id;                        /* 连接唯一标识 ID */
    int client_fd;                 /* 客户端的 Socket 文件描述符 (如浏览器) */
    int upstream_fd;               /* 目标服务器的 Socket 文件描述符 */
    conn_state_t state;            /* 当前连接处于状态机的哪一步 */
    
    /* 客户端请求数据缓冲区 */
    char req_buf[BUFFER_SIZE];     /* 存放客户端发来的原始 HTTP 请求字符 */
    size_t req_len;                /* 当前已收到的请求数据总字节数 */
    size_t req_sent;               /* 已发送给目标的字节数 */
    
    /* 服务端响应数据缓冲区 */
    char resp_buf[BUFFER_SIZE];    /* 存放准备发回给客户端的响应字符 */
    size_t resp_len;               /* 响应数据的总字节数 */
    size_t resp_sent;              /* 已发回给客户端的字节数 */
    
    /* 从 HTTP 解析出的元数据 */
    char method[16];               /* 请求方法: "GET", "POST" 等 */
    char path[MAX_URL_LEN];        /* 请求路径: "/api/user/login" 等 */
    char host[256];                /* 目标主机名: "example.com" 等 */
    int port;                      /* 目标端口: 80 等 */
    
    /* 故障规则指针 */
    void *matched_rule;            /* 如果匹配到了故障规则，指向该 rule 结构体 */
    
    /* 非阻塞延时控制 */
    uint64_t send_after_ms;        /* 目标发回时间戳 (例如 当前毫秒 + 3000ms) */
    
    struct conn *next;             /* 链表指针：指向下一个连接结构体 */
} conn_t;

/**
 * @brief 辅助函数：获取当前系统时间的毫秒级时间戳 (MS)
 * @return 距离 1970 纪元以来的总毫秒数
 */
static inline uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL); // 获取微秒级时间
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

#endif /* CHAOS_COMMON_H */

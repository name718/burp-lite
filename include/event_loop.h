/**
 * @file event_loop.h
 * @brief 面向 Socket 的 Reactor 多路复用事件循环抽象
 *
 * 工程化实践要点：
 * 1. 屏蔽不同 OS 下选择 select/kqueue/epoll 的差异
 * 2. Socket 非阻塞 (O_NONBLOCK) 工具函数抽象
 */

#ifndef CHAOS_EVENT_LOOP_H
#define CHAOS_EVENT_LOOP_H

#include "common.h"

typedef struct event_loop event_loop_t;

/**
 * @brief 初始化事件循环 Reactor 实例
 */
event_loop_t *event_loop_create(int port);

/**
 * @brief 运行事件循环主监听逻辑
 */
void event_loop_run(event_loop_t *loop);

/**
 * @brief 销毁 Reactor 并释放资源
 */
void event_loop_destroy(event_loop_t *loop);

/**
 * @brief 设置 Socket 为非阻塞模式 (O_NONBLOCK)
 */
int set_nonblocking(int fd);

#endif /* CHAOS_EVENT_LOOP_H */

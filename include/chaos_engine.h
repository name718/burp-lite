/**
 * @file chaos_engine.h
 * @brief 故障注入逻辑 (延迟、状态码篡改、Body 篡改、断连)
 */

#ifndef CHAOS_ENGINE_H
#define CHAOS_ENGINE_H

#include "common.h"
#include "rule_engine.h"

/**
 * @brief 对准备返回给客户端的 HTTP 响应执行故障注入
 * @param conn 当前 Socket 连接抽象
 * @param rule 命中的故障注入规则
 * @return 是否需要连接断开 (true 表示 drop)
 */
bool apply_chaos_injection(conn_t *conn, chaos_rule_t *rule);

#endif /* CHAOS_ENGINE_H */

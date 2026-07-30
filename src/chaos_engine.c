/**
 * @file chaos_engine.c
 * @brief 故障注入引擎的核心算法实现 (含超详细中文学习注释)
 * 
 * 💡 学习要点：
 * 1. 状态码重写：HTTP 协议的第一行为状态行 (例如 "HTTP/1.1 200 OK\r\n")。重写即通过内存操作将其替换为 "HTTP/1.1 503 Service Unavailable\r\n"。
 * 2. 响应体篡改 (Body Modification)：在文本中扫描关键词，用 replacement 字符串替换，同步更新日志。
 * 3. 延时注入 (Latency)：非阻塞计算目标到期时刻，放入 Reactor 时间队列等待唤醒。
 * 4. 瞬间断连 (Connection Drop)：触发时直接关闭 Socket，向客户端注入网络异常或 RST。
 */

#include "chaos_engine.h"
#include "logger.h"

/**
 * @brief 对即将返回给客户端的 HTTP 响应数据执行故障注入
 * @param conn 当前 Socket 连接抽象指针
 * @param rule 匹配到的故障规则句柄
 * @return true 表示需要立即强制关闭 Socket, false 表示正常保持连接
 */
bool apply_chaos_injection(conn_t *conn, chaos_rule_t *rule) {
    if (!rule || !rule->enabled) return false;

    // 增加全局故障注入计数器
    g_metrics.injected_faults++;

    /* -----------------------------------------------------------------
     * 故障模式 1: 瞬间中断连接 (Connection Drop)
     * ----------------------------------------------------------------- */
    if (rule->close_immediately) {
        g_metrics.drop_injections++;
        log_chaos(rule->name, "CLOSE_IMMEDIATELY", "模拟网络闪断：立刻强制关闭 Client Socket [%d]", conn->client_fd);
        return true; // 返回 true，告知外层调用方直接 close(fd) 掉客户端连接
    }

    /* -----------------------------------------------------------------
     * 故障模式 2: 状态码改写 (HTTP Status Code Override)
     * ----------------------------------------------------------------- */
    if (rule->status_code > 0 && conn->resp_len > 0) {
        /* 在响应缓冲区中查找 HTTP 协议状态行 */
        char *status_line = strstr(conn->resp_buf, "HTTP/1.1 ");
        if (!status_line) status_line = strstr(conn->resp_buf, "HTTP/1.0 ");
        
        if (status_line) {
            char new_status[64];
            const char *reason = "Chaos Override";
            // 组装标准 HTTP 错误状态 Reason Phrase
            if (rule->status_code == 500) reason = "Internal Server Error";
            else if (rule->status_code == 503) reason = "Service Unavailable";
            else if (rule->status_code == 429) reason = "Too Many Requests";
            
            snprintf(new_status, sizeof(new_status), "HTTP/1.1 %d %s", rule->status_code, reason);
            
            // 找到第一行的换行符 \r\n，覆盖写入新状态行
            char *line_end = strstr(status_line, "\r\n");
            if (line_end) {
                size_t old_len = line_end - status_line;
                size_t new_len = strlen(new_status);
                if (new_len <= old_len) {
                    memcpy(status_line, new_status, new_len);
                    // 用空格填补多余字节
                    memset(status_line + new_len, ' ', old_len - new_len);
                }
            }
            g_metrics.status_injections++;
            log_chaos(rule->name, "STATUS_OVERRIDE", "修改 HTTP 状态码为 %d (%s)", rule->status_code, reason);
        }
    }

    /* -----------------------------------------------------------------
     * 故障模式 3: 响应体篡改 (Response Body Manipulation)
     * ----------------------------------------------------------------- */
    if (strlen(rule->find_str) > 0 && conn->resp_len > 0) {
        /* 从响应 Buffer 中搜索包含的 target 子串 */
        char *pos = strstr(conn->resp_buf, rule->find_str);
        if (pos) {
            size_t find_len = strlen(rule->find_str);
            size_t replace_len = strlen(rule->replace_str);
            
            // 替换目标字符
            if (find_len == replace_len) {
                memcpy(pos, rule->replace_str, replace_len);
                g_metrics.body_injections++;
                log_chaos(rule->name, "BODY_REPLACE", "篡改 Body 内容: 替换 '%s' -> '%s'", rule->find_str, rule->replace_str);
            }
        }
    }

    /* -----------------------------------------------------------------
     * 故障模式 4: 非阻塞延迟响应 (Latency Injection)
     * ----------------------------------------------------------------- */
    if (rule->delay_ms > 0) {
        /* 计算该连接允许向客户端发送数据的目标毫秒时间戳 */
        conn->send_after_ms = get_time_ms() + rule->delay_ms;
        /* 将状态切换为 CONN_STATE_DELAY_WAIT，让 Reactor 时间轮在时间未到之前不发送数据 */
        conn->state = CONN_STATE_DELAY_WAIT;
        g_metrics.delay_injections++;
        log_chaos(rule->name, "LATENCY", "延迟响应注入: 强行延后 %d ms 发送", rule->delay_ms);
    }

    return false; // 保持 Socket 连接正常
}

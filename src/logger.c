/**
 * @file logger.c
 * @brief 日志与运行统计监控模块实现 (含超详细中文注释)
 * 
 * 💡 学习要点：
 * 1. C 语言可变参数函数 (Variable Arguments)：使用 va_list, va_start, vprintf, va_end 解析像 printf 那样的变长参数 (...)。
 * 2. ANSI Escape Code (终端彩色控制字符)：例如 "\033[31m" 能在终端打印红色文字，"\033[0m" 重置颜色。
 * 3. C 语言系统时间格式化：time(), localtime(), strftime() 的使用。
 */

#include "logger.h"
#include <stdarg.h>

/* 全局统计度量实例 */
metrics_t g_metrics = {0};

/**
 * @brief 打印当前时间的标准格式字符串 [YYYY-MM-DD HH:MM:SS]
 */
static void print_time(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buf[26];
    strftime(buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    printf("[%s] ", buf);
}

/**
 * @brief 打印普通信息日志 [INFO] (青色)
 */
void log_info(const char *fmt, ...) {
    print_time();
    printf(COLOR_CYAN "[INFO] " COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * @brief 打印成功转发日志 [PASS] (绿色)
 */
void log_success(const char *fmt, ...) {
    print_time();
    printf(COLOR_GREEN "[PASS] " COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * @brief 打印警告日志 [WARN] (黄色)
 */
void log_warn(const char *fmt, ...) {
    print_time();
    printf(COLOR_YELLOW "[WARN] " COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * @brief 打印错误日志 [ERR] (红色)
 */
void log_error(const char *fmt, ...) {
    print_time();
    printf(COLOR_RED "[ERR]  " COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * @brief 打印故障注入专项日志 [CHAOS] (紫色加粗)
 */
void log_chaos(const char *rule_name, const char *action, const char *fmt, ...) {
    print_time();
    printf(COLOR_PURPLE COLOR_BOLD "[CHAOS] " COLOR_RESET COLOR_YELLOW "(规则: %s, 动作: %s) " COLOR_RESET, 
           rule_name, action);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * @brief 程序退出时输出汇总统计表格
 */
void print_metrics_summary(void) {
    printf("\n");
    printf(COLOR_CYAN COLOR_BOLD "==================================================\n" COLOR_RESET);
    printf(COLOR_CYAN COLOR_BOLD "              Chaos-Proxy 运行统计摘要            \n" COLOR_RESET);
    printf(COLOR_CYAN COLOR_BOLD "==================================================\n" COLOR_RESET);
    printf(" 总请求数 (Total):         %llu\n", (unsigned long long)g_metrics.total_requests);
    printf(" 正常转发 (Forwarded):     " COLOR_GREEN "%llu\n" COLOR_RESET, (unsigned long long)g_metrics.forwarded_requests);
    printf(" 故障注入 (Injected):      " COLOR_YELLOW "%llu\n" COLOR_RESET, (unsigned long long)g_metrics.injected_faults);
    printf("   - 延迟注入 (Delay):      %llu\n", (unsigned long long)g_metrics.delay_injections);
    printf("   - 状态码重写 (Status):   %llu\n", (unsigned long long)g_metrics.status_injections);
    printf("   - Body 篡改 (Modify):   %llu\n", (unsigned long long)g_metrics.body_injections);
    printf("   - 连接中断 (Drop):       %llu\n", (unsigned long long)g_metrics.drop_injections);
    printf(COLOR_CYAN COLOR_BOLD "==================================================\n\n" COLOR_RESET);
}

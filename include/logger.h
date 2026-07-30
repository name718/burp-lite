/**
 * @file logger.h
 * @brief 终端彩色日志打印与运行统计计数器
 */

#ifndef CHAOS_LOGGER_H
#define CHAOS_LOGGER_H

#include "common.h"

// ANSI Color Code
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_PURPLE  "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

typedef struct {
    uint64_t total_requests;
    uint64_t forwarded_requests;
    uint64_t injected_faults;
    uint64_t delay_injections;
    uint64_t status_injections;
    uint64_t body_injections;
    uint64_t drop_injections;
} metrics_t;

extern metrics_t g_metrics;

void log_info(const char *fmt, ...);
void log_success(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_chaos(const char *rule_name, const char *action, const char *fmt, ...);

void print_metrics_summary(void);

#endif /* CHAOS_LOGGER_H */

/**
 * @file rule_engine.h
 * @brief 规则加载、匹配与文件热重载 (Hot Reload) 引擎
 */

#ifndef CHAOS_RULE_ENGINE_H
#define CHAOS_RULE_ENGINE_H

#include "common.h"

typedef struct chaos_rule {
    char name[128];
    bool enabled;
    
    // 匹配准则
    char match_url[MAX_URL_LEN];
    char match_method[16];
    
    // 注入动作
    int delay_ms;
    int status_code;
    char find_str[256];
    char replace_str[512];
    bool close_immediately;
    
    struct chaos_rule *next;
} chaos_rule_t;

/**
 * @brief 初始化规则引擎并加载 json
 */
int rule_engine_init(const char *config_file);

/**
 * @brief 在每次 EventLoop 间隙中检查 rules.json 文件更新
 */
void rule_engine_check_reload(void);

/**
 * @brief 给定连接特征匹配对应的故障规则
 * @return 匹配到的规则指针或 NULL
 */
chaos_rule_t *rule_engine_match(const char *url, const char *method);

/**
 * @brief 清理规则引擎资源
 */
void rule_engine_cleanup(void);

#endif /* CHAOS_RULE_ENGINE_H */

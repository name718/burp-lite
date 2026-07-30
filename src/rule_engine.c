/**
 * @file rule_engine.c
 * @brief 规则加载、匹配算法与 rules.json 热重载 (Hot Reload) 实现 (含超详细中文注释)
 * 
 * 💡 学习要点：
 * 1. C 语言单向链表 (Single Linked List) 的动态创建与遍历释放。
 * 2. 什么是文件热重载 (Hot Reload)？
 *    程序不需要重启，通过 POSIX 系统调用 stat() 读取 rules.json 文件的 st_mtime (修改时间戳)。
 *    只要检测到用户保存修改了文件，立刻重新解析并无缝更新内存规则！
 * 3. 极简 JSON 解析组件 cJSON 的树状节点读取。
 */

#include "rule_engine.h"
#include "cJSON.h"
#include "logger.h"

static chaos_rule_t *g_rules_head = NULL; /* 全局规则单向链表头节点指针 */
static char g_config_path[512] = {0};     /* 配置文件磁盘绝对/相对路径 */
static time_t g_last_mtime = 0;           /* 上一次读取规则文件时的修改时间戳 */

/**
 * @brief 释放规则单向链表占用的全部堆内存
 */
static void free_rule_list(chaos_rule_t *head) {
    while (head) {
        chaos_rule_t *tmp = head;
        head = head->next; // 暂存下一个节点指针
        free(tmp);         // 释放当前节点
    }
}

/**
 * @brief 解析 rules.json 文本内容，构建并返回一个 C 语言规则结构体链表
 */
static chaos_rule_t *parse_json_rules(const char *content) {
    /* 调用 cJSON 树状解析函数 */
    cJSON *json = cJSON_Parse(content);
    if (!json) {
        log_error("JSON 语法解析失败，请检查 rules.json 格式是否正确！");
        return NULL;
    }

    /* 校验 rules.json 顶层必须是一个 JSON 数组 [] */
    if (!cJSON_IsArray(json)) {
        log_error("rules.json 根节点必须是数组格式 []！");
        cJSON_Delete(json);
        return NULL;
    }

    chaos_rule_t *head = NULL, *tail = NULL;
    int count = cJSON_GetArraySize(json); // 获取规则数组的元素个数

    /* 遍历数组中的每一条故障注入规则 */
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(json, i);
        if (!item) continue;

        /* 在堆内存中分配一个新的规则结构体对象 */
        chaos_rule_t *rule = (chaos_rule_t*)calloc(1, sizeof(chaos_rule_t));
        if (!rule) continue;

        /* 读取规则的基础属性: name, enabled, match, inject */
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *enabled = cJSON_GetObjectItem(item, "enabled");
        cJSON *match = cJSON_GetObjectItem(item, "match");
        cJSON *inject = cJSON_GetObjectItem(item, "inject");

        if (name && cJSON_GetStringValue(name)) {
            strncpy(rule->name, cJSON_GetStringValue(name), sizeof(rule->name) - 1);
        }
        rule->enabled = enabled ? cJSON_IsTrue(enabled) : true;

        /* 解析 匹配准则 (match: url, method) */
        if (match) {
            cJSON *url = cJSON_GetObjectItem(match, "url");
            cJSON *method = cJSON_GetObjectItem(match, "method");
            if (url && cJSON_GetStringValue(url)) strncpy(rule->match_url, cJSON_GetStringValue(url), sizeof(rule->match_url) - 1);
            if (method && cJSON_GetStringValue(method)) strncpy(rule->match_method, cJSON_GetStringValue(method), sizeof(rule->match_method) - 1);
        }

        /* 解析 注入故障 (inject: delay_ms, status_code, action, body_modify) */
        if (inject) {
            cJSON *delay = cJSON_GetObjectItem(inject, "delay_ms");
            cJSON *code = cJSON_GetObjectItem(inject, "status_code");
            cJSON *action = cJSON_GetObjectItem(inject, "action");
            cJSON *body = cJSON_GetObjectItem(inject, "body_modify");

            if (delay) rule->delay_ms = (int)cJSON_GetNumberValue(delay);
            if (code) rule->status_code = (int)cJSON_GetNumberValue(code);
            if (action && cJSON_GetStringValue(action)) {
                if (strcmp(cJSON_GetStringValue(action), "close_immediately") == 0) {
                    rule->close_immediately = true; // 标记断开连接
                }
            }
            if (body) {
                cJSON *find = cJSON_GetObjectItem(body, "find");
                cJSON *replace = cJSON_GetObjectItem(body, "replace");
                if (find && cJSON_GetStringValue(find)) strncpy(rule->find_str, cJSON_GetStringValue(find), sizeof(rule->find_str) - 1);
                if (replace && cJSON_GetStringValue(replace)) strncpy(rule->replace_str, cJSON_GetStringValue(replace), sizeof(rule->replace_str) - 1);
            }
        }

        /* 采用尾插法将节点拼接到单向链表尾部 */
        if (!head) {
            head = tail = rule;
        } else {
            tail->next = rule;
            tail = rule;
        }
    }

    cJSON_Delete(json); // 释放 cJSON 树产生的临时解析内存
    return head;
}

/**
 * @brief 从磁盘读取 rules.json 文件并调用解析
 */
static void load_rules_from_file(void) {
    FILE *fp = fopen(g_config_path, "rb");
    if (!fp) {
        log_warn("配置文件 [%s] 不存在，服务将以后备空规则模式运行。", g_config_path);
        return;
    }

    /* 计算文件总大小 (字节数) */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return;
    }

    /* 在堆上分配内存读取文件 */
    char *buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(fp);
        return;
    }

    fread(buf, 1, size, fp);
    buf[size] = '\0';
    fclose(fp);

    /* 解析 JSON 字符串 */
    chaos_rule_t *new_rules = parse_json_rules(buf);
    free(buf);

    if (new_rules) {
        /* 旧链表安全释放，原子替换为最新的规则链表 */
        free_rule_list(g_rules_head);
        g_rules_head = new_rules;
        log_success("成功载入规则配置文件: %s", g_config_path);
    }
}

/**
 * @brief 初始化规则引擎
 */
int rule_engine_init(const char *config_file) {
    strncpy(g_config_path, config_file, sizeof(g_config_path) - 1);
    
    // 记录规则文件当前的修改时间戳
    struct stat st;
    if (stat(g_config_path, &st) == 0) {
        g_last_mtime = st.st_mtime;
    }
    
    load_rules_from_file();
    return 0;
}

/**
 * @brief 检查 rules.json 文件是否被外部改动 (无缝热重载机制)
 */
void rule_engine_check_reload(void) {
    struct stat st;
    /* 调用 stat() 轮询文件的最新修改时间 */
    if (stat(g_config_path, &st) == 0) {
        if (st.st_mtime != g_last_mtime) {
            g_last_mtime = st.st_mtime;
            log_info("检测到 rules.json 文件被重新编辑保存，触发规则热重载 (Hot Reload)...");
            load_rules_from_file();
        }
    }
}

/**
 * @brief 在内存规则链表中查找是否有匹配当前请求 (url & method) 的故障规则
 */
chaos_rule_t *rule_engine_match(const char *url, const char *method) {
    chaos_rule_t *curr = g_rules_head;
    while (curr) {
        if (curr->enabled) {
            // URL 匹配：匹配前缀或包含该 URL 子串
            bool url_match = (strlen(curr->match_url) == 0) || (strstr(url, curr->match_url) != NULL);
            // Method 匹配：匹配 GET, POST 或任意方法
            bool method_match = (strlen(curr->match_method) == 0) || (strcasecmp(method, curr->match_method) == 0);

            if (url_match && method_match) {
                return curr; // 命中！返回规则句柄
            }
        }
        curr = curr->next;
    }
    return NULL; // 未命中任何规则
}

/**
 * @brief 清理规则引擎资源
 */
void rule_engine_cleanup(void) {
    free_rule_list(g_rules_head);
    g_rules_head = NULL;
}

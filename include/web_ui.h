/**
 * @file web_ui.h
 * @brief 内嵌 Web 可视化 UI 界面与 RESTful 管理 API
 */

#ifndef CHAOS_WEB_UI_H
#define CHAOS_WEB_UI_H

#include "common.h"

/**
 * @brief 检查请求是否为 Web UI 页面或 REST API
 * @return true 表示已被 Web UI 拦截处理, false 表示正常代理请求
 */
bool handle_web_ui_request(conn_t *conn);

#endif /* CHAOS_WEB_UI_H */

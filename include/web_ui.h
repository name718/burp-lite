/**
 * @file web_ui.h
 * @brief 内嵌 Web 可视化 UI 界面与 RESTful 管理 API
 */

#ifndef CHAOS_WEB_UI_H
#define CHAOS_WEB_UI_H

#include "common.h"

/**
 * @brief 尝试处理当前连接是否为对 Web UI 或 API 的请求
 * 
 * @param conn 当前连接对象
 * @param listen_port 代理服务器的监听端口
 * @return true 表示已被 Web UI 拦截处理完毕，不再走后续代理转发流程
 * @return false 表示非 Web UI 请求，需继续走代理转发逻辑
 */
bool handle_web_ui_request(conn_t *conn, int listen_port);

#endif /* CHAOS_WEB_UI_H */

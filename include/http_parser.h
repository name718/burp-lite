/**
 * @file http_parser.h
 * @brief HTTP 报文解析与 Accept-Encoding 过滤器
 */

#ifndef CHAOS_HTTP_PARSER_H
#define CHAOS_HTTP_PARSER_H

#include "common.h"

/**
 * @brief 解析 HTTP 请求报文提取 Method, Path, Host, Port
 * @return 0 成功, -1 报文不完整或格式不正确
 */
int parse_http_request(conn_t *conn);

/**
 * @brief 从请求头中剥离/替换 Accept-Encoding (防止 gzip)
 * 强制后端返回 plaintext 响应
 */
void strip_accept_encoding(char *buf, size_t *len);

#endif /* CHAOS_HTTP_PARSER_H */

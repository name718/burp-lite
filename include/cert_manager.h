#ifndef CERT_MANAGER_H
#define CERT_MANAGER_H

#include <openssl/ssl.h>
#include <openssl/err.h>

void cert_manager_init(void);
void cert_manager_cleanup(void);

/* 为特定域名获取（或生成）一个服务端 SSL_CTX (用于与浏览器握手) */
SSL_CTX *get_server_ssl_ctx(const char *domain);

/* 获取一个通用的客户端 SSL_CTX (用于与上游服务器握手) */
SSL_CTX *get_client_ssl_ctx(void);

#endif // CERT_MANAGER_H

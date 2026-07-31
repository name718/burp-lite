#include "cert_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static SSL_CTX *g_client_ctx = NULL;

void cert_manager_init(void) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    /* 创建 CA 目录和脚本 */
    mkdir("/tmp/chaos_ca", 0777);
    mkdir("/tmp/chaos_ca/certs", 0777);
    
    /* 检查是否已有 CA 根证书，没有则生成 */
    if (access("/tmp/chaos_ca/ca.crt", F_OK) != 0) {
        log_info("生成新的 Root CA 根证书...");
        system("openssl genrsa -out /tmp/chaos_ca/ca.key 2048 > /dev/null 2>&1");
        system("openssl req -x509 -new -nodes -key /tmp/chaos_ca/ca.key -sha256 -days 3650 -out /tmp/chaos_ca/ca.crt -subj '/C=CN/ST=Beijing/L=Beijing/O=ChaosProxy/CN=ChaosProxy Root CA' > /dev/null 2>&1");
        log_success("Root CA 根证书生成成功: /tmp/chaos_ca/ca.crt (请在系统中安装并信任它！)");
    }

    /* 初始化通用的 Client SSL_CTX */
    const SSL_METHOD *client_method = TLS_client_method();
    g_client_ctx = SSL_CTX_new(client_method);
    if (!g_client_ctx) {
        log_error("无法创建 Client SSL_CTX");
    }
}

void cert_manager_cleanup(void) {
    if (g_client_ctx) {
        SSL_CTX_free(g_client_ctx);
        g_client_ctx = NULL;
    }
    EVP_cleanup();
}

SSL_CTX *get_client_ssl_ctx(void) {
    return g_client_ctx;
}

SSL_CTX *get_server_ssl_ctx(const char *domain) {
    char cert_path[256];
    char key_path[256];
    
    snprintf(cert_path, sizeof(cert_path), "/tmp/chaos_ca/certs/%s.crt", domain);
    snprintf(key_path, sizeof(key_path), "/tmp/chaos_ca/certs/%s.key", domain);

    /* 检查证书是否已生成 */
    if (access(cert_path, F_OK) != 0) {
        log_info("为域名 %s 动态伪造 TLS 证书...", domain);
        char cmd[1024];
        /* 使用 shell 快速生成域名证书并用 CA 签名 */
        snprintf(cmd, sizeof(cmd), 
            "openssl genrsa -out %s 2048 > /dev/null 2>&1 && "
            "openssl req -new -key %s -out /tmp/chaos_ca/certs/%s.csr -subj '/C=CN/ST=Beijing/L=Beijing/O=ChaosProxy/CN=%s' > /dev/null 2>&1 && "
            "echo \"subjectAltName=DNS:%s,DNS:*.%s\" > /tmp/chaos_ca/certs/%s.ext && "
            "openssl x509 -req -in /tmp/chaos_ca/certs/%s.csr -CA /tmp/chaos_ca/ca.crt -CAkey /tmp/chaos_ca/ca.key -CAcreateserial -extfile /tmp/chaos_ca/certs/%s.ext -out %s -days 365 -sha256 > /dev/null 2>&1",
            key_path, key_path, domain, domain, domain, domain, domain, domain, domain, cert_path);
        
        system(cmd);
    }

    const SSL_METHOD *server_method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(server_method);
    if (!ctx) return NULL;

    if (SSL_CTX_use_certificate_chain_file(ctx, cert_path) <= 0) {
        log_error("加载证书失败: %s", cert_path);
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        log_error("加载私钥失败: %s", key_path);
        SSL_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

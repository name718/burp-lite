/**
 * @file main.c
 * @brief Chaos-Proxy 程序入口 (命令行参数解析、信号捕获与优雅退出)
 * 
 * 💡 学习要点：
 * 1. 命令行参数解析：getopt() 函数的用法（如何解析 -p 端口、-c 配置文件）。
 * 2. Linux/macOS 信号处理：通过 sigaction 捕获 Ctrl+C (SIGINT)，实现程序“优雅退出”而非崩溃。
 * 3. 资源清理：退出前关闭所有 Socket、释放内存、打印运行统计摘要。
 */

#include "common.h"
#include "event_loop.h"
#include "rule_engine.h"
#include "logger.h"

/* 全局 Reactor 事件循环指针，方便信号回调函数访问 */
static event_loop_t *g_loop = NULL;

/**
 * @brief 打印 CLI 命令行帮助选项
 */
static void print_usage(const char *prog_name) {
    printf("==================================================\n");
    printf("  Chaos-Proxy - 本地 HTTP 代理与故障注入工具 (V1.0)\n");
    printf("==================================================\n");
    printf("用法: %s [-p 端口] [-c 配置文件路径] [-h]\n", prog_name);
    printf("选项:\n");
    printf("  -p <port>        指定代理监听端口 (默认: %d)\n", DEFAULT_PORT);
    printf("  -c <config_file> 指定 rules.json 规则路径 (默认: %s)\n", DEFAULT_RULES_PATH);
    printf("  -h               显示帮助信息\n\n");
}

/**
 * @brief 操作系统信号回调函数 (Signal Handler)
 * 当用户在终端按下 Ctrl+C 时，操作系统会向本进程发送 SIGINT 信号。
 * 我们捕获这个信号，执行“优雅退出”逻辑。
 */
static void handle_signal(int sig) {
    (void)sig; // 避免未使用参数的 Warning
    printf("\n");
    log_warn("接收到终止信号 (Ctrl+C / SIGINT)，准备优雅退出 (Graceful Shutdown)...");
    
    /* 1. 销毁 Reactor 事件循环，关闭所有未完成的 Socket 连接 */
    if (g_loop) {
        event_loop_destroy(g_loop);
        g_loop = NULL;
    }
    
    /* 2. 清理规则引擎分配的内存 */
    rule_engine_cleanup();
    
    /* 3. 打印最终的请求与故障注入统计报表 */
    print_metrics_summary();
    
    /* 4. 正常退出进程，返回状态码 0 */
    exit(0);
}

/**
 * @brief C 语言主函数入口
 */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;                       // 监听端口，默认 8888
    char config_path[512] = DEFAULT_RULES_PATH;   // 配置文件路径，默认 rules.json

    /* 使用 POSIX C 标准函数 getopt() 解析命令行输入的参数 (-p, -c, -h) */
    int opt;
    while ((opt = getopt(argc, argv, "p:c:h")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg); // 将输入的端口字符串转为整数
                if (port <= 0 || port > 65535) {
                    log_error("端口号非法: %s (必须在 1-65535 之间)", optarg);
                    return 1;
                }
                break;
            case 'c':
                strncpy(config_path, optarg, sizeof(config_path) - 1);
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    /* 注册 Linux/macOS 信号监听 (捕获 Ctrl+C SIGINT 以及 kill SIGTERM) */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal; // 设置收到信号时执行 handle_signal 函数
    sigaction(SIGINT, &sa, NULL);  // 捕获 Ctrl+C
    sigaction(SIGTERM, &sa, NULL); // 捕获 kill 命令

    log_info("启动 Chaos-Proxy 代理服务...");
    log_success("🌐 Web UI 控制台可视化界面已在 http://127.0.0.1:%d/ui 开启", port);

    /* 步骤 1: 初始化故障注入规则引擎 (读取并解析 rules.json) */
    if (rule_engine_init(config_path) != 0) {
        log_error("规则引擎初始化失败!");
        return 1;
    }

    /* 步骤 2: 创建 Reactor 事件循环 (绑定 Socket 端口并开启监听) */
    g_loop = event_loop_create(port);
    if (!g_loop) {
        log_error("创建 Event Loop 监听实例失败!");
        rule_engine_cleanup();
        return 1;
    }

    /* 步骤 3: 进入无限 Reactor 事件循环，持续处理网络连接与故障注入 */
    event_loop_run(g_loop);

    /* 步骤 4: 循环结束后的资源回收 */
    event_loop_destroy(g_loop);
    rule_engine_cleanup();
    print_metrics_summary();

    return 0;
}

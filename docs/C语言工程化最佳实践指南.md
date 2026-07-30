# C 语言 Socket 开发与工程管理最佳实践指南

**工程名称**：Chaos-Proxy  
**工程目录**：[chaos-proxy](file:///Users/didi/Desktop/my-project/chaos-proxy)  
**适合场景**：学习工业级 C 语言模块划分、面向 Socket 的 Reactor 高性能网络开发、Makefile 自动构建管理。

---

## 1. 工业级 C 项目工程目录组织规范

在标准的工业级 C 语言工程中，源码、头文件、构建产物必须严格隔离，遵循以下设计规范：

```text
chaos-proxy/
├── Makefile               # 自动化构建管理 (支持自动依赖生成 -MMD -MP)
├── .gitignore             # Git 忽略文件 (忽略 build/ 与中间 .o .d 文件)
├── rules.json             # 示例配置文件
├── include/               # 【头文件目录】对外声明接口，禁止放置具体逻辑实现
│   ├── common.h           # 通用宏、结构体定义、Socket 标准库包含
│   ├── event_loop.h       # Socket 多路复用 (Reactor) 抽象定义
│   ├── http_parser.h      # HTTP 协议解析与 Header 操纵
│   ├── rule_engine.h      # 规则链表管理与文件热重载接口
│   ├── chaos_engine.h     # 故障注入核心算法接口
│   ├── cJSON.h            # 轻量自包含 JSON 库
│   └── logger.h           # ANSI 彩色终端日志与运行计数器
├── src/                   # 【源码目录】模块具体实现，一个头文件对应一个源码文件
│   ├── main.c             # CLI 参数解析 (getopt) 与优雅退出信号处理
│   ├── event_loop.c       # 非阻塞 Socket 监听与多路复用驱动
│   ├── http_parser.c      # HTTP 状态机解析与 Accept-Encoding 清洗
│   ├── rule_engine.c      # JSON 规则检索算法与 stat() 变更监控
│   ├── chaos_engine.c     # 状态码重写、Body 替换、非阻塞延时实现
│   ├── cJSON.c            # cJSON 实现
│   └── logger.c           # 格式化终端日志与 Metrics 统计
└── build/                 # 【构建目录】编译产生的临时文件，与源码彻底隔离
    ├── obj/               # 存放 .o (Object) 与 .d (Dependency) 文件
    └── bin/               # 存放最终编译生成的可执行二进制 chaos-proxy
```

### 核心工程原则：
1. **单一职责原则**：一个 `.c` 文件只负责一类功能，并且对应一个同名的 `.h` 文件。
2. **头文件保护**：每个 `.h` 头文件必须包含 `#ifndef XXX_H` ... `#define XXX_H` 防范重复包含。
3. **隔离构建产物**：中间目标文件 (`.o`) 和二进制输出 (`.bin`) 放置在 `build/` 目录下，保证 `git status` 洁净。

---

## 2. 自动化构建与依赖管理 (Makefile 最佳实践)

很多 C 语言初学者在修改 `.h` 文件后，因为 Makefile 规则未写好，导致 `make` 不会重新编译依赖该头文件的 `.c` 源码。

本工程的 [Makefile](file:///Users/didi/Desktop/my-project/chaos-proxy/Makefile) 采用了**自动依赖生成算法 (`-MMD -MP`)**：

```makefile
CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -Werror -std=c99 -Iinclude -O2 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
LDFLAGS  += -lm

# 自动生成 .o 文件和 .d 依赖描述文件
$(OBJ_DIR)/%.c.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo " [CC] $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# 自动引入 .d 依赖文件
-include $(DEPS)
```

- **`-MMD`**：编译器会自动生成每个 `.c` 文件的头文件依赖文件 `.d`。
- **`-MP`**：即使某个头文件被删除，构建系统也不会报 `No rule to make target` 错误。
- **`-Werror`**：将所有编译器 Warning 视为 Error，确保提交的代码 0 警告，极大地提升了代码质量。

---

## 3. 面向 Socket 的高性能网络开发最佳实践

### 3.1 非阻塞 I/O (Non-blocking Socket)

在高性能代理服务中，**绝不允许使用阻塞式 Socket**。任何一个慢连接或网络延迟都会挂起整个进程。

```c
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

### 3.2 抽象 Socket 连接状态机 (FSM)

网络 Socket 传输是分块、渐进式的。使用有限状态机 (Finite State Machine) 跟踪每个连接的生命周期：

```c
typedef enum {
    CONN_STATE_READ_CLIENT_REQ,    // 正在接收客户端请求 Header/Body
    CONN_STATE_PARSE_REQ,          // 解析请求 & 匹配规则
    CONN_STATE_READ_UPSTREAM,      // 接收目标服务器响应
    CONN_STATE_INJECT_CHAOS,       // 执行故障注入
    CONN_STATE_DELAY_WAIT,         // 非阻塞 Timer 等待中
    CONN_STATE_WRITE_CLIENT,       // 响应发送回客户端
    CONN_STATE_CLOSED              // 连接待回收销毁
} conn_state_t;
```

### 3.3 非阻塞定时器实现 (禁止 sleep)

为了在单线程中实现接口延迟 3000ms 注入，**绝对不能在 Socket 线程中调用 `sleep(3)`**。

**正确实践**：
1. 标记连接到期时间戳：`conn->send_after_ms = get_time_ms() + rule->delay_ms`。
2. 将连接状态切换为 `CONN_STATE_DELAY_WAIT`。
3. 在 Event Loop 的每次轮询中对比当前时间：若 `now >= conn->send_after_ms`，则将其重新挂载到 `write_fds` 集合中发送响应。

---

## 4. C 语言内存安全与工程习惯

1. **优先使用静态/固定缓冲区**：如 `conn_t` 中分配固定 16KB `req_buf` 与 `resp_buf`，避免频繁调用 `malloc`/`free` 导致内存碎片化。
2. **安全的字符串拷贝**：禁止使用危险的 `strcpy`，统一使用 `strncpy(dest, src, sizeof(dest) - 1)` 防止缓冲区溢出 (Buffer Overflow)。
3. **POSIX 信号捕捉与 Graceful Shutdown**：
   在 [src/main.c](file:///Users/didi/Desktop/my-project/chaos-proxy/src/main.c) 中捕获 `SIGINT` (Ctrl+C)，退出时优雅清理已打开的 Socket 文件描述符并输出汇总度量，避免内核端口残留占满 `TIME_WAIT`。

---

# Chaos Proxy: Burp Lite & HTTPS MITM 架构重构计划 (Plan)

## 🎯 核心目标
将现有的轻量级 HTTP 代理引擎升级为具备 **Burp Suite** 核心能力的抓包与拦截利器，包括：
1. **完整报文查看**：支持在 Electron UI 查看完整的 Request / Response (Headers + Body)。
2. **请求/响应拦截与篡改**：支持在报文发往上游或返回浏览器前将其挂起，供用户在 UI 中手动修改后放行。
3. **HTTPS 流量解密 (MITM)**：通过自签发根证书，动态伪造证书，实现对 HTTPS 加密流量的明文解密与篡改。

---

## 🏗 架构设计方案

### 1. 通信架构大换血 (IPC 升级)
目前的 C 代理与 Electron 之间仅靠简单的 `stdout` 传输单行日志，无法承载庞大的 HTTP Body。
**重构方案**：基于本地文件的异步 IPC 通信
- **C 代理端**：将完整的请求和响应流临时写入到磁盘缓存目录（如 `~/.chaos_proxy/logs/`）。
- **信号传递**：C 代理通过 stdout 仅发送信号，如 `REQ_READY 1001`。
- **Electron UI 端**：收到信号后，直接从本地读取对应的 `1001_req.bin` 文件进行展示。此方案极为稳定，完美支持二进制文件且杜绝了 IPC 内存溢出。

### 2. 报文拦截流转机制 (Intercept State Machine)
在 C 语言的 `event_loop` 状态机中引入新的挂起状态：
- 增加 `CONN_STATE_INTERCEPT_REQ` 和 `CONN_STATE_INTERCEPT_RESP`。
- 命中规则后，C 代理将报文落盘并暂停该 socket 的读写事件，等待外部指令。
- Electron UI 提供 "Forward" 按钮，用户点击后将修改后的报文覆盖回本地文件。
- Electron 向 C 代理暴露的本地 HTTP 接口（`web_ui.c`）发送 `POST /api/resume?id=1001` 指令，C 代理重新加载文件并继续转发。

### 3. HTTPS 中间人解密引擎 (OpenSSL MITM)
这是本次重构的深水区，将彻底改变现有的透传机制。
- **依赖引入**：C 项目链接 `OpenSSL` (libssl, libcrypto)。
- **根证书管理**：代理首次启动时，生成一对全局 `Root CA` 证书并提示用户安装信任。
- **动态证书签发 (On-the-fly Forgery)**：
  当收到 `CONNECT api.xxx.com:443` 时，C 代理在内存中动态生成一张被 Root CA 签名的 `api.xxx.com` 证书。
- **双向 TLS 握手**：
  C 代理作为 Server 与浏览器调用 `SSL_accept` 完成 TLS 握手。
  C 代理作为 Client 与上游服务器调用 `SSL_connect` 完成 TLS 握手。
- **透明加解密**：状态机中的 `read/write` 全部替换封装为 `SSL_read / SSL_write`，代理内部即可像处理普通 HTTP 一样处理 HTTPS 的明文流量。

---

## 📅 分步执行路线图 (Roadmap)

### 第一阶段：文件 IPC 与全量报文展示 (HTTP)
1. **[C 侧]** 增强 `event_loop.c`，在请求和响应的生命周期中加入落盘逻辑，保存 Header 和 Body。
2. **[C 侧]** 更新 stdout 日志格式，加入连接的唯一 ID（如 UUID 或自增 ID）。
3. **[Node/Vue 侧]** 改造 `App.vue`，点击历史记录时，根据 ID 读取本地磁盘对应的 `.bin` 文件并渲染在界面的 "Raw" 面板中。

### 第二阶段：请求与响应的拦截断点 (HTTP)
1. **[C 侧]** 扩展 `web_ui.c`，新增 `/api/resume` 接口，用于唤醒被挂起的连接。
2. **[C 侧]** 修改 `event_loop.c` 状态机，匹配到拦截规则时进入挂起态。
3. **[Node/Vue 侧]** 实现 Interceptor 面板交互：展示挂起的报文，支持文本编辑，实现 Forward 放行接口调用。

### 第三阶段：HTTPS 中间人攻击引擎接入 (MITM)
1. **[构建]** 修改 Makefile，链接 OpenSSL 库。
2. **[C 侧: 证书生成]** 编写 `cert_manager.c`，利用 OpenSSL 接口实现 Root CA 生成和按域名的动态证书签发。
3. **[C 侧: TLS 握手]** 改造 `event_loop.c` 对 `CONNECT` 方法的处理，摒弃旧的盲目透传，介入双向 TLS 握手。
4. **[C 侧: 统一 I/O]** 封装底层 IO 操作，使 HTTP 和 HTTPS 可以共用同一套拦截与篡改逻辑。
5. **[文档]** 编写引导文档，协助用户在 macOS/Windows 上信任 Root CA。

---
*注：该重构将极大增强代理能力，但也伴随较高的 C 语言内存管理及密码学工程挑战。按阶段渐进式落地将是保障稳定性的关键。*

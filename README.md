# Burp Lite (formerly Chaos Proxy)

Burp Lite is a high-performance, lightweight HTTP/HTTPS proxy and interception tool written in pure C for the proxy engine and Vue.js/Electron for the desktop UI. It brings Burp Suite-like capabilities into a modern, lightning-fast developer experience.

## ✨ Features

- **Blazing Fast C Core**: The core proxy is written in C using `select()` based event loops, capable of handling high concurrency with zero garbage collection pauses.
- **Full HTTPS MITM Support**: Automatically generates Root CA and dynamically signs domain certificates on-the-fly using OpenSSL, enabling transparent interception and decryption of HTTPS traffic.
- **Burp-like Repeater**: Edit raw HTTP requests and resend them instantly. Automatically recalculates `Content-Length`.
- **Live Interceptor**: Set rules to pause incoming or outgoing traffic, manually modify the HTTP body/headers in the UI, and forward them seamlessly.
- **Chaos Engineering**: Built-in latency injection, connection drops, status code overrides, and payload tampering to test the robustness of your microservices.
- **Modern Electron UI**: Chrome DevTools-inspired interface built with Vue 3, featuring split-pane views, dark mode, and real-time structured logs.

## 🚀 Quick Start

### 1. Build the Proxy Core

Ensure you have a C compiler, `make`, and OpenSSL installed (e.g., `brew install openssl@3` on macOS).

```bash
make
```

### 2. Start the Desktop App

Make sure Node.js is installed.

```bash
cd desktop
npm install
npm run dev
```

### 3. Trust the Root CA (For HTTPS Interception)

Once started, the proxy generates a Root CA in `/tmp/chaos_ca/ca.crt`. To intercept HTTPS traffic, you must trust this certificate on your system.

**macOS (UI Method):**
1. Press `Cmd + Shift + G` in Finder and go to `/tmp/chaos_ca/`.
2. Double-click `ca.crt` to open Keychain Access.
3. Find `ChaosProxy Root CA`, double-click it, expand **Trust**, and select **Always Trust**.

**macOS (CLI Method):**
```bash
security add-trusted-cert -p ssl -p basic -k ~/Library/Keychains/login.keychain-db /tmp/chaos_ca/ca.crt
```

## 🛠️ Architecture

- **`src/`**: The core C HTTP/HTTPS proxy engine. Uses POSIX sockets, non-blocking I/O, and OpenSSL.
- **`desktop/`**: The Electron wrapper and Vue 3 frontend. Communicates with the C proxy via efficient file-based IPC and a local REST API.
- **`docs/`**: Technical design documents and architectural plans.

## 📄 License

MIT License

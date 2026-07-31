"use strict";
const electron = require("electron");
const child_process = require("child_process");
const path = require("path");
const url = require("url");
const fs = require("fs");
const net = require("net");
var _documentCurrentScript = typeof document !== "undefined" ? document.currentScript : null;
const __dirname$1 = path.dirname(url.fileURLToPath(typeof document === "undefined" ? require("url").pathToFileURL(__filename).href : _documentCurrentScript && _documentCurrentScript.tagName.toUpperCase() === "SCRIPT" && _documentCurrentScript.src || new URL("main.js", document.baseURI).href));
let mainWindow = null;
let proxyProcess = null;
const isDev = process.env.VITE_DEV_SERVER_URL !== void 0;
function getBinaryPath() {
  if (isDev) {
    return path.join(__dirname$1, "../../build/bin/burp-lite");
  }
  return path.join(process.resourcesPath, "bin", "burp-lite");
}
function getRulesPath() {
  if (isDev) {
    return path.join(__dirname$1, "../../rules.json");
  }
  return path.join(process.resourcesPath, "rules.json");
}
function createWindow() {
  mainWindow = new electron.BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 1100,
    minHeight: 680,
    frame: false,
    backgroundColor: "#0a0d14",
    webPreferences: {
      preload: path.join(__dirname$1, "../dist-electron/preload.js"),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false
    }
  });
  if (isDev) {
    mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL);
  } else {
    mainWindow.loadFile(path.join(__dirname$1, "../dist/index.html"));
  }
  mainWindow.on("closed", () => {
    mainWindow = null;
  });
}
electron.ipcMain.on("window:minimize", () => mainWindow == null ? void 0 : mainWindow.minimize());
electron.ipcMain.on("window:maximize", () => {
  if (mainWindow == null ? void 0 : mainWindow.isMaximized()) mainWindow.unmaximize();
  else mainWindow == null ? void 0 : mainWindow.maximize();
});
electron.ipcMain.on("window:close", () => mainWindow == null ? void 0 : mainWindow.close());
electron.ipcMain.handle("proxy:start", (_event, { port = 8888 } = {}) => {
  if (proxyProcess) return { ok: false, msg: "Already running" };
  const bin = getBinaryPath();
  const rules = getRulesPath();
  if (!fs.existsSync(bin)) {
    return {
      ok: false,
      msg: `Binary not found at: ${bin}
Run "make" in the project root first.`
    };
  }
  try {
    proxyProcess = child_process.spawn(bin, ["-p", String(port), "-c", rules], {
      cwd: path.join(__dirname$1, "../..")
    });
    proxyProcess.stdout.on("data", (data) => {
      const lines = data.toString().split("\n").filter(Boolean);
      lines.forEach((line) => {
        mainWindow == null ? void 0 : mainWindow.webContents.send("log:line", line);
      });
    });
    proxyProcess.stderr.on("data", (data) => {
      const text = data.toString().trim();
      if (text) mainWindow == null ? void 0 : mainWindow.webContents.send("log:line", `[STDERR] ${text}`);
    });
    proxyProcess.on("close", (code) => {
      proxyProcess = null;
      mainWindow == null ? void 0 : mainWindow.webContents.send("proxy:stopped", { code });
    });
    proxyProcess.on("error", (err) => {
      proxyProcess = null;
      mainWindow == null ? void 0 : mainWindow.webContents.send("proxy:stopped", { code: -1, err: err.message });
    });
    return { ok: true, pid: proxyProcess.pid };
  } catch (err) {
    proxyProcess = null;
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("proxy:stop", () => {
  if (!proxyProcess) return { ok: false, msg: "Not running" };
  try {
    proxyProcess.kill("SIGTERM");
    proxyProcess = null;
    return { ok: true };
  } catch (err) {
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("proxy:status", () => ({
  running: !!proxyProcess,
  pid: (proxyProcess == null ? void 0 : proxyProcess.pid) ?? null
}));
electron.ipcMain.handle("rules:read", () => {
  try {
    const raw = fs.readFileSync(getRulesPath(), "utf-8");
    return { ok: true, rules: JSON.parse(raw) };
  } catch {
    return { ok: true, rules: [] };
  }
});
electron.ipcMain.handle("rules:write", (_event, rules) => {
  try {
    const clean = rules.map(({ _id, ...r }) => r);
    fs.writeFileSync(getRulesPath(), JSON.stringify(clean, null, 2), "utf-8");
    return { ok: true };
  } catch (err) {
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("system:setProxy", (_event, { host = "127.0.0.1", port = 8888 } = {}) => {
  try {
    const services = ["Wi-Fi", "Ethernet", "USB 10/100/1000 LAN"];
    for (const svc of services) {
      try {
        child_process.execSync(`networksetup -setwebproxy "${svc}" ${host} ${port} off 2>/dev/null`);
        child_process.execSync(`networksetup -setwebproxystate "${svc}" on 2>/dev/null`);
        child_process.execSync(`networksetup -setsecurewebproxy "${svc}" ${host} ${port} off 2>/dev/null`);
        child_process.execSync(`networksetup -setsecurewebproxystate "${svc}" on 2>/dev/null`);
      } catch {
      }
    }
    return { ok: true };
  } catch (err) {
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("system:clearProxy", () => {
  try {
    const services = ["Wi-Fi", "Ethernet", "USB 10/100/1000 LAN"];
    for (const svc of services) {
      try {
        child_process.execSync(`networksetup -setwebproxystate "${svc}" off 2>/dev/null`);
        child_process.execSync(`networksetup -setsecurewebproxystate "${svc}" off 2>/dev/null`);
      } catch {
      }
    }
    return { ok: true };
  } catch (err) {
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("system:downloadCert", async () => {
  try {
    const caPath = "/tmp/chaos_ca/ca.crt";
    if (!fs.existsSync(caPath)) {
      return { ok: false, msg: "Root CA certificate not found at /tmp/chaos_ca/ca.crt. Has the proxy started?" };
    }
    const defaultPath = path.join(electron.app.getPath("desktop"), "burp-lite-ca.crt");
    const { canceled, filePath } = await electron.dialog.showSaveDialog({
      title: "Save Root CA Certificate",
      defaultPath,
      filters: [{ name: "Certificates", extensions: ["crt", "cer", "pem"] }]
    });
    if (canceled || !filePath) {
      return { ok: false, msg: "Cancelled by user" };
    }
    fs.copyFileSync(caPath, filePath);
    return { ok: true, msg: "Certificate saved successfully to " + filePath };
  } catch (err) {
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("payload:read", (_event, id, type) => {
  try {
    const filePath = path.join("/tmp/chaos_logs", `${id}_${type}.bin`);
    if (!fs.existsSync(filePath)) {
      return { ok: false, msg: "File not found" };
    }
    const content = fs.readFileSync(filePath, "utf-8");
    return { ok: true, content };
  } catch (err) {
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("payload:write", (_event, id, type, content) => {
  try {
    const filePath = path.join("/tmp/chaos_logs", `${id}_${type}.bin`);
    fs.writeFileSync(filePath, content, "utf-8");
    return { ok: true };
  } catch (err) {
    return { ok: false, msg: err.message };
  }
});
electron.ipcMain.handle("request:sendRaw", (_event, port, rawRequest) => {
  return new Promise((resolve) => {
    const client = new net.Socket();
    let responseData = Buffer.alloc(0);
    let startTime = Date.now();
    let normalizedReq = rawRequest.replace(/\r\n/g, "\n").replace(/\n/g, "\r\n");
    if (!normalizedReq.includes("\r\n\r\n")) {
      if (normalizedReq.endsWith("\r\n")) {
        normalizedReq += "\r\n";
      } else {
        normalizedReq += "\r\n\r\n";
      }
    }
    let finalRequest = normalizedReq;
    const parts = normalizedReq.split("\r\n\r\n");
    if (parts.length >= 2) {
      const headStr = parts[0];
      const bodyStr = parts.slice(1).join("\r\n\r\n");
      if (/Content-Length:\s*\d+/i.test(headStr)) {
        const actualLen = Buffer.byteLength(bodyStr, "utf8");
        const newHead = headStr.replace(/Content-Length:\s*\d+/i, `Content-Length: ${actualLen}`);
        finalRequest = newHead + "\r\n\r\n" + bodyStr;
      }
    }
    client.connect(port, "127.0.0.1", () => {
      client.write(finalRequest);
    });
    client.on("data", (data) => {
      responseData = Buffer.concat([responseData, data]);
    });
    client.on("close", () => {
      resolve({ ok: true, latency: Date.now() - startTime, response: responseData.toString() });
    });
    client.on("error", (err) => {
      resolve({ ok: false, msg: err.message, latency: Date.now() - startTime });
    });
  });
});
electron.app.whenReady().then(createWindow);
electron.app.on("window-all-closed", () => {
  if (proxyProcess) proxyProcess.kill("SIGTERM");
  if (process.platform !== "darwin") electron.app.quit();
});
electron.app.on("activate", () => {
  if (electron.BrowserWindow.getAllWindows().length === 0) createWindow();
});
electron.app.on("before-quit", () => {
  if (proxyProcess) proxyProcess.kill("SIGTERM");
});

"use strict";
const electron = require("electron");
electron.contextBridge.exposeInMainWorld("electronAPI", {
  // ── Proxy Lifecycle ────────────────────────────────────────────────────────
  startProxy: (opts) => electron.ipcRenderer.invoke("proxy:start", opts),
  stopProxy: () => electron.ipcRenderer.invoke("proxy:stop"),
  getProxyStatus: () => electron.ipcRenderer.invoke("proxy:status"),
  // ── Rules CRUD ─────────────────────────────────────────────────────────────
  readRules: () => electron.ipcRenderer.invoke("rules:read"),
  writeRules: (rules) => electron.ipcRenderer.invoke("rules:write", rules),
  // ── File Reading ───────────────────────────────────────────────────────────
  readPayload: async (id, type) => {
    const res = await electron.ipcRenderer.invoke("payload:read", id, type);
    if (res.ok) return res.content;
    return "";
  },
  writePayload: async (id, type, content) => {
    return await electron.ipcRenderer.invoke("payload:write", id, type, content);
  },
  sendRawRequest: async (port, rawRequest) => {
    return await electron.ipcRenderer.invoke("request:sendRaw", port, rawRequest);
  },
  // ── System Proxy ───────────────────────────────────────────────────────────
  setSystemProxy: (opts) => electron.ipcRenderer.invoke("system:setProxy", opts),
  clearSystemProxy: () => electron.ipcRenderer.invoke("system:clearProxy"),
  downloadCert: () => electron.ipcRenderer.invoke("system:downloadCert"),
  // ── Tools ──────────────────────────────────────────────────────────────────
  getPorts: () => electron.ipcRenderer.invoke("tool:getPorts"),
  killPort: (pid) => electron.ipcRenderer.invoke("tool:killPort", pid),
  sendCurl: (cmd) => electron.ipcRenderer.invoke("tool:sendCurl", cmd),
  // ── Projects ───────────────────────────────────────────────────────────────
  selectProjectDir: () => electron.ipcRenderer.invoke("project:selectDir"),
  scanProjects: (dir) => electron.ipcRenderer.invoke("project:scan", dir),
  startProject: (path, script) => electron.ipcRenderer.invoke("project:start", path, script),
  stopProject: (path) => electron.ipcRenderer.invoke("project:stop", path),
  openEditor: (path, editor) => electron.ipcRenderer.invoke("project:openEditor", path, editor),
  openChrome: (url) => electron.ipcRenderer.invoke("project:openBrowser", url),
  // ── Git ────────────────────────────────────────────────────────────────────
  gitScan: (cwd) => electron.ipcRenderer.invoke("git:scan", cwd),
  gitStatus: (cwd) => electron.ipcRenderer.invoke("git:status", cwd),
  gitLog: (cwd) => electron.ipcRenderer.invoke("git:log", cwd),
  gitDiff: (cwd, file, staged) => electron.ipcRenderer.invoke("git:diff", cwd, file, staged),
  gitStage: (cwd, file) => electron.ipcRenderer.invoke("git:stage", cwd, file),
  gitUnstage: (cwd, file) => electron.ipcRenderer.invoke("git:unstage", cwd, file),
  gitCommit: (cwd, message) => electron.ipcRenderer.invoke("git:commit", cwd, message),
  gitBranch: (cwd) => electron.ipcRenderer.invoke("git:branch", cwd),
  gitCheckout: (cwd, branch) => electron.ipcRenderer.invoke("git:checkout", cwd, branch),
  gitDiscard: (cwd, file) => electron.ipcRenderer.invoke("git:discard", cwd, file),
  gitPull: (cwd) => electron.ipcRenderer.invoke("git:pull", cwd),
  gitPush: (cwd) => electron.ipcRenderer.invoke("git:push", cwd),
  gitFetch: (cwd) => electron.ipcRenderer.invoke("git:fetch", cwd),
  gitStash: (cwd) => electron.ipcRenderer.invoke("git:stash", cwd),
  gitStashPop: (cwd) => electron.ipcRenderer.invoke("git:stashPop", cwd),
  gitResolveConflict: (cwd, file, strategy) => electron.ipcRenderer.invoke("git:resolveConflict", cwd, file, strategy),
  gitDeleteFile: (cwd, file) => electron.ipcRenderer.invoke("git:deleteFile", cwd, file),
  gitAskpassReply: (reqId, password) => electron.ipcRenderer.invoke("git:askpass-reply", reqId, password),
  onProjectLog: (cb) => {
    electron.ipcRenderer.on("project:log", (_event, data) => cb(data));
  },
  onGitAskpass: (cb) => {
    electron.ipcRenderer.on("git:askpass", (_event, data) => cb(data));
  },
  onProjectReady: (cb) => {
    electron.ipcRenderer.on("project:ready", (_event, data) => cb(data));
  },
  onProjectStopped: (cb) => {
    electron.ipcRenderer.on("project:stopped", (_event, data) => cb(data));
  },
  // ── Events (main → renderer) ───────────────────────────────────────────────
  onLogLine: (cb) => {
    electron.ipcRenderer.on("log:line", (_event, line) => cb(line));
  },
  onProxyStopped: (cb) => {
    electron.ipcRenderer.on("proxy:stopped", (_event, data) => cb(data));
  },
  removeAllListeners: (channel) => {
    electron.ipcRenderer.removeAllListeners(channel);
  },
  // ── Window Controls ────────────────────────────────────────────────────────
  minimize: () => electron.ipcRenderer.send("window:minimize"),
  maximize: () => electron.ipcRenderer.send("window:maximize"),
  close: () => electron.ipcRenderer.send("window:close")
});

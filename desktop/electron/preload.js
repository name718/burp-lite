import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('electronAPI', {
  // ── Proxy Lifecycle ────────────────────────────────────────────────────────
  startProxy: (opts) => ipcRenderer.invoke('proxy:start', opts),
  stopProxy: () => ipcRenderer.invoke('proxy:stop'),
  getProxyStatus: () => ipcRenderer.invoke('proxy:status'),

  // ── Rules CRUD ─────────────────────────────────────────────────────────────
  readRules: () => ipcRenderer.invoke('rules:read'),
  writeRules: (rules) => ipcRenderer.invoke('rules:write', rules),

  // ── File Reading ───────────────────────────────────────────────────────────
  readPayload: async (id, type) => {
    const res = await ipcRenderer.invoke('payload:read', id, type)
    if (res.ok) return res.content
    return ''
  },
  writePayload: async (id, type, content) => {
    return await ipcRenderer.invoke('payload:write', id, type, content)
  },
  
  sendRawRequest: async (port, rawRequest) => {
    return await ipcRenderer.invoke('request:sendRaw', port, rawRequest)
  },

  // ── System Proxy ───────────────────────────────────────────────────────────
  setSystemProxy: (opts) => ipcRenderer.invoke('system:setProxy', opts),
  clearSystemProxy: () => ipcRenderer.invoke('system:clearProxy'),
  downloadCert: () => ipcRenderer.invoke('system:downloadCert'),

  // ── Tools ──────────────────────────────────────────────────────────────────
  getPorts: () => ipcRenderer.invoke('tool:getPorts'),
  killPort: (pid) => ipcRenderer.invoke('tool:killPort', pid),
  sendCurl: (cmd) => ipcRenderer.invoke('tool:sendCurl', cmd),

  // ── Projects ───────────────────────────────────────────────────────────────
  selectProjectDir: () => ipcRenderer.invoke('project:selectDir'),
  scanProjects: (dir) => ipcRenderer.invoke('project:scan', dir),
  startProject: (path, script) => ipcRenderer.invoke('project:start', path, script),
  stopProject: (path) => ipcRenderer.invoke('project:stop', path),
  openEditor: (path, editor) => ipcRenderer.invoke('project:openEditor', path, editor),
  openChrome: (url) => ipcRenderer.invoke('project:openBrowser', url),

  // ── Git ────────────────────────────────────────────────────────────────────
  gitScan: (cwd) => ipcRenderer.invoke('git:scan', cwd),
  gitStatus: (cwd) => ipcRenderer.invoke('git:status', cwd),
  gitLog: (cwd) => ipcRenderer.invoke('git:log', cwd),
  gitDiff: (cwd, file, staged) => ipcRenderer.invoke('git:diff', cwd, file, staged),
  gitStage: (cwd, file) => ipcRenderer.invoke('git:stage', cwd, file),
  gitUnstage: (cwd, file) => ipcRenderer.invoke('git:unstage', cwd, file),
  gitCommit: (cwd, message) => ipcRenderer.invoke('git:commit', cwd, message),
  gitBranch: (cwd) => ipcRenderer.invoke('git:branch', cwd),
  gitCheckout: (cwd, branch) => ipcRenderer.invoke('git:checkout', cwd, branch),
  gitDiscard: (cwd, file) => ipcRenderer.invoke('git:discard', cwd, file),
  gitPull: (cwd) => ipcRenderer.invoke('git:pull', cwd),
  gitPush: (cwd) => ipcRenderer.invoke('git:push', cwd),
  gitFetch: (cwd) => ipcRenderer.invoke('git:fetch', cwd),
  gitStash: (cwd) => ipcRenderer.invoke('git:stash', cwd),
  gitStashPop: (cwd) => ipcRenderer.invoke('git:stashPop', cwd),
  gitResolveConflict: (cwd, file, strategy) => ipcRenderer.invoke('git:resolveConflict', cwd, file, strategy),
  gitDeleteFile: (cwd, file) => ipcRenderer.invoke('git:deleteFile', cwd, file),
  gitAskpassReply: (reqId, password) => ipcRenderer.invoke('git:askpass-reply', reqId, password),
  onProjectLog: (cb) => {
    ipcRenderer.on('project:log', (_event, data) => cb(data))
  },
  onGitAskpass: (cb) => {
    ipcRenderer.on('git:askpass', (_event, data) => cb(data))
  },
  onProjectReady: (cb) => {
    ipcRenderer.on('project:ready', (_event, data) => cb(data))
  },
  onProjectStopped: (cb) => {
    ipcRenderer.on('project:stopped', (_event, data) => cb(data))
  },

  // ── Events (main → renderer) ───────────────────────────────────────────────
  onLogLine: (cb) => {
    ipcRenderer.on('log:line', (_event, line) => cb(line))
  },
  onProxyStopped: (cb) => {
    ipcRenderer.on('proxy:stopped', (_event, data) => cb(data))
  },
  removeAllListeners: (channel) => {
    ipcRenderer.removeAllListeners(channel)
  },

  // ── Window Controls ────────────────────────────────────────────────────────
  minimize: () => ipcRenderer.send('window:minimize'),
  maximize: () => ipcRenderer.send('window:maximize'),
  close: () => ipcRenderer.send('window:close'),
})

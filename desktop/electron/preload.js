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
  readPayload: (id, type) => ipcRenderer.invoke('payload:read', id, type),

  // ── System Proxy ───────────────────────────────────────────────────────────
  setSystemProxy: (opts) => ipcRenderer.invoke('system:setProxy', opts),
  clearSystemProxy: () => ipcRenderer.invoke('system:clearProxy'),

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

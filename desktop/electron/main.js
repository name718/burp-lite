import { app, BrowserWindow, ipcMain } from 'electron'
import { spawn, execSync } from 'child_process'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'
import { readFileSync, writeFileSync, existsSync } from 'fs'
import net from 'net'

const __dirname = dirname(fileURLToPath(import.meta.url))

// ─── State ───────────────────────────────────────────────────────────────────
let mainWindow = null
let proxyProcess = null

// ─── Path helpers ─────────────────────────────────────────────────────────────
// In dev: __dirname = desktop/dist-electron, binary is at desktop/../build/bin
// In prod: __dirname = app.asar/dist-electron, extraResources at process.resourcesPath
const isDev = process.env.VITE_DEV_SERVER_URL !== undefined

function getBinaryPath() {
  if (isDev) {
    return join(__dirname, '../../build/bin/burp-lite')
  }
  return join(process.resourcesPath, 'bin', 'burp-lite')
}

function getRulesPath() {
  if (isDev) {
    return join(__dirname, '../../rules.json')
  }
  return join(process.resourcesPath, 'rules.json')
}

// ─── Window ───────────────────────────────────────────────────────────────────
function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 1100,
    minHeight: 680,
    frame: false,
    backgroundColor: '#0a0d14',
    webPreferences: {
      preload: join(__dirname, '../dist-electron/preload.js'),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false,
    },
  })

  if (isDev) {
    mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL)
    // mainWindow.webContents.openDevTools()
  } else {
    mainWindow.loadFile(join(__dirname, '../dist/index.html'))
  }

  mainWindow.on('closed', () => {
    mainWindow = null
  })
}

// ─── IPC: Window Controls ─────────────────────────────────────────────────────
ipcMain.on('window:minimize', () => mainWindow?.minimize())
ipcMain.on('window:maximize', () => {
  if (mainWindow?.isMaximized()) mainWindow.unmaximize()
  else mainWindow?.maximize()
})
ipcMain.on('window:close', () => mainWindow?.close())

// ─── IPC: Proxy Lifecycle ─────────────────────────────────────────────────────
ipcMain.handle('proxy:start', (_event, { port = 8888 } = {}) => {
  if (proxyProcess) return { ok: false, msg: 'Already running' }

  const bin = getBinaryPath()
  const rules = getRulesPath()

  if (!existsSync(bin)) {
    return {
      ok: false,
      msg: `Binary not found at: ${bin}\nRun "make" in the project root first.`,
    }
  }

  try {
    proxyProcess = spawn(bin, ['-p', String(port), '-c', rules], {
      cwd: join(__dirname, '../..'),
    })

    proxyProcess.stdout.on('data', (data) => {
      const lines = data.toString().split('\n').filter(Boolean)
      lines.forEach((line) => {
        mainWindow?.webContents.send('log:line', line)
      })
    })

    proxyProcess.stderr.on('data', (data) => {
      const text = data.toString().trim()
      if (text) mainWindow?.webContents.send('log:line', `[STDERR] ${text}`)
    })

    proxyProcess.on('close', (code) => {
      proxyProcess = null
      mainWindow?.webContents.send('proxy:stopped', { code })
    })

    proxyProcess.on('error', (err) => {
      proxyProcess = null
      mainWindow?.webContents.send('proxy:stopped', { code: -1, err: err.message })
    })

    return { ok: true, pid: proxyProcess.pid }
  } catch (err) {
    proxyProcess = null
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('proxy:stop', () => {
  if (!proxyProcess) return { ok: false, msg: 'Not running' }
  try {
    proxyProcess.kill('SIGTERM')
    proxyProcess = null
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('proxy:status', () => ({
  running: !!proxyProcess,
  pid: proxyProcess?.pid ?? null,
}))

// ─── IPC: Rules CRUD ──────────────────────────────────────────────────────────
ipcMain.handle('rules:read', () => {
  try {
    const raw = readFileSync(getRulesPath(), 'utf-8')
    return { ok: true, rules: JSON.parse(raw) }
  } catch {
    return { ok: true, rules: [] }
  }
})

ipcMain.handle('rules:write', (_event, rules) => {
  try {
    // Strip internal _id fields before writing
    const clean = rules.map(({ _id, ...r }) => r) // eslint-disable-line no-unused-vars
    writeFileSync(getRulesPath(), JSON.stringify(clean, null, 2), 'utf-8')
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

// ─── IPC: System Proxy (macOS) ────────────────────────────────────────────────
ipcMain.handle('system:setProxy', (_event, { host = '127.0.0.1', port = 8888 } = {}) => {
  try {
    // Try both Wi-Fi and Ethernet
    const services = ['Wi-Fi', 'Ethernet', 'USB 10/100/1000 LAN']
    for (const svc of services) {
      try {
        execSync(`networksetup -setwebproxy "${svc}" ${host} ${port} off 2>/dev/null`)
        execSync(`networksetup -setwebproxystate "${svc}" on 2>/dev/null`)
        execSync(`networksetup -setsecurewebproxy "${svc}" ${host} ${port} off 2>/dev/null`)
        execSync(`networksetup -setsecurewebproxystate "${svc}" on 2>/dev/null`)
      } catch { /* ignore unavailable interfaces */ }
    }
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('system:clearProxy', () => {
  try {
    const services = ['Wi-Fi', 'Ethernet', 'USB 10/100/1000 LAN']
    for (const svc of services) {
      try {
        execSync(`networksetup -setwebproxystate "${svc}" off 2>/dev/null`)
        execSync(`networksetup -setsecurewebproxystate "${svc}" off 2>/dev/null`)
      } catch { /* ignore */ }
    }
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

// ─── IPC: Read Payload ────────────────────────────────────────────────────────
ipcMain.handle('payload:read', (_event, id, type) => {
  try {
    const filePath = join('/tmp/chaos_logs', `${id}_${type}.bin`)
    if (!existsSync(filePath)) {
      return { ok: false, msg: 'File not found' }
    }
    const content = readFileSync(filePath, 'utf-8')
    return { ok: true, content }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('payload:write', (_event, id, type, content) => {
  try {
    const filePath = join('/tmp/chaos_logs', `${id}_${type}.bin`)
    writeFileSync(filePath, content, 'utf-8')
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

// ─── IPC: Send Raw Request ────────────────────────────────────────────────────
ipcMain.handle('request:sendRaw', (_event, port, rawRequest) => {
  return new Promise((resolve) => {
    const client = new net.Socket()
    let responseData = Buffer.alloc(0)
    let startTime = Date.now()

    let finalRequest = rawRequest;
    const parts = rawRequest.split('\r\n\r\n');
    if (parts.length >= 2) {
      const headStr = parts[0];
      const bodyStr = parts.slice(1).join('\r\n\r\n');
      const actualLen = Buffer.byteLength(bodyStr, 'utf8');
      if (/Content-Length:\s*\d+/i.test(headStr)) {
        const newHead = headStr.replace(/Content-Length:\s*\d+/i, `Content-Length: ${actualLen}`);
        finalRequest = newHead + '\r\n\r\n' + bodyStr;
      }
    }

    client.connect(port, '127.0.0.1', () => {
      client.write(finalRequest)
    })

    client.on('data', (data) => {
      responseData = Buffer.concat([responseData, data])
    })

    client.on('close', () => {
      resolve({ ok: true, latency: Date.now() - startTime, response: responseData.toString() })
    })

    client.on('error', (err) => {
      resolve({ ok: false, msg: err.message, latency: Date.now() - startTime })
    })
  })
})

// ─── App Lifecycle ────────────────────────────────────────────────────────────
app.whenReady().then(createWindow)

app.on('window-all-closed', () => {
  if (proxyProcess) proxyProcess.kill('SIGTERM')
  if (process.platform !== 'darwin') app.quit()
})

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow()
})

app.on('before-quit', () => {
  if (proxyProcess) proxyProcess.kill('SIGTERM')
})

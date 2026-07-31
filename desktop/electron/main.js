import { app, BrowserWindow, ipcMain, dialog } from 'electron'
import { spawn, execSync } from 'child_process'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'
import { readFileSync, writeFileSync, existsSync, copyFileSync, readdirSync, statSync } from 'fs'
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

ipcMain.handle('system:downloadCert', async () => {
  try {
    const caPath = '/tmp/chaos_ca/ca.crt'
    if (!existsSync(caPath)) {
      return { ok: false, msg: 'Root CA certificate not found at /tmp/chaos_ca/ca.crt. Has the proxy started?' }
    }
    
    // Default to Desktop
    const defaultPath = join(app.getPath('desktop'), 'burp-lite-ca.crt')
    
    const { canceled, filePath } = await dialog.showSaveDialog({
      title: 'Save Root CA Certificate',
      defaultPath,
      filters: [{ name: 'Certificates', extensions: ['crt', 'cer', 'pem'] }]
    })
    
    if (canceled || !filePath) {
      return { ok: false, msg: 'Cancelled by user' }
    }
    
    copyFileSync(caPath, filePath)
    return { ok: true, msg: 'Certificate saved successfully to ' + filePath }
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

    // Normalize newlines to \r\n (textarea might emit just \n)
    let normalizedReq = rawRequest.replace(/\r\n/g, '\n').replace(/\n/g, '\r\n');
    if (!normalizedReq.includes('\r\n\r\n')) {
      if (normalizedReq.endsWith('\r\n')) {
        normalizedReq += '\r\n';
      } else {
        normalizedReq += '\r\n\r\n';
      }
    }

    let finalRequest = normalizedReq;
    const parts = normalizedReq.split('\r\n\r\n');
    if (parts.length >= 2) {
      const headStr = parts[0];
      const bodyStr = parts.slice(1).join('\r\n\r\n');
      // Only fix Content-Length if it's actually a request with a body
      if (/Content-Length:\s*\d+/i.test(headStr)) {
        const actualLen = Buffer.byteLength(bodyStr, 'utf8');
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

// ─── IPC: Port Manager ────────────────────────────────────────────────────────
ipcMain.handle('tool:getPorts', async () => {
  try {
    // macOS 'lsof -i -P -n | grep LISTEN' outputs:
    // COMMAND   PID USER   FD   TYPE             DEVICE SIZE/OFF NODE NAME
    // node    12345 didi   23u  IPv4 0xabcdef1234567890      0t0  TCP *:8888 (LISTEN)
    const out = execSync('lsof -i -P -n | grep LISTEN', { encoding: 'utf-8' })
    const lines = out.split('\n').filter(Boolean)
    const ports = []

    for (const line of lines) {
      const parts = line.trim().split(/\s+/)
      if (parts.length >= 9) {
        const processName = parts[0]
        const pid = parseInt(parts[1], 10)
        const protocol = parts[7]
        
        // Parse "NAME" column which looks like "*:8888" or "127.0.0.1:8888" or "localhost:8888"
        const nameCol = parts.slice(8).join(' ')
        const match = nameCol.match(/:(\d+)/)
        if (match) {
          const port = parseInt(match[1], 10)
          ports.push({ port, pid, processName, protocol })
        }
      }
    }
    // Deduplicate by port
    const uniquePorts = []
    const seen = new Set()
    for (const p of ports) {
      if (!seen.has(p.port)) {
        seen.add(p.port)
        uniquePorts.push(p)
      }
    }
    // Sort by port number
    uniquePorts.sort((a, b) => a.port - b.port)
    return { ok: true, ports: uniquePorts }
  } catch (err) {
    // grep returns 1 if no match, which throws an error in execSync
    if (err.status === 1) return { ok: true, ports: [] }
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('tool:killPort', async (_event, pid) => {
  try {
    execSync(`kill -9 ${pid}`)
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

// ─── IPC: Project Manager ─────────────────────────────────────────────────────
const projectProcesses = new Map() // key: projectPath, value: childProcess object

ipcMain.handle('project:selectDir', async () => {
  const { canceled, filePaths } = await dialog.showOpenDialog(mainWindow, {
    properties: ['openDirectory']
  })
  if (canceled || filePaths.length === 0) return { ok: false }
  return { ok: true, path: filePaths[0] }
})

ipcMain.handle('project:scan', async (_event, rootDir) => {
  try {
    if (!existsSync(rootDir)) return { ok: false, msg: 'Directory not found' }
    
    const projects = []
    const items = readdirSync(rootDir)
    
    for (const item of items) {
      if (item === 'node_modules' || item.startsWith('.')) continue
      
      const itemPath = join(rootDir, item)
      if (statSync(itemPath).isDirectory()) {
        const pkgPath = join(itemPath, 'package.json')
        if (existsSync(pkgPath)) {
          try {
            const pkgData = JSON.parse(readFileSync(pkgPath, 'utf-8'))
            const scripts = pkgData.scripts || {}
            // Determine best script
            let script = null
            if (scripts.dev) script = 'dev'
            else if (scripts.serve) script = 'serve'
            else if (scripts.start) script = 'start'
            
            projects.push({
              name: pkgData.name || item,
              path: itemPath,
              script: script,
              hasScripts: !!script
            })
          } catch (e) {
            // Ignore malformed package.json
          }
        }
      }
    }
    return { ok: true, projects }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('project:start', (_event, projectPath, script) => {
  if (projectProcesses.has(projectPath)) {
    return { ok: false, msg: 'Project is already running' }
  }

  try {
    // detached: true creates a new process group, so we can kill all subprocesses (vite, node, etc.) easily on mac/linux
    const child = spawn('npm', ['run', script], {
      cwd: projectPath,
      detached: true,
      stdio: 'pipe'
    })

    const onData = (data) => {
      const text = data.toString()
      mainWindow?.webContents.send('project:log', { path: projectPath, text })
      
      // Attempt to extract all local and network URLs
      const regex = /http:\/\/(localhost|127\.0\.0\.1|192\.168\.\d+\.\d+|[\d\.]+):(\d+)(\/[^\s\x1b]*)?/gi
      const matches = [...text.matchAll(regex)]
      if (matches.length > 0) {
        const urls = matches.map(m => m[0])
        mainWindow?.webContents.send('project:ready', { path: projectPath, urls })
      }
    }

    child.stdout.on('data', onData)
    child.stderr.on('data', onData)

    child.on('close', (code) => {
      projectProcesses.delete(projectPath)
      mainWindow?.webContents.send('project:stopped', { path: projectPath, code })
    })
    
    child.on('error', (err) => {
      projectProcesses.delete(projectPath)
      mainWindow?.webContents.send('project:stopped', { path: projectPath, error: err.message })
    })

    projectProcesses.set(projectPath, child)
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('project:stop', (_event, projectPath) => {
  const child = projectProcesses.get(projectPath)
  if (!child) return { ok: false, msg: 'Process not found' }
  
  try {
    // Kill the process group to ensure child processes (like Vite/Webpack) are also terminated
    if (child.pid) {
      process.kill(-child.pid) 
    }
    projectProcesses.delete(projectPath)
    return { ok: true }
  } catch (err) {
    // If it fails (e.g. ESRCH), just clean up
    projectProcesses.delete(projectPath)
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('project:openEditor', async (_event, projectPath, editor) => {
  try {
    // editor can be 'code' or 'zed'
    execSync(`${editor} "${projectPath}"`)
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

ipcMain.handle('project:openBrowser', async (_event, url) => {
  try {
    execSync(`open -a "Google Chrome" "${url}"`)
    return { ok: true }
  } catch (err) {
    return { ok: false, msg: err.message }
  }
})

// ─── IPC: Dev Tools ───────────────────────────────────────────────────────────
ipcMain.handle('tool:sendCurl', async (_event, curlCmd) => {
  try {
    let cmd = curlCmd.trim()
    if (!cmd.toLowerCase().startsWith('curl')) {
      return { ok: false, msg: 'Not a curl command' }
    }
    // inject -i to include headers, -s for silent progress
    if (!cmd.includes(' -i') && !cmd.includes(' -is') && !cmd.includes(' -si')) {
      cmd = cmd.replace(/^curl(\.exe)? /i, 'curl -is ')
    }
    
    // Using execSync because curl commands from browser often contain complex quotes and escapes
    // Max buffer 10MB just in case the response is huge
    const out = execSync(cmd, { encoding: 'utf-8', maxBuffer: 10 * 1024 * 1024 })
    return { ok: true, output: out }
  } catch (err) {
    return { ok: false, msg: err.message, output: err.stdout?.toString() || '' }
  }
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

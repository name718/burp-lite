<template>
  <div class="desktop-app">
    <!-- 1. 顶部全局系统控制栏 -->
    <header class="top-bar">
      <div class="brand">
        <div class="logo">⚡</div>
        <div class="brand-info">
          <span class="title">Chaos-Proxy Desktop</span>
          <span class="version">v1.0 (Burp Suite Edition)</span>
        </div>
      </div>

      <div class="top-controls">
        <!-- 系统代理一键开关 -->
        <button 
          class="toggle-btn" 
          :class="{ active: systemProxyOn }" 
          @click="systemProxyOn = !systemProxyOn"
        >
          <span class="dot"></span>
          <span>系统代理: {{ systemProxyOn ? '已接管 (127.0.0.1:8888)' : '未开启' }}</span>
        </button>

        <!-- Interceptor 拦截器全局大开关 -->
        <button 
          class="toggle-btn interceptor-btn" 
          :class="{ active: interceptorOn }" 
          @click="interceptorOn = !interceptorOn"
        >
          <span>⚡ 实时拦截器: {{ interceptorOn ? 'ON (挂起中)' : 'OFF (直通)' }}</span>
        </button>
      </div>
    </header>

    <!-- 2. 主功能 Tab 导航 -->
    <nav class="nav-tabs">
      <button 
        v-for="tab in tabs" 
        :key="tab.id"
        class="tab-btn" 
        :class="{ active: activeTab === tab.id }"
        @click="activeTab = tab.id"
      >
        <span class="tab-icon">{{ tab.icon }}</span>
        <span>{{ tab.label }}</span>
        <span v-if="tab.badge" class="tab-badge">{{ tab.badge }}</span>
      </button>
    </nav>

    <!-- 3. Tab 页面内容视窗 -->
    <div class="workspace-area">
      <!-- A. HTTP History 抓包历史面板 (双栏布局: 上列表 下Inspector) -->
      <section v-if="activeTab === 'history'" class="tab-page history-layout">
        <!-- 上半部: 抓包请求流数据表格 -->
        <div class="table-container">
          <table class="packet-table">
            <thead>
              <tr>
                <th style="width: 60px;">#</th>
                <th style="width: 90px;">Method</th>
                <th style="width: 180px;">Host</th>
                <th>Path</th>
                <th style="width: 100px;">Status</th>
                <th style="width: 100px;">Delay</th>
                <th style="width: 90px;">Size</th>
                <th style="width: 100px;">Time</th>
              </tr>
            </thead>
            <tbody>
              <tr 
                v-for="pkg in historyList" 
                :key="pkg.id"
                :class="{ selected: selectedPkgId === pkg.id, chaos: pkg.injected }"
                @click="selectedPkgId = pkg.id"
              >
                <td class="num">{{ pkg.id }}</td>
                <td><span class="method-badge" :class="pkg.method.toLowerCase()">{{ pkg.method }}</span></td>
                <td class="host">{{ pkg.host }}</td>
                <td class="path">{{ pkg.path }}</td>
                <td><span class="status-code" :class="getStatusClass(pkg.status)">{{ pkg.status }}</span></td>
                <td class="delay"><span v-if="pkg.delay > 0" class="delay-tag">⏱ {{ pkg.delay }}ms</span><span v-else>-</span></td>
                <td class="size">{{ pkg.size }}</td>
                <td class="time">{{ pkg.time }}</td>
              </tr>
            </tbody>
          </table>
        </div>

        <!-- 下半部: Request / Response 报文分栏 Inspector (对标 Burp Inspector) -->
        <div v-if="selectedPkg" class="inspector-panel">
          <div class="inspector-column">
            <div class="inspector-header">
              <span class="title">📥 Request (客户端请求)</span>
              <button class="btn-action" @click="sendToRepeater(selectedPkg)">⏩ Send to Repeater</button>
            </div>
            <div class="inspector-tabs">
              <button :class="{ active: reqTab === 'headers' }" @click="reqTab = 'headers'">Headers</button>
              <button :class="{ active: reqTab === 'raw' }" @click="reqTab = 'raw'">Raw Text</button>
              <button :class="{ active: reqTab === 'json' }" @click="reqTab = 'json'">JSON</button>
            </div>
            <div class="inspector-body">
              <pre v-if="reqTab === 'headers'">{{ selectedPkg.reqHeaders }}</pre>
              <pre v-else-if="reqTab === 'raw'">{{ selectedPkg.reqRaw }}</pre>
              <pre v-else>{{ selectedPkg.reqJson }}</pre>
            </div>
          </div>

          <div class="inspector-column">
            <div class="inspector-header">
              <span class="title">📤 Response (服务端响应 / 故障结果)</span>
              <span v-if="selectedPkg.injected" class="chaos-flag">💥 Chaos Injected</span>
            </div>
            <div class="inspector-tabs">
              <button :class="{ active: respTab === 'headers' }" @click="respTab = 'headers'">Headers</button>
              <button :class="{ active: respTab === 'raw' }" @click="respTab = 'raw'">Raw Text</button>
              <button :class="{ active: respTab === 'json' }" @click="respTab = 'json'">JSON</button>
            </div>
            <div class="inspector-body">
              <pre v-if="respTab === 'headers'">{{ selectedPkg.respHeaders }}</pre>
              <pre v-else-if="respTab === 'raw'">{{ selectedPkg.respRaw }}</pre>
              <pre v-else>{{ selectedPkg.respJson }}</pre>
            </div>
          </div>
        </div>
      </section>

      <!-- B. Proxy Interceptor 实时请求拦截挂起视图 -->
      <section v-else-if="activeTab === 'interceptor'" class="tab-page interceptor-layout">
        <div class="interceptor-bar">
          <button class="btn-forward" @click="forwardCurrent">⏩ Forward (放行连接)</button>
          <button class="btn-drop" @click="dropCurrent">🗑 Drop (丢弃连接)</button>
          <span class="status-tip">状态: 有 1 个请求被挂起等待审批...</span>
        </div>
        <div class="inspector-panel flex-1">
          <div class="inspector-column width-100">
            <div class="inspector-header">
              <span class="title">✏️ 可编辑挂起的请求报文 (手动改包)</span>
            </div>
            <div class="inspector-body">
              <textarea v-model="interceptedRaw" class="raw-textarea"></textarea>
            </div>
          </div>
        </div>
      </section>

      <!-- C. Chaos Rules 故障规则配置视图 -->
      <section v-else-if="activeTab === 'rules'" class="tab-page rules-layout">
        <div class="rules-view">
          <h2>💥 Chaos 故障规则全量配置视窗</h2>
          <p class="desc">在这里配置对目标接口注入延迟、状态码伪造与 Body 内容篡改的规则。</p>
        </div>
      </section>

      <!-- D. Repeater 请求重发视窗 -->
      <section v-else-if="activeTab === 'repeater'" class="tab-page repeater-layout">
        <div class="repeater-bar">
          <button class="btn-primary" @click="sendRepeater">🚀 Send (重发请求)</button>
        </div>
        <div class="inspector-panel flex-1">
          <div class="inspector-column">
            <div class="inspector-header"><span class="title">Request 构造器</span></div>
            <div class="inspector-body">
              <textarea v-model="repeaterReq" class="raw-textarea"></textarea>
            </div>
          </div>
          <div class="inspector-column">
            <div class="inspector-header"><span class="title">Response 反馈结果</span></div>
            <div class="inspector-body">
              <pre>{{ repeaterResp }}</pre>
            </div>
          </div>
        </div>
      </section>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'

const activeTab = ref('history')
const systemProxyOn = ref(false)
const interceptorOn = ref(true)

const tabs = computed(() => [
  { id: 'history', label: 'HTTP 抓包历史 (History)', icon: '📜', badge: historyList.value.length },
  { id: 'interceptor', label: '实时拦截 (Interceptor)', icon: '🛑', badge: '1' },
  { id: 'rules', label: '故障规则 (Chaos Rules)', icon: '💥' },
  { id: 'repeater', label: '重发器 (Repeater)', icon: '🔄' }
])

// 抓包模拟列表数据 (对标 Burp Suite HTTP History)
const historyList = ref([
  {
    id: 1,
    method: 'POST',
    host: 'api.github.com',
    path: '/api/user/login',
    status: 503,
    delay: 2000,
    size: '1.2 KB',
    time: '15:30:12',
    injected: true,
    reqHeaders: 'POST /api/user/login HTTP/1.1\r\nHost: api.github.com\r\nContent-Type: application/json',
    reqRaw: '{\n  "user": "admin",\n  "pass": "secret"\n}',
    reqJson: '{\n  "user": "admin",\n  "pass": "secret"\n}',
    respHeaders: 'HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: 27',
    respRaw: '{\n  "code": 5,\n  "msg": "chaos ok"\n}',
    respJson: '{\n  "code": 5,\n  "msg": "chaos ok"\n}'
  },
  {
    id: 2,
    method: 'GET',
    host: 'example.com',
    path: '/api/order/list',
    status: 200,
    delay: 3000,
    size: '4.8 KB',
    time: '15:30:15',
    injected: true,
    reqHeaders: 'GET /api/order/list HTTP/1.1\r\nHost: example.com',
    reqRaw: 'GET /api/order/list HTTP/1.1',
    reqJson: '{}',
    respHeaders: 'HTTP/1.1 200 OK\r\nContent-Type: application/json',
    respRaw: '{\n  "code": 0,\n  "data": [ {"id": 101, "price": 99} ]\n}',
    respJson: '{\n  "code": 0,\n  "data": [ {"id": 101, "price": 99} ]\n}'
  },
  {
    id: 3,
    method: 'GET',
    host: 'api.github.com',
    path: '/api/ping',
    status: 200,
    delay: 0,
    size: '310 B',
    time: '15:30:18',
    injected: false,
    reqHeaders: 'GET /api/ping HTTP/1.1\r\nHost: api.github.com',
    reqRaw: 'GET /api/ping HTTP/1.1',
    reqJson: '{}',
    respHeaders: 'HTTP/1.1 200 OK\r\nContent-Type: application/json',
    respRaw: '{\n  "code": 0,\n  "msg": "proxy forwarded"\n}',
    respJson: '{\n  "code": 0,\n  "msg": "proxy forwarded"\n}'
  }
])

const selectedPkgId = ref(1)
const reqTab = ref('headers')
const respTab = ref('headers')

const selectedPkg = computed(() => {
  return historyList.value.find(p => p.id === selectedPkgId.value)
})

const getStatusClass = (status) => {
  if (status >= 500) return 'err-500'
  if (status >= 400) return 'err-400'
  return 'ok-200'
}

// 拦截器改包 Raw Text
const interceptedRaw = ref(
  'POST /api/user/login HTTP/1.1\r\nHost: api.github.com\r\nContent-Type: application/json\r\n\r\n{\n  "user": "intercepted_admin"\n}'
)

// Repeater 数据
const repeaterReq = ref('POST /api/user/login HTTP/1.1\r\nHost: api.github.com\r\n\r\n{"user":"repeater"}')
const repeaterResp = ref('点击 🚀 Send 查看重发数据反馈...')

const sendToRepeater = (pkg) => {
  repeaterReq.value = pkg.reqHeaders + '\r\n\r\n' + pkg.reqRaw
  activeTab.value = 'repeater'
}

const sendRepeater = () => {
  repeaterResp.value = 'HTTP/1.1 503 Service Unavailable (Repeated Test Result)\r\nContent-Type: application/json\r\n\r\n{\n  "code": 5,\n  "msg": "chaos repeater feedback"\n}'
}

const forwardCurrent = () => {
  alert('⏩ 请求已放行发送！')
}

const dropCurrent = () => {
  alert('🗑 请求已被手动丢弃！')
}
</script>

<style scoped>
.desktop-app {
  height: 100vh;
  display: flex;
  flex-direction: column;
  background: var(--bg-dark);
}

.top-bar {
  background: var(--header-bg);
  border-bottom: 1px solid var(--border);
  padding: 0.6rem 1.5rem;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.brand {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.logo {
  width: 34px;
  height: 34px;
  background: linear-gradient(135deg, var(--accent-orange), var(--accent-purple));
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.1rem;
}

.brand-info {
  display: flex;
  flex-direction: column;
}

.brand-info .title {
  font-weight: 700;
  font-size: 1.05rem;
  color: #fff;
}

.brand-info .version {
  font-size: 0.75rem;
  color: var(--accent-cyan);
  font-family: monospace;
}

.top-controls {
  display: flex;
  gap: 1rem;
}

.toggle-btn {
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--border);
  color: var(--text-muted);
  padding: 0.4rem 0.9rem;
  border-radius: 6px;
  font-size: 0.85rem;
  font-weight: 600;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 0.5rem;
  transition: all 0.2s;
}

.toggle-btn.active {
  background: rgba(16, 185, 129, 0.15);
  border-color: var(--success);
  color: #fff;
}

.toggle-btn .dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--text-muted);
}

.toggle-btn.active .dot {
  background: var(--success);
  box-shadow: 0 0 8px var(--success);
}

.interceptor-btn.active {
  background: rgba(249, 115, 22, 0.2);
  border-color: var(--accent-orange);
  color: #fff;
}

.nav-tabs {
  background: #0f1420;
  border-bottom: 1px solid var(--border);
  display: flex;
  padding: 0 1rem;
}

.tab-btn {
  background: transparent;
  border: none;
  color: var(--text-muted);
  padding: 0.75rem 1.25rem;
  font-size: 0.88rem;
  font-weight: 600;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 0.5rem;
  border-bottom: 2px solid transparent;
  transition: all 0.2s;
}

.tab-btn:hover {
  color: #fff;
}

.tab-btn.active {
  color: #fff;
  border-bottom-color: var(--accent-orange);
  background: rgba(255, 255, 255, 0.02);
}

.tab-badge {
  background: rgba(255, 255, 255, 0.1);
  padding: 0.1rem 0.4rem;
  border-radius: 10px;
  font-size: 0.75rem;
  font-family: monospace;
}

.workspace-area {
  flex: 1;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.tab-page {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.table-container {
  flex: 1;
  overflow-y: auto;
  border-bottom: 1px solid var(--border);
}

.packet-table {
  width: 100%;
  border-collapse: collapse;
  text-align: left;
  font-size: 0.85rem;
}

.packet-table th {
  background: #121824;
  color: var(--text-muted);
  padding: 0.6rem 1rem;
  font-weight: 600;
  position: sticky;
  top: 0;
  border-bottom: 1px solid var(--border);
}

.packet-table td {
  padding: 0.6rem 1rem;
  border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  font-family: monospace;
}

.packet-table tr {
  cursor: pointer;
}

.packet-table tr:hover {
  background: rgba(255, 255, 255, 0.03);
}

.packet-table tr.selected {
  background: rgba(139, 92, 246, 0.15) !important;
}

.packet-table tr.chaos {
  border-left: 3px solid var(--accent-orange);
}

.method-badge {
  padding: 0.15rem 0.4rem;
  border-radius: 4px;
  font-weight: 700;
  font-size: 0.75rem;
}

.method-badge.post { background: rgba(249, 115, 22, 0.2); color: var(--accent-orange); }
.method-badge.get { background: rgba(6, 182, 212, 0.2); color: var(--accent-cyan); }

.status-code.ok-200 { color: var(--success); }
.status-code.err-500 { color: var(--danger); font-weight: 700; }

.delay-tag {
  color: var(--accent-orange);
  font-weight: 600;
}

.inspector-panel {
  height: 280px;
  display: flex;
  background: var(--panel-bg);
}

.inspector-column {
  flex: 1;
  display: flex;
  flex-direction: column;
  border-right: 1px solid var(--border);
}

.inspector-column:last-child { border-right: none; }
.inspector-column.width-100 { flex: none; width: 100%; }

.inspector-header {
  padding: 0.5rem 1rem;
  background: rgba(0, 0, 0, 0.2);
  border-bottom: 1px solid var(--border);
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 0.85rem;
  font-weight: 600;
}

.btn-action {
  background: rgba(139, 92, 246, 0.2);
  color: #c4b5fd;
  border: 1px solid rgba(139, 92, 246, 0.3);
  padding: 0.2rem 0.5rem;
  border-radius: 4px;
  font-size: 0.75rem;
  cursor: pointer;
}

.btn-action:hover { background: rgba(139, 92, 246, 0.4); }

.chaos-flag {
  color: var(--accent-orange);
  font-size: 0.8rem;
  font-weight: 700;
}

.inspector-tabs {
  display: flex;
  background: rgba(0, 0, 0, 0.1);
  border-bottom: 1px solid var(--border);
}

.inspector-tabs button {
  background: transparent;
  border: none;
  color: var(--text-muted);
  padding: 0.35rem 0.8rem;
  font-size: 0.78rem;
  cursor: pointer;
}

.inspector-tabs button.active {
  color: #fff;
  background: rgba(255, 255, 255, 0.05);
  font-weight: 600;
}

.inspector-body {
  flex: 1;
  padding: 0.75rem 1rem;
  overflow: auto;
  font-family: monospace;
  font-size: 0.85rem;
  color: #cbd5e1;
}

.raw-textarea {
  width: 100%;
  height: 100%;
  background: transparent;
  border: none;
  color: #a7f3d0;
  font-family: monospace;
  font-size: 0.9rem;
  outline: none;
  resize: none;
}

.interceptor-bar, .repeater-bar {
  padding: 0.75rem 1.5rem;
  background: rgba(0, 0, 0, 0.3);
  border-bottom: 1px solid var(--border);
  display: flex;
  align-items: center;
  gap: 1rem;
}

.btn-forward {
  background: var(--success);
  color: #fff;
  border: none;
  padding: 0.5rem 1.2rem;
  border-radius: 6px;
  font-weight: 700;
  cursor: pointer;
}

.btn-drop {
  background: var(--danger);
  color: #fff;
  border: none;
  padding: 0.5rem 1.2rem;
  border-radius: 6px;
  font-weight: 700;
  cursor: pointer;
}

.btn-primary {
  background: var(--accent-purple);
  color: #fff;
  border: none;
  padding: 0.5rem 1.2rem;
  border-radius: 6px;
  font-weight: 700;
  cursor: pointer;
}

.status-tip {
  font-size: 0.85rem;
  color: var(--accent-orange);
  font-weight: 600;
}

.flex-1 { flex: 1; }
</style>

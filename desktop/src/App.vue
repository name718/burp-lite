<template>
  <div class="app">

    <!-- ① 自定义标题栏 (无边框窗口) -->
    <div class="titlebar drag-region">
      <div class="titlebar-left no-drag">
        <button class="wc wc-close"   @click="api.close()"    title="关闭" />
        <button class="wc wc-min"     @click="api.minimize()" title="最小化" />
        <button class="wc wc-max"     @click="api.maximize()" title="最大化" />
      </div>
      <div class="titlebar-title">
        <span class="tb-logo">⚡</span> Burp Lite
      </div>
      <div class="titlebar-right no-drag">
        <span class="tb-version">v1.0</span>
      </div>
    </div>

    <!-- ② 顶部控制栏 -->
    <header class="topbar">
      <div class="topbar-left">
        <!-- 代理状态指示 -->
        <div class="status-pill" :class="proxyRunning ? 'status-on' : 'status-off'">
          <span class="status-dot" />
          <span>{{ proxyRunning ? `Listening :${proxyPort}` : 'Stopped' }}</span>
        </div>

        <!-- 实时统计 -->
        <div class="stat-chips">
          <div class="stat-chip">
            <span class="chip-val">{{ stats.total }}</span>
            <span class="chip-lbl">Requests</span>
          </div>
          <div class="stat-chip chaos-chip">
            <span class="chip-val">{{ stats.injected }}</span>
            <span class="chip-lbl">Chaos</span>
          </div>
          <div class="stat-chip">
            <span class="chip-val">{{ stats.avgMs }}ms</span>
            <span class="chip-lbl">Avg Latency</span>
          </div>
        </div>
      </div>

      <div class="topbar-right no-drag">
        <!-- 端口输入 -->
        <div class="port-group">
          <label>Port</label>
          <input v-model.number="proxyPort" type="number" min="1024" max="65535"
                 :disabled="proxyRunning" class="port-input" />
        </div>

        <!-- 系统代理开关 -->
        <button class="ctrl-btn sys-btn"
                :class="{ 'ctrl-on': systemProxyOn }"
                @click="toggleSystemProxy"
                :disabled="!proxyRunning"
                :title="!proxyRunning ? '请先启动代理' : ''">
          <span class="btn-dot" />
          系统代理
        </button>

        <!-- 启动/停止 -->
        <button class="ctrl-btn start-btn"
                :class="{ 'stop-mode': proxyRunning }"
                @click="toggleProxy">
          {{ proxyRunning ? '⏹ Stop' : '▶ Start' }}
        </button>
      </div>
    </header>

    <!-- ③ Tab 导航 -->
    <nav class="tab-nav">
      <button v-for="t in tabs" :key="t.id"
              class="tab-btn"
              :class="{ active: activeTab === t.id }"
              @click="activeTab = t.id">
        <span class="tab-icon">{{ t.icon }}</span>
        {{ t.label }}
        <span v-if="t.id === 'history' && historyList.length" class="tab-badge">
          {{ historyList.length }}
        </span>
        <span v-if="t.id === 'interceptor' && pendingCount" class="tab-badge tab-badge-warn">
          {{ pendingCount }}
        </span>
      </button>
      <!-- 日志开关 -->
      <div class="tab-nav-right">
        <button class="log-toggle" :class="{active: showLog}" @click="showLog = !showLog">
          📋 Log
        </button>
      </div>
    </nav>

    <!-- ④ 主内容 -->
    <div class="workspace">

      <!-- ════════════ A. HTTP History ════════════ -->
      <section v-show="activeTab === 'history'" class="tab-pane history-pane" :class="{'split-layout': !!selectedItem}">
        <!-- 左侧/顶部：请求列表 -->
        <div class="history-list-view">
          <div class="pane-toolbar">
            <button class="toolbar-btn danger-btn" @click="clearHistory">🗑 Clear</button>
            <label class="filter-label">
              <span>Filter:</span>
              <input v-model="historyFilter" placeholder="/api/..." class="filter-input" />
            </label>
            <label class="filter-label">
              <input type="checkbox" v-model="filterChaosOnly" />
              <span>Chaos only</span>
            </label>
          </div>

          <div class="req-table-wrap">
            <table class="req-table">
              <thead>
                <tr>
                  <th style="width:44px" v-if="!selectedItem">#</th>
                  <th :style="{ width: selectedItem ? '100%' : 'auto' }">名称 (Name)</th>
                  <th style="width:72px" v-if="!selectedItem">Status</th>
                  <th style="width:90px" v-if="!selectedItem">Latency</th>
                  <th style="width:72px" v-if="!selectedItem">Size</th>
                  <th style="width:78px" v-if="!selectedItem">Time</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="item in filteredHistory" :key="item.id"
                    :class="{ selected: selectedId === item.id, 'chaos-row': item.injected, 'is-error': item.status >= 400 }"
                    @click="selectedId = item.id">
                  <td class="mono muted" v-if="!selectedItem">{{ item.id }}</td>
                  <td class="mono name-cell">
                    <span class="badge-method" :class="'m-' + item.method.toLowerCase()">{{ item.method }}</span>
                    <span class="path-text" :title="item.path">{{ getPathName(item.path) }}</span>
                  </td>
                  <td v-if="!selectedItem"><span class="badge-status" :class="statusClass(item.status)">{{ item.status }}</span></td>
                  <td class="mono" :class="item.latency > 1000 ? 'text-warn' : ''" v-if="!selectedItem">
                    {{ item.latency > 0 ? item.latency + 'ms' : '-' }}
                  </td>
                  <td class="mono muted" v-if="!selectedItem">{{ item.size }}</td>
                  <td class="mono muted" v-if="!selectedItem">{{ item.time }}</td>
                </tr>
                <tr v-if="filteredHistory.length === 0">
                  <td :colspan="selectedItem ? 1 : 6" class="empty-row">
                    {{ proxyRunning ? '等待请求流入...' : '启动代理后开始捕获' }}
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>

        <!-- 右侧：Chrome DevTools 风格的详情面板 -->
        <div class="history-detail-view" v-if="selectedItem">
          <div class="devtools-header">
            <button class="close-btn" @click="selectedId = null">✕</button>
            <div class="devtools-tabs">
              <button :class="{active: devtoolsTab==='headers'}" @click="devtoolsTab='headers'">标头 (Headers)</button>
              <button :class="{active: devtoolsTab==='payload'}" @click="devtoolsTab='payload'">载荷 (Payload)</button>
              <button :class="{active: devtoolsTab==='response'}" @click="devtoolsTab='response'">响应 (Response)</button>
              <button :class="{active: devtoolsTab==='raw'}" @click="devtoolsTab='raw'">原始数据 (Raw)</button>
            </div>
            <button class="btn-sm btn-purple repeater-btn" @click="sendToRepeater(selectedItem)">⏩ Repeater</button>
          </div>

          <div class="devtools-content">
            <!-- Headers Tab -->
            <div v-if="devtoolsTab==='headers'" class="dt-section">
              <div class="dt-group">
                <h3>常规 (General)</h3>
                <div class="dt-row"><span class="dt-key">请求网址:</span><span class="dt-val">http://{{ selectedItem.host }}{{ selectedItem.path }}</span></div>
                <div class="dt-row"><span class="dt-key">请求方法:</span><span class="dt-val">{{ selectedItem.method }}</span></div>
                <div class="dt-row"><span class="dt-key">状态代码:</span><span class="dt-val" :class="statusClass(selectedItem.status)">{{ selectedItem.status }}</span></div>
              </div>
              
              <div class="dt-group">
                <h3>响应标头 (Response Headers)</h3>
                <pre class="dt-pre">{{ selectedItem.respHeaders || 'Loading...' }}</pre>
              </div>

              <div class="dt-group">
                <h3>请求标头 (Request Headers)</h3>
                <pre class="dt-pre">{{ selectedItem.reqHeaders || 'Loading...' }}</pre>
              </div>
            </div>

            <!-- Payload Tab -->
            <div v-if="devtoolsTab==='payload'" class="dt-section">
              <div class="dt-group" v-if="Object.keys(parsedQuery).length > 0">
                <h3>查询字符串参数 (Query String Parameters)</h3>
                <table class="dt-table">
                  <tr v-for="(val, key) in parsedQuery" :key="key">
                    <td class="dt-td-key">{{ key }}</td>
                    <td class="dt-td-val">{{ val }}</td>
                  </tr>
                </table>
              </div>
              <div v-else class="dt-empty">没有查询字符串参数</div>
              
              <div class="dt-group" v-if="selectedItem.reqRaw">
                <h3>请求正文 (Request Body)</h3>
                <pre class="dt-pre">{{ selectedItem.reqRaw }}</pre>
              </div>
            </div>

            <!-- Response Tab -->
            <div v-if="devtoolsTab==='response'" class="dt-section">
              <div v-if="parsedResponseJson" class="json-viewer-wrap">
                <vue-json-pretty :data="parsedResponseJson" :deep="3" showIcon showLineNumber :collapsedOnClickBrackets="true" />
              </div>
              <pre v-else class="dt-pre dt-response">{{ selectedItem.respRaw || 'Loading...' }}</pre>
            </div>

            <!-- Raw Tab -->
            <div v-if="devtoolsTab==='raw'" class="dt-section">
              <div class="dt-group">
                <h3>Full Request</h3>
                <pre class="dt-pre">{{ selectedItem.reqHeaders }}\n\n{{ selectedItem.reqRaw }}</pre>
              </div>
              <div class="dt-group">
                <h3>Full Response</h3>
                <pre class="dt-pre">{{ selectedItem.respHeaders }}\n\n{{ selectedItem.respRaw }}</pre>
              </div>
            </div>
          </div>
        </div>
      </section>

      <!-- ════════════ B. Interceptor ════════════ -->
      <section v-show="activeTab === 'interceptor'" class="tab-pane interceptor-pane">
        <div class="interceptor-toolbar">
          <button class="ctrl-btn sys-btn" :class="{'ctrl-on': interceptorOn}"
                  @click="interceptorOn = !interceptorOn">
            <span class="btn-dot" /> 拦截器 {{ interceptorOn ? 'ON (挂起)' : 'OFF (直通)' }}
          </button>
          <button class="ctrl-btn start-btn" @click="forwardRequest" :disabled="!interceptorOn">
            ⏩ Forward
          </button>
          <button class="ctrl-btn danger-btn" @click="dropRequest" :disabled="!interceptorOn">
            🗑 Drop
          </button>
          <span class="interceptor-hint" v-if="interceptorOn">
            {{ interceptedItems.length }} 个请求挂起等待放行
          </span>
        </div>
        <div class="interceptor-body">
          <div class="interceptor-editor" v-if="activeInterceptItem">
            <div class="editor-label">
              <span class="badge-status">{{ activeInterceptItem.type.toUpperCase() }}</span>
              #{{ activeInterceptItem.connId }} - 可编辑报文（修改后 Forward 放行）
            </div>
            <textarea v-model="activeInterceptItem.raw" class="raw-editor" spellcheck="false" />
          </div>
          <div class="interceptor-editor" v-else>
            <div class="editor-label" style="text-align: center; margin-top: 40px; color: var(--text-muted);">
              等待请求拦截...
            </div>
          </div>
        </div>
      </section>

      <!-- ════════════ C. Chaos Rules ════════════ -->
      <section v-show="activeTab === 'rules'" class="tab-pane rules-pane">
        <!-- 左侧规则列表 -->
        <div class="rules-list">
          <div class="rules-list-header">
            <span>规则列表 <span class="muted">({{ enabledCount }}/{{ rules.length }} 启用)</span></span>
            <button class="btn-sm btn-orange" @click="addRule">+ 新建</button>
          </div>
          <div class="rules-scroll">
            <div v-for="rule in rules" :key="rule._id"
                 class="rule-card"
                 :class="{ 'rule-selected': editingRule?._id === rule._id, 'rule-disabled': !rule.enabled }"
                 @click="selectRule(rule)">
              <div class="rule-card-top">
                <span class="rule-name">{{ rule.name || '未命名规则' }}</span>
                <label class="toggle-switch" @click.stop>
                  <input type="checkbox" v-model="rule.enabled" @change="saveRules" />
                  <span class="toggle-track" />
                </label>
              </div>
              <div class="rule-url">
                <span class="method-tiny" :class="'m-' + (rule.match.method||'all').toLowerCase()">
                  {{ rule.match.method || 'ALL' }}
                </span>
                {{ rule.match.url || '匹配所有' }}
              </div>
              <div class="rule-badges">
                <span v-if="rule.inject.delay_ms" class="rbadge rbadge-orange">
                  DELAY {{ rule.inject.delay_ms }}ms
                </span>
                <span v-if="rule.inject.status_code" class="rbadge rbadge-red">
                  {{ rule.inject.status_code }}
                </span>
                <span v-if="rule.inject.body_modify?.find" class="rbadge rbadge-purple">BODY</span>
                <span v-if="rule.inject.close_connection" class="rbadge rbadge-yellow">DROP</span>
              </div>
            </div>
            <div v-if="rules.length === 0" class="rules-empty">
              暂无规则，点击「新建」添加
            </div>
          </div>
        </div>

        <!-- 右侧规则编辑器 -->
        <div class="rule-editor" v-if="editingRule">
          <div class="editor-header">
            <span class="editor-title">
              {{ isNewRule ? '✨ 新建规则' : `✏️ 编辑：${editingRule.name || '未命名'}` }}
            </span>
            <div class="editor-actions">
              <button class="btn-sm btn-green" @click="saveCurrentRule">💾 保存</button>
              <button v-if="!isNewRule" class="btn-sm btn-red" @click="deleteCurrentRule">🗑 删除</button>
              <button class="btn-sm" @click="editingRule = null">✕ 关闭</button>
            </div>
          </div>

          <div class="editor-form">
            <!-- 基础信息 -->
            <div class="form-section">
              <div class="form-row">
                <label class="form-label">规则名称</label>
                <input v-model="editingRule.name" placeholder="模拟登录接口系统繁忙..." class="form-input" />
              </div>
              <div class="form-row two-col">
                <div>
                  <label class="form-label">URL 匹配（前缀 / 精确）</label>
                  <input v-model="editingRule.match.url" placeholder="/api/user/login" class="form-input" />
                </div>
                <div>
                  <label class="form-label">请求方法</label>
                  <select v-model="editingRule.match.method" class="form-input">
                    <option value="">ALL</option>
                    <option>GET</option>
                    <option>POST</option>
                    <option>PUT</option>
                    <option>DELETE</option>
                    <option>PATCH</option>
                  </select>
                </div>
              </div>
            </div>

            <!-- 故障注入模块 -->
            <div class="form-section-title">💥 故障注入</div>

            <!-- 延迟注入 -->
            <div class="inject-block" :class="{active: delayEnabled}">
              <div class="inject-header">
                <label class="inject-toggle">
                  <input type="checkbox" v-model="delayEnabled" />
                  <span class="inject-name">⏱ 延迟注入 (Delay)</span>
                </label>
                <span class="inject-val" v-if="delayEnabled">{{ editingRule.inject.delay_ms }} ms</span>
              </div>
              <div v-if="delayEnabled" class="inject-body">
                <input type="range" v-model.number="editingRule.inject.delay_ms"
                       min="0" max="10000" step="100" class="delay-slider" />
                <input type="number" v-model.number="editingRule.inject.delay_ms"
                       min="0" max="30000" class="delay-number" />
                <span class="form-unit">ms</span>
              </div>
            </div>

            <!-- 状态码篡改 -->
            <div class="inject-block" :class="{active: statusEnabled}">
              <div class="inject-header">
                <label class="inject-toggle">
                  <input type="checkbox" v-model="statusEnabled" />
                  <span class="inject-name">🔴 状态码篡改 (Status Override)</span>
                </label>
              </div>
              <div v-if="statusEnabled" class="inject-body">
                <input type="number" v-model.number="editingRule.inject.status_code"
                       min="100" max="599" placeholder="503" class="form-input" style="width:120px" />
                <span class="form-hint">常用: 500, 503, 429, 404, 401</span>
              </div>
            </div>

            <!-- Body 替换 -->
            <div class="inject-block" :class="{active: bodyEnabled}">
              <div class="inject-header">
                <label class="inject-toggle">
                  <input type="checkbox" v-model="bodyEnabled" />
                  <span class="inject-name">🔀 响应体替换 (Body Modify)</span>
                </label>
              </div>
              <div v-if="bodyEnabled" class="inject-body body-edit">
                <div>
                  <label class="form-label">查找 (Find)</label>
                  <input v-model="editingRule.inject.body_modify.find"
                         placeholder='"success":true' class="form-input" />
                </div>
                <div>
                  <label class="form-label">替换为 (Replace)</label>
                  <input v-model="editingRule.inject.body_modify.replace"
                         placeholder='"success":false,"msg":"系统繁忙"' class="form-input" />
                </div>
              </div>
            </div>

            <!-- 连接中断 -->
            <div class="inject-block" :class="{active: editingRule.inject.close_connection}">
              <div class="inject-header">
                <label class="inject-toggle">
                  <input type="checkbox" v-model="editingRule.inject.close_connection" />
                  <span class="inject-name">🔌 连接中断 (Drop Connection)</span>
                </label>
                <span v-if="editingRule.inject.close_connection" class="inject-warn">
                  ⚠️ 接收请求后立即关闭 Socket
                </span>
              </div>
            </div>
          </div>
        </div>

        <!-- 空状态 -->
        <div v-else class="rule-editor rule-editor-empty">
          <div class="empty-hint">
            <div class="empty-icon">💥</div>
            <div>从左侧选择规则编辑，或点击「新建」创建一条故障注入规则</div>
          </div>
        </div>
      </section>

      <!-- ════════════ D. Repeater ════════════ -->
      <section v-show="activeTab === 'repeater'" class="tab-pane repeater-pane">
        <div class="repeater-toolbar">
          <div class="target-group">
            <label>Target:</label>
            <input v-model="repeaterTarget" placeholder="http://example.com" class="target-input" />
          </div>
          <button class="ctrl-btn start-btn" @click="sendRepeater" :disabled="repeaterLoading">
            {{ repeaterLoading ? '⏳ Sending...' : '🚀 Send' }}
          </button>
          <button class="btn-sm" @click="repeaterResp = ''">Clear</button>
        </div>
        <div class="repeater-body">
          <div class="repeater-col">
            <div class="repeater-col-header" style="justify-content: space-between; display: flex; align-items: center;">
              <span style="font-weight: 600;">✏️ Request Builder</span>
              <div class="repeater-tabs">
                <button :class="{active: repeaterReqTab==='builder'}" @click="switchRepeaterTab('builder')">Builder</button>
                <button :class="{active: repeaterReqTab==='raw'}" @click="switchRepeaterTab('raw')">Raw</button>
              </div>
            </div>
            
            <textarea v-if="repeaterReqTab === 'raw'" v-model="repeaterReq" class="raw-editor" spellcheck="false"
                      placeholder="POST /api/user/login HTTP/1.1&#10;Host: example.com&#10;Content-Type: application/json&#10;&#10;{&quot;user&quot;: &quot;admin&quot;}" />
                      
            <div v-else class="builder-editor">
              <div class="builder-req-line">
                <select v-model="repeaterMethod" class="builder-select">
                  <option>GET</option><option>POST</option><option>PUT</option><option>DELETE</option><option>PATCH</option><option>OPTIONS</option><option>HEAD</option>
                </select>
                <input v-model="repeaterPath" @input="syncPathToParams" class="builder-input path-input" placeholder="/api/path?query=1" />
                <input v-model="repeaterProto" class="builder-input proto-input" />
              </div>
              
              <div class="builder-section">
                <div class="builder-section-title">
                  <span>Query Params</span>
                  <button class="btn-sm btn-outline" @click="addRepeaterQueryParam">➕ Add</button>
                </div>
                <div class="builder-headers" style="max-height: 180px;">
                  <div v-for="(p, i) in repeaterQueryParams" :key="i" class="header-row">
                    <input type="checkbox" v-model="p.enabled" @change="syncParamsToPath" title="Enable/Disable" style="margin-right: 4px; cursor: pointer;" />
                    <input v-model="p.key" @input="syncParamsToPath" placeholder="Key" class="builder-input hdr-key" />
                    <input v-model="p.val" @input="syncParamsToPath" placeholder="Value" class="builder-input hdr-val" />
                    <button class="btn-icon delete-btn" @click="removeRepeaterQueryParam(i)" title="Remove">✕</button>
                  </div>
                  <div v-if="repeaterQueryParams.length === 0" class="empty-headers" @click="addRepeaterQueryParam">
                    No query parameters. Click here to add one.
                  </div>
                </div>
              </div>
              
              <div class="builder-section">
                <div class="builder-section-title">
                  <span>Headers</span>
                  <button class="btn-sm btn-outline" @click="addRepeaterHeader">➕ Add</button>
                </div>
                <div class="builder-headers">
                  <div v-for="(h, i) in repeaterHeaders" :key="i" class="header-row">
                    <input v-model="h.key" placeholder="Key" class="builder-input hdr-key" />
                    <input v-model="h.val" placeholder="Value" class="builder-input hdr-val" />
                    <button class="btn-icon delete-btn" @click="removeRepeaterHeader(i)" title="Remove">✕</button>
                  </div>
                  <div v-if="repeaterHeaders.length === 0" class="empty-headers" @click="addRepeaterHeader">
                    No headers. Click here to add one.
                  </div>
                </div>
              </div>
              
              <div class="builder-section body-section">
                <div class="builder-section-title">Body</div>
                <textarea v-model="repeaterBody" class="builder-body-editor" spellcheck="false" placeholder="Enter request body here..."></textarea>
              </div>
            </div>
          </div>
          <div class="repeater-col">
            <div class="repeater-col-header" style="justify-content: space-between; display: flex; align-items: center;">
              <div style="display: flex; align-items: center; gap: 8px;">
                <span style="font-weight: 600;">📨 Response</span>
                <span v-if="repeaterStatus" class="badge-status" :class="statusClass(repeaterStatus)">
                  {{ repeaterStatus }}
                </span>
                <span v-if="repeaterMs" class="muted">{{ repeaterMs }}ms</span>
              </div>
              <div class="repeater-tabs">
                <button :class="{active: repeaterRespTab==='pretty'}" @click="repeaterRespTab='pretty'">Pretty</button>
                <button :class="{active: repeaterRespTab==='raw'}" @click="repeaterRespTab='raw'">Raw</button>
              </div>
            </div>
            
            <div v-if="repeaterRespTab === 'pretty' && parsedRepeaterRespJson" class="json-viewer-wrap" style="flex:1; overflow:auto; background:var(--bg-color); padding: 10px;">
              <vue-json-pretty :data="parsedRepeaterRespJson" :deep="3" showIcon showLineNumber :collapsedOnClickBrackets="true" />
            </div>
            <pre v-else-if="repeaterRespTab === 'pretty' && !parsedRepeaterRespJson" class="raw-editor" style="overflow:auto; white-space:pre-wrap">{{ repeaterResp ? 'Response is not valid JSON. View in Raw tab.' : '点击 Send 发送请求...' }}</pre>
            <pre v-else class="raw-editor" style="overflow:auto; white-space:pre-wrap">{{ repeaterResp || '点击 Send 发送请求...' }}</pre>
          </div>
        </div>
      </section>
      
      <!-- ════════════ E. Settings ════════════ -->
      <section v-show="activeTab === 'settings'" class="tab-pane settings-pane">
        <div class="settings-header">
          <h2>⚙️ 系统设置 (Settings)</h2>
        </div>
        <div class="settings-body">
          <div class="settings-card">
            <h3>🔒 HTTPS 代理根证书配置</h3>
            <p class="settings-desc">
              为了拦截并解密 HTTPS 流量，Burp Lite 会动态为你生成私有的 Root CA 证书。
              你需要将此根证书下载并安装到系统中，然后设置为<strong>“始终信任 (Always Trust)”</strong>。
            </p>
            
            <div class="cert-actions">
              <button class="btn-primary cert-btn" @click="downloadCert">
                ⬇️ 下载 Root CA 证书
              </button>
            </div>
            
            <div class="cert-guide">
              <h4>macOS 安装教程：</h4>
              <ol>
                <li>点击上方按钮，将证书下载到桌面 (默认名为 <code>burp-lite-ca.crt</code>)</li>
                <li>双击桌面的证书文件，系统会自动打开 <strong>钥匙串访问 (Keychain Access)</strong></li>
                <li>在钥匙串列表中找到名为 <strong><code>ChaosProxy Root CA</code></strong> 的证书</li>
                <li>双击它，展开 <strong>“信任 (Trust)”</strong> 菜单</li>
                <li>将“使用此证书时”改为 <strong>“始终信任 (Always Trust)”</strong></li>
                <li>关闭窗口并输入系统密码授权。重启浏览器即可生效！</li>
              </ol>
            </div>
          </div>
        </div>
      </section>
    </div>

    <!-- ⑤ 底部日志面板 (可折叠) -->
    <div class="log-panel" :class="{collapsed: !showLog}">
      <div class="log-header">
        <span>📋 Console Log <span class="muted">({{ logLines.length }} lines)</span></span>
        <button class="btn-sm" @click="logLines = []">Clear</button>
      </div>
      <div class="log-body" ref="logBodyEl">
        <div v-for="(line, i) in logLines" :key="i" class="log-line"
             :class="logClass(line)">{{ line }}</div>
        <div v-if="logLines.length === 0" class="log-empty">日志为空</div>
      </div>
    </div>

    <!-- Toast 通知 -->
    <transition name="toast-fade">
      <div v-if="toast.show" class="toast" :class="'toast-' + toast.type">
        {{ toast.msg }}
      </div>
    </transition>

  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import VueJsonPretty from 'vue-json-pretty'
import 'vue-json-pretty/lib/styles.css'

// ── Electron API bridge ───────────────────────────────────────────────────────
const isElectron = typeof window !== 'undefined' && !!window.electronAPI
const api = isElectron ? window.electronAPI : {
  startProxy: async () => ({ ok: false, msg: 'Not in Electron' }),
  stopProxy: async () => ({ ok: false }),
  getProxyStatus: async () => ({ running: false }),
  readRules: async () => ({ ok: true, rules: mockRules() }),
  writeRules: async () => ({ ok: true }),
  setSystemProxy: async () => ({ ok: true }),
  clearSystemProxy: async () => ({ ok: true }),
  onLogLine: () => {},
  onProxyStopped: () => {},
  removeAllListeners: () => {},
  minimize: () => {}, maximize: () => {}, close: () => {},
}

// ── State ─────────────────────────────────────────────────────────────────────
const activeTab       = ref('history')
const proxyRunning    = ref(false)
const proxyPort       = ref(8888)
const systemProxyOn   = ref(false)
const interceptorOn   = ref(false)
const showLog         = ref(false)
const logLines        = ref([])
const logBodyEl       = ref(null)

// History
const historyList     = ref([])
const selectedId      = ref(null)
const historyFilter   = ref('')
const filterChaosOnly = ref(false)
const reqView         = ref('raw')
const respView        = ref('raw')
let historySeq = 0

// Rules
const rules           = ref([])
const editingRule     = ref(null)
const isNewRule       = ref(false)

// Interceptor
const interceptedItems= ref([])
const activeInterceptItem = computed(() => interceptedItems.value[0] || null)

watch(interceptorOn, async () => {
  await fetch('http://127.0.0.1:8888/api/interceptor_toggle', { method: 'POST' })
})

// Repeater
const repeaterTarget  = ref('http://127.0.0.1:8888')
const repeaterReq     = ref('POST /api/user/login HTTP/1.1\r\nHost: api.example.com\r\nContent-Type: application/json\r\n\r\n{"user":"repeater"}')
const repeaterReqTab  = ref('raw')
const repeaterRespTab = ref('pretty')
const repeaterMethod  = ref('GET')
const repeaterPath    = ref('/')
const repeaterProto   = ref('HTTP/1.1')
const repeaterQueryParams = ref([])
const repeaterHeaders = ref([])
const repeaterBody    = ref('')
const repeaterResp    = ref('')
const repeaterStatus  = ref(null)
const repeaterMs      = ref(null)
const repeaterLoading = ref(false)

// Toast
const toast = ref({ show: false, msg: '', type: 'info' })

// Stats
const stats = computed(() => {
  const total    = historyList.value.length
  const injected = historyList.value.filter(h => h.injected).length
  const latencies = historyList.value.map(h => h.latency).filter(Boolean)
  const avgMs = latencies.length
    ? Math.round(latencies.reduce((a, b) => a + b, 0) / latencies.length)
    : 0
  return { total, injected, avgMs }
})

// ── Tab config ────────────────────────────────────────────────────────────────
const tabs = [
  { id: 'history',     icon: '📜', label: 'HTTP History' },
  { id: 'interceptor', icon: '🛑', label: 'Interceptor'  },
  { id: 'rules',       icon: '💥', label: 'Chaos Rules'  },
  { id: 'repeater',    icon: '🔄', label: 'Repeater'     },
  { id: 'settings',    icon: '⚙️', label: 'Settings'     },
]

// ── Computed ──────────────────────────────────────────────────────────────────
const filteredHistory = computed(() => {
  let list = historyList.value
  if (historyFilter.value) {
    const f = historyFilter.value.toLowerCase()
    list = list.filter(h => h.path.toLowerCase().includes(f) || h.host.toLowerCase().includes(f))
  }
  if (filterChaosOnly.value) list = list.filter(h => h.injected)
  return list.slice().reverse()
})

const devtoolsTab = ref('headers')

function decodeChunkedBody(body) {
  if (!/^[0-9a-fA-F]+\r\n/.test(body)) return body
  let decoded = ''
  let pos = 0
  while (pos < body.length) {
    const nextCrLf = body.indexOf('\r\n', pos)
    if (nextCrLf === -1) break
    const chunkHex = body.slice(pos, nextCrLf)
    const chunkSize = parseInt(chunkHex, 16)
    if (isNaN(chunkSize)) return body // fallback if not really chunked
    if (chunkSize === 0) break
    pos = nextCrLf + 2
    decoded += body.slice(pos, pos + chunkSize)
    pos += chunkSize + 2
  }
  return decoded
}

const parsedResponseJson = computed(() => {
  if (!selectedItem.value || !selectedItem.value.respRaw) return null
  try {
    let raw = selectedItem.value.respRaw
    if (selectedItem.value.respHeaders?.toLowerCase().includes('transfer-encoding: chunked')) {
      raw = decodeChunkedBody(raw)
    }
    return JSON.parse(raw)
  } catch (e) {
    return null
  }
})

const parsedRepeaterRespJson = computed(() => {
  if (!repeaterResp.value) return null
  const parts = repeaterResp.value.split(/\r?\n\r?\n/)
  if (parts.length < 2) return null
  const headers = parts[0].toLowerCase()
  let body = parts.slice(1).join('\n\n')
  if (headers.includes('transfer-encoding: chunked')) {
    body = decodeChunkedBody(body)
  }
  try {
    return JSON.parse(body)
  } catch(e) {
    return null
  }
})

const parsedQuery = computed(() => {
  if (!selectedItem.value || !selectedItem.value.path) return {}
  const qmark = selectedItem.value.path.indexOf('?')
  if (qmark === -1) return {}
  const qs = selectedItem.value.path.substring(qmark + 1)
  const obj = {}
  new URLSearchParams(qs).forEach((val, key) => {
    obj[key] = val
  })
  return obj
})

function getPathName(path) {
  if (!path) return '/'
  const qmark = path.indexOf('?')
  let p = qmark === -1 ? path : path.substring(0, qmark)
  const lastSlash = p.lastIndexOf('/')
  return lastSlash !== -1 && lastSlash < p.length - 1 ? p.substring(lastSlash + 1) : p
}

const selectedItem = computed(() =>
  historyList.value.find(h => h.id === selectedId.value) || null
)

watch(selectedId, async (newVal) => {
  if (!newVal) return
  const item = historyList.value.find(h => h.id === newVal)
  if (!item || !item.connId) return

  // Fetch full payloads from disk when selected
  if (api.readPayload) {
    try {
      const reqContent = await api.readPayload(item.connId, 'req')
      if (reqContent) {
        const parts = reqContent.split('\r\n\r\n')
        item.reqHeaders = parts[0]
        item.reqRaw = parts.length > 1 ? parts.slice(1).join('\r\n\r\n') : ''
      } else {
        item.reqRaw = 'Failed to load or empty payload'
      }
      
      const respContent = await api.readPayload(item.connId, 'resp')
      if (respContent) {
        const parts = respContent.split('\r\n\r\n')
        item.respHeaders = parts[0]
        item.respRaw = parts.length > 1 ? parts.slice(1).join('\r\n\r\n') : ''
      } else {
        item.respRaw = 'Failed to load or empty payload'
      }
    } catch (e) {
      console.error('Failed to read payload', e)
    }
  }
})

const enabledCount = computed(() => rules.value.filter(r => r.enabled).length)

// Rule editor computed toggles
const delayEnabled = computed({
  get: () => editingRule.value ? editingRule.value.inject.delay_ms > 0 : false,
  set: (v) => {
    if (!editingRule.value) return
    editingRule.value.inject.delay_ms = v ? 2000 : 0
  }
})

const statusEnabled = computed({
  get: () => editingRule.value ? !!editingRule.value.inject.status_code : false,
  set: (v) => {
    if (!editingRule.value) return
    editingRule.value.inject.status_code = v ? 500 : null
  }
})

const bodyEnabled = computed({
  get: () => editingRule.value
    ? !!(editingRule.value.inject.body_modify?.find)
    : false,
  set: (v) => {
    if (!editingRule.value) return
    editingRule.value.inject.body_modify = v
      ? { find: '', replace: '' }
      : null
  }
})

// ── Helpers ───────────────────────────────────────────────────────────────────
function statusClass(code) {
  if (!code) return ''
  if (code >= 500) return 's-5xx'
  if (code >= 400) return 's-4xx'
  if (code >= 300) return 's-3xx'
  return 's-2xx'
}

function logClass(line) {
  const l = line.toLowerCase()
  if (l.includes('error') || l.includes('err]') || l.includes('fail')) return 'log-err'
  if (l.includes('chaos') || l.includes('inject') || l.includes('delay')) return 'log-chaos'
  if (l.includes('warn') || l.includes('drop')) return 'log-warn'
  return 'log-info'
}

function showToast(msg, type = 'info', ms = 3000) {
  toast.value = { show: true, msg, type }
  setTimeout(() => { toast.value.show = false }, ms)
}

function uid() {
  return Math.random().toString(36).slice(2, 10)
}

function nowTime() {
  return new Date().toLocaleTimeString('zh-CN', { hour12: false })
}

// ── Proxy Control ─────────────────────────────────────────────────────────────
async function toggleProxy() {
  if (proxyRunning.value) {
    const res = await api.stopProxy()
    if (res.ok) {
      proxyRunning.value = false
      showToast('代理已停止', 'warn')
    } else {
      showToast(res.msg || '停止失败', 'error')
    }
  } else {
    const res = await api.startProxy({ port: proxyPort.value })
    if (res.ok) {
      proxyRunning.value = true
      showToast(`代理已启动，监听 :${proxyPort.value}`, 'success')
    } else {
      showToast(res.msg || '启动失败', 'error')
    }
  }
}

async function toggleSystemProxy() {
  if (!proxyRunning.value) return
  if (systemProxyOn.value) {
    await api.clearSystemProxy()
    systemProxyOn.value = false
    showToast('系统代理已关闭', 'warn')
  } else {
    const res = await api.setSystemProxy({ host: '127.0.0.1', port: proxyPort.value })
    if (res.ok) {
      systemProxyOn.value = true
      showToast(`系统代理已设置 → 127.0.0.1:${proxyPort.value}`, 'success')
    } else {
      showToast(res.msg || '设置系统代理失败', 'error')
    }
  }
}

// ── Rules ─────────────────────────────────────────────────────────────────────
function emptyRule() {
  return {
    _id: uid(),
    name: '',
    enabled: true,
    match: { url: '', method: '' },
    inject: {
      delay_ms: 0,
      status_code: null,
      body_modify: null,
      close_connection: false,
    }
  }
}

async function loadRules() {
  const res = await api.readRules()
  if (res.ok) {
    rules.value = res.rules.map(r => ({ ...r, _id: uid() }))
  }
}

async function saveRules() {
  await api.writeRules(rules.value)
}

function addRule() {
  const r = emptyRule()
  rules.value.push(r)
  editingRule.value = r
  isNewRule.value = true
}

function selectRule(rule) {
  // Deep clone to allow cancellation without side effects
  editingRule.value = JSON.parse(JSON.stringify(rule))
  isNewRule.value = false
}

async function saveCurrentRule() {
  if (!editingRule.value) return
  const idx = rules.value.findIndex(r => r._id === editingRule.value._id)
  if (idx >= 0) {
    rules.value[idx] = { ...editingRule.value }
  } else {
    rules.value.push({ ...editingRule.value })
  }
  await saveRules()
  isNewRule.value = false
  showToast('规则已保存', 'success')
}

async function deleteCurrentRule() {
  if (!editingRule.value) return
  rules.value = rules.value.filter(r => r._id !== editingRule.value._id)
  editingRule.value = null
  await saveRules()
  showToast('规则已删除', 'warn')
}

// ── History ───────────────────────────────────────────────────────────────────
function clearHistory() {
  historyList.value = []
  selectedId.value = null
  historySeq = 0
}

function pushHistoryEntry(entry) {
  historySeq++
  const id = entry.id || historySeq
  historyList.value.push({ time: nowTime(), ...entry, id })
  if (historyList.value.length > 1000) historyList.value.shift()
}

/**
 * Parse a log line from the C proxy stdout into a history entry.
 *
 * The C proxy emits a structured line for every completed request:
 *   PROXY GET /gateway?foo=1 200 1234 localhost
 *   ^     ^   ^              ^   ^    ^
 *   tag   method path        status size host
 *
 * Other lines are logger.c coloured output – shown in the log panel only.
 */
function parseLogLine(rawLine) {
  // Strip ANSI escape codes (e.g. \033[32m ... \033[0m)
  // eslint-disable-next-line no-control-regex
  const line = rawLine.replace(/\x1b\[[0-9;]*m/g, '').trim()

  logLines.value.push(line)
  scrollLog()

  // Structured line: "PROXY METHOD path statusCode sizeBytes host connId"
  const m = line.match(/^PROXY\s+(\w+)\s+(\S+)\s+(\d+)\s+(\d+)\s*(\S*)\s*(\d*)/)
  if (m) {
    const isChaos = /chaos|inject|delay|drop/i.test(line)
    const connId = m[6] ? parseInt(m[6]) : null
    pushHistoryEntry({
      connId:      connId,
      method:      m[1].toUpperCase(),
      host:        m[5] || 'localhost',
      path:        m[2],
      status:      parseInt(m[3]),
      latency:     0,
      size:        formatBytes(parseInt(m[4])),
      injected:    isChaos,
      reqHeaders:  `${m[1]} ${m[2]} HTTP/1.1\r\nHost: ${m[5] || 'localhost'}`,
      reqRaw:      '',
      respHeaders: `HTTP/1.1 ${m[3]}`,
      respRaw:     '',
    })
    return
  }
  
  const interceptMatch = line.match(/^INTERCEPT\s+(REQ|RESP)\s+(\d+)/)
  if (interceptMatch) {
    const type = interceptMatch[1].toLowerCase()
    const connId = parseInt(interceptMatch[2])
    api.readPayload(connId, type).then(payload => {
      interceptedItems.value.push({
        id: uid(),
        connId,
        type,
        raw: payload
      })
      if (activeTab.value !== 'interceptor') {
        activeTab.value = 'interceptor'
      }
    }).catch(e => console.error(e))
    return
  }

  // Fallback: also try to pick up CHAOS log lines and mark last entry as injected
  if (/\[CHAOS\]/i.test(line) && historyList.value.length > 0) {
    const last = historyList.value[historyList.value.length - 1]
    last.injected = true
  }
}

function formatBytes(n) {
  if (!n || n === 0) return '-'
  if (n < 1024) return n + ' B'
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB'
  return (n / 1024 / 1024).toFixed(1) + ' MB'
}

function scrollLog() {
  nextTick(() => {
    if (logBodyEl.value) {
      logBodyEl.value.scrollTop = logBodyEl.value.scrollHeight
    }
  })
}

// ── Interceptor ───────────────────────────────────────────────────────────────
async function forwardRequest() {
  if (!activeInterceptItem.value) return
  const item = activeInterceptItem.value
  
  // Write the modified raw text back to disk via IPC
  await api.writePayload(item.connId, item.type, item.raw)
  
  // Tell proxy to resume
  await fetch(`http://127.0.0.1:8888/api/resume?id=${item.connId}&type=${item.type}`, { method: 'POST' })
  
  interceptedItems.value.shift()
  showToast('报文已修改并放行', 'success')
}

async function dropRequest() {
  if (!activeInterceptItem.value) return
  // To drop, we just don't tell the proxy to resume, and maybe send a close signal?
  // For now, we'll just remove it from UI, it will hang until timeout
  interceptedItems.value.shift()
  showToast('请求已丢弃（挂起超时断开）', 'warn')
}

// ── Repeater ──────────────────────────────────────────────────────────────────

function switchRepeaterTab(tab) {
  if (repeaterReqTab.value === tab) return
  if (tab === 'builder') {
    const text = repeaterReq.value || ''
    const parts = text.split(/\r?\n\r?\n/)
    const headStr = parts[0] || ''
    const bodyStr = parts.slice(1).join('\n\n') || ''
    const lines = headStr.split(/\r?\n/)
    const reqLine = lines[0] || ''
    const [method, path, proto] = reqLine.trim().split(/\s+/)
    repeaterMethod.value = method || 'GET'
    repeaterPath.value = path || '/'
    repeaterProto.value = proto || 'HTTP/1.1'
    syncPathToParams()
    
    const hdrs = []
    for (let i = 1; i < lines.length; i++) {
      const l = lines[i].trim()
      if (!l) continue
      const idx = l.indexOf(':')
      if (idx > 0) {
        hdrs.push({ key: l.substring(0, idx).trim(), val: l.substring(idx + 1).trim() })
      } else {
        hdrs.push({ key: l, val: '' })
      }
    }
    repeaterHeaders.value = hdrs
    repeaterBody.value = bodyStr
  } else {
    let raw = `${repeaterMethod.value} ${repeaterPath.value} ${repeaterProto.value}\r\n`
    for (const h of repeaterHeaders.value) {
      if (h.key) raw += `${h.key}: ${h.val}\r\n`
    }
    raw += '\r\n' + repeaterBody.value
    repeaterReq.value = raw
  }
  repeaterReqTab.value = tab
}

function syncPathToParams() {
  const urlParts = repeaterPath.value.split('?')
  const params = []
  if (urlParts.length > 1) {
    const qs = urlParts.slice(1).join('?')
    if (qs) {
      const pairs = qs.split('&')
      for (const p of pairs) {
        if (!p) continue
        const [k, v] = p.split('=')
        params.push({ key: decodeURIComponent(k || ''), val: decodeURIComponent(v || ''), enabled: true })
      }
    }
  }
  repeaterQueryParams.value = params
}

function syncParamsToPath() {
  const urlParts = repeaterPath.value.split('?')
  let base = urlParts[0] || '/'
  const activeParams = repeaterQueryParams.value.filter(p => p.enabled && p.key)
  if (activeParams.length > 0) {
    const qs = activeParams.map(p => `${encodeURIComponent(p.key)}=${encodeURIComponent(p.val || '')}`).join('&')
    repeaterPath.value = base + '?' + qs
  } else {
    repeaterPath.value = base
  }
}

function addRepeaterQueryParam() {
  repeaterQueryParams.value.push({ key: '', val: '', enabled: true })
  syncParamsToPath()
}

function removeRepeaterQueryParam(idx) {
  repeaterQueryParams.value.splice(idx, 1)
  syncParamsToPath()
}

function addRepeaterHeader() {
  repeaterHeaders.value.push({ key: '', val: '' })
}

function removeRepeaterHeader(idx) {
  repeaterHeaders.value.splice(idx, 1)
}

async function sendRepeater() {
  // Ensure we sync from builder to raw before sending if in builder mode
  if (repeaterReqTab.value === 'builder') {
    switchRepeaterTab('raw')
    switchRepeaterTab('builder') // just to trigger the build but stay in builder
    // Wait, switchRepeaterTab('raw') changes to raw. We can just build it manually.
    let raw = `${repeaterMethod.value} ${repeaterPath.value} ${repeaterProto.value}\r\n`
    for (const h of repeaterHeaders.value) {
      if (h.key) raw += `${h.key}: ${h.val}\r\n`
    }
    raw += '\r\n' + repeaterBody.value
    repeaterReq.value = raw
  }
  repeaterLoading.value = true
  repeaterResp.value = ''
  repeaterStatus.value = null
  repeaterMs.value = null

  if (window.electronAPI && window.electronAPI.sendRawRequest) {
    const res = await window.electronAPI.sendRawRequest(8888, repeaterReq.value)
    if (res.ok) {
      repeaterResp.value = res.response
      repeaterMs.value = res.latency
      // extract status code from response string
      const match = res.response.match(/^HTTP\/1\.\d\s+(\d+)/)
      if (match) {
        repeaterStatus.value = parseInt(match[1])
      } else {
        repeaterStatus.value = 0
      }
    } else {
      repeaterResp.value = 'Error: ' + res.msg
      repeaterStatus.value = 500
      repeaterMs.value = res.latency
    }
  } else {
    // Web environment fallback
    await new Promise(r => setTimeout(r, 600 + Math.random() * 400))
    repeaterStatus.value = 503
    repeaterMs.value = Math.round(600 + Math.random() * 400)
    repeaterResp.value = 'HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: 42\r\n\r\n{"code":5,"msg":"chaos ok","success":false}'
  }
  
  repeaterLoading.value = false
}

// ── Send to Repeater ──────────────────────────────────────────────────────────
function sendToRepeater(item) {
  repeaterReq.value = (item.reqHeaders || '') + '\r\n\r\n' + (item.reqRaw || '')
  activeTab.value = 'repeater'
  repeaterReqTab.value = '' // clear to force switch
  switchRepeaterTab('builder')
}

// ── Settings ──────────────────────────────────────────────────────────────────
async function downloadCert() {
  if (window.electronAPI && window.electronAPI.downloadCert) {
    const res = await window.electronAPI.downloadCert()
    if (res.ok) {
      showToast('✅ 证书已成功保存到桌面，请按照教程安装并信任！', 'info')
    } else if (res.msg !== 'Cancelled by user') {
      showToast('❌ 下载失败: ' + res.msg, 'error')
    }
  } else {
    showToast('Web 模式不支持下载，请在 Electron 桌面端使用', 'warn')
  }
}

// ── Mock data (browser mode) ──────────────────────────────────────────────────
function mockRules() {
  return [
    {
      name: '模拟登录接口系统繁忙',
      enabled: true,
      match: { url: '/api/user/login', method: 'POST' },
      inject: {
        delay_ms: 2000,
        status_code: 503,
        body_modify: { find: '"success":true', replace: '"success":false,"msg":"系统繁忙，请稍后重试"' },
        close_connection: false,
      }
    },
    {
      name: '订单列表慢查询',
      enabled: true,
      match: { url: '/api/order/list', method: '' },
      inject: { delay_ms: 5000, status_code: null, body_modify: null, close_connection: false }
    },
  ]
}

function seedDemoHistory() {
  const demo = [
    { method: 'POST', host: 'api.example.com', path: '/api/user/login', status: 503, latency: 2150, size: '1.1 KB', injected: true,
      reqHeaders: 'POST /api/user/login HTTP/1.1\r\nHost: api.example.com\r\nContent-Type: application/json',
      reqRaw: '{"user":"admin","pass":"secret"}',
      respHeaders: 'HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json',
      respRaw: '{"code":5,"msg":"系统繁忙，请稍后重试","success":false}' },
    { method: 'GET', host: 'api.example.com', path: '/api/order/list', status: 200, latency: 5012, size: '4.8 KB', injected: true,
      reqHeaders: 'GET /api/order/list HTTP/1.1\r\nHost: api.example.com',
      reqRaw: '',
      respHeaders: 'HTTP/1.1 200 OK\r\nContent-Type: application/json',
      respRaw: '{"code":0,"data":[{"id":101,"price":99}]}' },
    { method: 'GET', host: 'api.example.com', path: '/api/ping', status: 200, latency: 12, size: '310 B', injected: false,
      reqHeaders: 'GET /api/ping HTTP/1.1\r\nHost: api.example.com',
      reqRaw: '',
      respHeaders: 'HTTP/1.1 200 OK',
      respRaw: '{"code":0,"msg":"pong"}' },
    { method: 'POST', host: 'pay.example.com', path: '/api/payment/charge', status: 429, latency: 88, size: '220 B', injected: false,
      reqHeaders: 'POST /api/payment/charge HTTP/1.1\r\nHost: pay.example.com',
      reqRaw: '{"amount":100}',
      respHeaders: 'HTTP/1.1 429 Too Many Requests',
      respRaw: '{"code":429,"msg":"rate limit exceeded"}' },
  ]
  demo.forEach(d => pushHistoryEntry(d))
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
onMounted(async () => {
  await loadRules()

  if (isElectron) {
    // Subscribe to log stream from C proxy
    api.onLogLine(parseLogLine)
    api.onProxyStopped(({ code }) => {
      proxyRunning.value = false
      systemProxyOn.value = false
      showToast(`代理已退出 (code: ${code})`, 'warn')
    })
    // Sync initial status
    const status = await api.getProxyStatus()
    proxyRunning.value = status.running
  } else {
    // Browser demo: inject fake history + log
    seedDemoHistory()
    logLines.value = [
      '[INFO]  Chaos-Proxy v1.0 starting on :8888',
      '[INFO]  Loaded 2 rules from rules.json',
      '[PROXY] POST /api/user/login -> connected to api.example.com:80',
      '[CHAOS] Rule matched: 模拟登录接口系统繁忙 → delay=2000ms, status=503',
      '[PROXY] GET /api/order/list -> delay=5000ms applied',
    ]
  }
})

onUnmounted(() => {
  if (isElectron) {
    api.removeAllListeners('log:line')
    api.removeAllListeners('proxy:stopped')
  }
})
</script>

<style scoped>
/* ─── Layout ──────────────────────────────────────────────────────────────── */
.app {
  height: 100vh;
  display: flex;
  flex-direction: column;
  background: var(--bg);
  overflow: hidden;
}

/* ─── Titlebar ────────────────────────────────────────────────────────────── */
.titlebar {
  height: 38px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 16px;
  flex-shrink: 0;
}

.titlebar-left { display: flex; gap: 6px; align-items: center; }

.wc {
  width: 12px; height: 12px;
  border-radius: 50%;
  border: none;
  cursor: pointer;
  transition: opacity 0.15s;
}
.wc:hover { opacity: 0.75; }
.wc-close  { background: #ff5f57; }
.wc-min    { background: #febc2e; }
.wc-max    { background: #28c840; }

.titlebar-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-2);
  letter-spacing: 0.03em;
}
.tb-logo { font-size: 14px; }
.tb-version { font-size: 11px; color: var(--text-muted); font-family: var(--font-mono); }

/* ─── Topbar ──────────────────────────────────────────────────────────────── */
.topbar {
  height: 48px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 16px;
  gap: 12px;
  flex-shrink: 0;
}

.topbar-left  { display: flex; align-items: center; gap: 12px; }
.topbar-right { display: flex; align-items: center; gap: 8px; }

.status-pill {
  display: flex; align-items: center; gap: 6px;
  padding: 4px 10px;
  border-radius: 99px;
  font-size: 12px;
  font-weight: 600;
  font-family: var(--font-mono);
  border: 1px solid var(--border-2);
  background: var(--bg-2);
}
.status-pill.status-on  { border-color: var(--green); color: var(--green); background: var(--green-dim); }
.status-pill.status-off { color: var(--text-muted); }

.status-dot {
  width: 7px; height: 7px; border-radius: 50%;
  background: currentColor;
}
.status-on .status-dot { box-shadow: 0 0 6px currentColor; animation: pulse 2s infinite; }

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50%       { opacity: 0.5; }
}

.stat-chips { display: flex; gap: 6px; }
.stat-chip {
  display: flex; flex-direction: column; align-items: center;
  padding: 2px 10px;
  background: var(--bg-2);
  border: 1px solid var(--border);
  border-radius: 6px;
  min-width: 60px;
}
.stat-chip.chaos-chip { border-color: var(--orange-dim); }
.chip-val { font-size: 14px; font-weight: 700; line-height: 1.2; }
.chip-lbl { font-size: 10px; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.05em; }
.chaos-chip .chip-val { color: var(--orange); }

.port-group {
  display: flex; align-items: center; gap: 6px;
  font-size: 12px; color: var(--text-2);
}
.port-input {
  width: 68px;
  text-align: center;
  padding: 4px 6px;
  font-size: 13px;
}

.ctrl-btn {
  display: flex; align-items: center; gap: 6px;
  padding: 6px 14px;
  border-radius: 6px;
  border: 1px solid var(--border-2);
  background: var(--bg-2);
  color: var(--text-2);
  font-size: 12px;
  font-weight: 600;
  transition: all 0.15s;
  cursor: pointer;
}
.ctrl-btn:hover  { background: var(--bg-3); border-color: var(--border-2); color: var(--text); }
.ctrl-btn:disabled { opacity: 0.4; cursor: not-allowed; }

.btn-dot {
  width: 7px; height: 7px; border-radius: 50%;
  background: var(--text-muted);
  transition: all 0.15s;
}

.sys-btn.ctrl-on { background: var(--green-dim); border-color: var(--green); color: var(--green); }
.sys-btn.ctrl-on .btn-dot { background: var(--green); box-shadow: 0 0 6px var(--green); }

.start-btn { background: var(--purple-dim); border-color: var(--purple); color: #c4b5fd; font-weight: 700; }
.start-btn:hover { background: rgba(139,92,246,0.25); }
.start-btn.stop-mode { background: var(--red-dim); border-color: var(--red); color: #fca5a5; }

.danger-btn { border-color: var(--red-dim); color: var(--red); }
.danger-btn:hover { background: var(--red-dim); }

/* ─── Tab Nav ─────────────────────────────────────────────────────────────── */
.tab-nav {
  display: flex;
  align-items: center;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  padding: 0 8px;
  flex-shrink: 0;
}

.tab-btn {
  display: flex; align-items: center; gap: 5px;
  padding: 0 16px;
  height: 38px;
  background: transparent;
  border: none;
  border-bottom: 2px solid transparent;
  color: var(--text-muted);
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
  position: relative;
}
.tab-btn:hover { color: var(--text-2); }
.tab-btn.active { color: var(--text); border-bottom-color: var(--orange); }
.tab-icon { font-size: 13px; }

.tab-badge {
  background: rgba(255,255,255,0.1);
  color: var(--text-2);
  padding: 1px 5px;
  border-radius: 99px;
  font-size: 10px;
  font-family: var(--font-mono);
}
.tab-badge-warn { background: var(--orange-dim); color: var(--orange); }

.tab-nav-right { margin-left: auto; }

.log-toggle {
  padding: 4px 10px;
  border-radius: 5px;
  border: 1px solid var(--border);
  background: transparent;
  color: var(--text-muted);
  font-size: 11px;
  cursor: pointer;
  transition: all 0.15s;
}
.log-toggle.active { background: var(--bg-2); color: var(--text-2); border-color: var(--border-2); }

/* ─── Workspace ───────────────────────────────────────────────────────────── */
.workspace {
  flex: 1;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.tab-pane {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

/* ─── Common Toolbar ──────────────────────────────────────────────────────── */
.pane-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 7px 12px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.toolbar-btn {
  padding: 4px 10px;
  border-radius: 5px;
  border: 1px solid var(--border-2);
  background: var(--bg-2);
  color: var(--text-2);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}

.filter-label {
  display: flex; align-items: center; gap: 6px;
  font-size: 12px; color: var(--text-muted);
}
.filter-input {
  width: 200px;
  padding: 4px 8px;
  font-size: 12px;
}

/* ─── Request Table ───────────────────────────────────────────────────────── */
.req-table-wrap {
  flex: 1;
  overflow-y: auto;
  min-height: 0;
}

.req-table {
  width: 100%;
  table-layout: fixed;
  border-collapse: collapse;
  font-size: 12px;
}

.req-table th {
  background: var(--bg-1);
  color: var(--text-muted);
  padding: 6px 10px;
  font-weight: 600;
  text-align: left;
  position: sticky; top: 0; z-index: 1;
  border-bottom: 1px solid var(--border);
  white-space: nowrap;
}

.req-table td {
  padding: 5px 10px;
  border-bottom: 1px solid rgba(255,255,255,0.035);
  vertical-align: middle;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.req-table tbody tr {
  cursor: pointer;
  transition: background 0.1s;
}
.req-table tbody tr:hover    { background: var(--bg-hover); }
.req-table tbody tr.selected { background: rgba(139,92,246,0.12) !important; }
.req-table tbody tr.chaos-row td:first-child { border-left: 2px solid var(--orange); }

.mono  { font-family: var(--font-mono); }
.muted { color: var(--text-muted); }
.text-warn { color: var(--yellow); }
.ellipsis { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

.badge-method {
  display: inline-block;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 10px;
  font-weight: 700;
  font-family: var(--font-mono);
  letter-spacing: 0.05em;
  flex-shrink: 0;
}
.m-get     { background: var(--cyan-dim);   color: var(--cyan);   }
.m-post    { background: var(--orange-dim); color: var(--orange); }
.m-put     { background: var(--yellow-dim); color: var(--yellow); }
.m-delete  { background: var(--red-dim);    color: var(--red);    }
.m-patch   { background: var(--purple-dim); color: var(--purple); }
.m-connect { background: var(--purple-dim); color: var(--purple); }
.m-options { background: var(--bg-3);       color: var(--text-muted); }
.m-all     { background: var(--bg-3);       color: var(--text-muted); }

.badge-status {
  display: inline-block;
  padding: 1px 6px;
  border-radius: 3px;
  font-size: 11px;
  font-weight: 700;
  font-family: var(--font-mono);
}
.s-2xx { background: var(--green-dim);  color: var(--green); }
.s-3xx { background: var(--cyan-dim);   color: var(--cyan);  }
.s-4xx { background: var(--yellow-dim); color: var(--yellow); }
.s-5xx { background: var(--red-dim);    color: var(--red); }

.empty-row {
  text-align: center;
  padding: 32px !important;
  color: var(--text-muted);
  font-style: italic;
}

/* ── History Pane (Chrome DevTools Style) ─────────────────────────────────── */
.history-pane {
  display: flex;
  flex-direction: column;
  height: 100%;
}
.history-pane.split-layout {
  flex-direction: row;
}

/* Left: List View */
.history-list-view {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-width: 300px;
  overflow: hidden;
  border-right: 1px solid var(--border);
}

.req-table-wrap {
  flex: 1;
  overflow-y: auto;
}

.name-cell {
  display: flex;
  align-items: center;
  gap: 8px;
  overflow: hidden;
  white-space: nowrap;
}
.path-text {
  overflow: hidden;
  text-overflow: ellipsis;
}
.is-error {
  color: var(--danger) !important;
}

/* Right: DevTools Inspector */
.history-detail-view {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 400px;
  background: var(--bg-card);
}

.devtools-header {
  display: flex;
  align-items: center;
  border-bottom: 1px solid var(--border);
  background: var(--bg-dark);
  padding: 0 8px;
}

.close-btn {
  background: transparent;
  border: none;
  color: var(--text-muted);
  cursor: pointer;
  padding: 8px;
  font-size: 16px;
  margin-right: 8px;
}
.close-btn:hover { color: var(--text); }

.devtools-tabs {
  display: flex;
  flex: 1;
  gap: 2px;
}
.devtools-tabs button {
  background: transparent;
  border: none;
  color: var(--text-muted);
  padding: 10px 16px;
  cursor: pointer;
  border-bottom: 2px solid transparent;
  font-size: 13px;
  transition: all 0.2s;
}
.devtools-tabs button:hover {
  color: var(--text);
  background: rgba(255,255,255,0.05);
}
.devtools-tabs button.active {
  color: var(--primary);
  border-bottom: 2px solid var(--primary);
}

.devtools-content {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
}

.dt-section {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.dt-group h3 {
  margin: 0 0 8px 0;
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
  border-bottom: 1px solid var(--border);
  padding-bottom: 4px;
}

.dt-row {
  display: flex;
  font-size: 13px;
  line-height: 1.6;
  font-family: var(--font-mono);
}
.dt-key {
  font-weight: bold;
  color: var(--text-muted);
  width: 120px;
  flex-shrink: 0;
}
.dt-val {
  color: var(--text);
  word-break: break-all;
}

.dt-pre {
  background: var(--bg-dark);
  padding: 12px;
  border-radius: 4px;
  border: 1px solid var(--border);
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-muted);
  white-space: pre-wrap;
  word-break: break-all;
  margin: 0;
}
.dt-response {
  color: var(--text);
}

.dt-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
  font-family: var(--font-mono);
}
.dt-td-key {
  width: 30%;
  padding: 6px;
  border-bottom: 1px solid var(--border);
  color: var(--primary);
}
.dt-td-val {
  padding: 6px;
  border-bottom: 1px solid var(--border);
  color: var(--text);
  word-break: break-all;
}
.dt-empty {
  font-size: 13px;
  color: var(--text-muted);
  font-style: italic;
}
.repeater-tabs {
  display: inline-flex;
  background: rgba(0,0,0,0.25);
  border-radius: 6px;
  padding: 3px;
  border: 1px solid rgba(255,255,255,0.05);
}
.repeater-tabs button {
  background: transparent;
  border: none;
  color: var(--text-muted);
  padding: 4px 14px;
  font-size: 12px;
  font-weight: 500;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
}
.repeater-tabs button:hover {
  color: #fff;
}
.repeater-tabs button.active {
  color: #fff;
  background: var(--panel-bg);
  box-shadow: 0 1px 3px rgba(0,0,0,0.3);
}
.builder-editor {
  display: flex;
  flex-direction: column;
  flex: 1;
  overflow: hidden;
  background: var(--bg-color);
}
.builder-req-line {
  display: flex;
  padding: 12px;
  gap: 8px;
  border-bottom: 1px solid var(--border-color);
  background: rgba(255,255,255,0.01);
}
.builder-input, .builder-select {
  background: rgba(255,255,255,0.04);
  border: 1px solid rgba(255,255,255,0.1);
  color: var(--text-main);
  padding: 8px 12px;
  border-radius: 6px;
  font-family: 'Fira Code', Consolas, monospace;
  font-size: 13px;
  outline: none;
  transition: border-color 0.2s, background 0.2s;
}
.builder-input:focus, .builder-select:focus {
  border-color: #4a90e2;
  background: rgba(255,255,255,0.08);
}
.builder-select { width: 100px; font-weight: bold; color: #61afef; cursor: pointer; }
.path-input { flex: 1; }
.proto-input { width: 110px; color: #e5c07b; }

.builder-section {
  display: flex;
  flex-direction: column;
  border-bottom: 1px solid var(--border-color);
}
.builder-section-title {
  padding: 8px 12px;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-muted);
  background: rgba(0,0,0,0.1);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.btn-outline {
  background: transparent;
  border: 1px solid rgba(255,255,255,0.2);
  color: var(--text-muted);
  border-radius: 4px;
}
.btn-outline:hover {
  background: rgba(255,255,255,0.1);
  color: #fff;
}
.builder-headers {
  max-height: 250px;
  overflow-y: auto;
  padding: 12px;
}
.header-row {
  display: flex;
  gap: 8px;
  margin-bottom: 8px;
  align-items: center;
}
.header-row:last-child {
  margin-bottom: 0;
}
.hdr-key { width: 200px; color: #98c379; }
.hdr-val { flex: 1; color: #abb2bf; }
.delete-btn {
  background: transparent;
  border: none;
  color: #e06c75;
  font-size: 16px;
  cursor: pointer;
  padding: 4px 8px;
  opacity: 0.6;
  transition: opacity 0.2s, transform 0.2s;
}
.delete-btn:hover {
  opacity: 1;
  transform: scale(1.1);
}
.empty-headers {
  color: var(--text-muted);
  font-size: 12px;
  text-align: center;
  padding: 10px;
  cursor: pointer;
  border: 1px dashed rgba(255,255,255,0.1);
  border-radius: 4px;
}
.empty-headers:hover {
  background: rgba(255,255,255,0.05);
  color: #fff;
}
.body-section {
  flex: 1;
  border-bottom: none;
}
.builder-body-editor {
  flex: 1;
  width: 100%;
  resize: none;
  background: transparent;
  color: #abb2bf;
  border: none;
  padding: 15px;
  font-family: 'Fira Code', Consolas, monospace;
  font-size: 13px;
  line-height: 1.5;
  outline: none;
}

/* ─── Settings ────────────────────────────────────────────────────────────── */
.settings-pane {
  padding: 20px;
  overflow-y: auto;
}
.settings-header {
  margin-bottom: 20px;
  border-bottom: 1px solid var(--border-color);
  padding-bottom: 10px;
}
.settings-header h2 {
  margin: 0;
  font-size: 20px;
  color: #fff;
}
.settings-body {
  max-width: 800px;
}
.settings-card {
  background: var(--panel-bg);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  padding: 25px;
  margin-bottom: 20px;
}
.settings-card h3 {
  margin-top: 0;
  color: #fff;
  font-size: 16px;
  margin-bottom: 15px;
}
.settings-desc {
  color: var(--text-muted);
  line-height: 1.6;
  margin-bottom: 20px;
  font-size: 14px;
}
.cert-actions {
  margin-bottom: 25px;
}
.cert-btn {
  padding: 12px 24px;
  font-size: 15px;
  border-radius: 6px;
  cursor: pointer;
  background: #4a90e2;
  color: white;
  border: none;
  font-weight: 600;
  transition: background 0.2s;
  box-shadow: 0 2px 8px rgba(74, 144, 226, 0.3);
}
.cert-btn:hover {
  background: #357abd;
}
.cert-guide {
  background: rgba(0,0,0,0.2);
  padding: 20px;
  border-radius: 6px;
  border: 1px solid rgba(255,255,255,0.05);
}
.cert-guide h4 {
  margin-top: 0;
  color: #e5c07b;
  margin-bottom: 15px;
  font-size: 15px;
}
.cert-guide ol {
  margin: 0;
  padding-left: 20px;
  color: #abb2bf;
  line-height: 1.8;
  font-size: 14px;
}
.cert-guide code {
  background: rgba(255,255,255,0.1);
  padding: 2px 6px;
  border-radius: 4px;
  font-family: 'Fira Code', monospace;
  color: #98c379;
}

.repeater-btn {
  margin-left: auto;
}

.json-viewer-wrap {
  background: var(--bg-dark);
  padding: 12px;
  border-radius: 4px;
  border: 1px solid var(--border);
  font-family: var(--font-mono);
  font-size: 13px;
  overflow-x: auto;
}
.json-viewer-wrap .vjs-tree__node {
  color: var(--text) !important;
}
.json-viewer-wrap .vjs-value__string {
  color: var(--green) !important;
}
.json-viewer-wrap .vjs-value__number {
  color: var(--orange) !important;
}
.json-viewer-wrap .vjs-value__boolean {
  color: var(--cyan) !important;
}

/* ─── Inspector ───────────────────────────────────────────────────────────── */
.inspector {
  height: 240px;
  display: flex;
  border-top: 1px solid var(--border);
  flex-shrink: 0;
}
.inspector-empty {
  height: 60px;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  font-size: 12px;
  font-style: italic;
}

.inspector-col {
  flex: 1;
  display: flex;
  flex-direction: column;
  border-right: 1px solid var(--border);
  min-width: 0;
}
.inspector-col:last-child { border-right: none; }

.inspector-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 5px 10px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  font-size: 11px;
  font-weight: 600;
  flex-shrink: 0;
}

.inspector-tabs {
  display: flex;
  margin-left: 4px;
}
.inspector-tabs button {
  background: transparent;
  border: none;
  color: var(--text-muted);
  padding: 2px 8px;
  font-size: 11px;
  cursor: pointer;
  border-radius: 3px;
}
.inspector-tabs button.active { background: var(--bg-2); color: var(--text); }

.inspector-body {
  flex: 1;
  padding: 8px 12px;
  overflow: auto;
  font-family: var(--font-mono);
  font-size: 11.5px;
  color: #a7c4e0;
  white-space: pre-wrap;
  word-break: break-all;
  line-height: 1.6;
  background: var(--bg);
}

.chaos-badge {
  margin-left: auto;
  color: var(--orange);
  font-size: 11px;
  font-weight: 700;
}

/* ─── Btn helpers ─────────────────────────────────────────────────────────── */
.btn-sm {
  padding: 3px 8px;
  border-radius: 4px;
  border: 1px solid var(--border-2);
  background: var(--bg-2);
  color: var(--text-2);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.12s;
}
.btn-sm:hover { background: var(--bg-3); color: var(--text); }

.btn-green  { background: var(--green-dim);  border-color: var(--green);  color: var(--green); }
.btn-red    { background: var(--red-dim);    border-color: var(--red);    color: var(--red); }
.btn-orange { background: var(--orange-dim); border-color: var(--orange); color: var(--orange); }
.btn-purple { background: var(--purple-dim); border-color: var(--purple); color: #c4b5fd; }

/* ─── Interceptor ─────────────────────────────────────────────────────────── */
.interceptor-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 14px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}
.interceptor-hint { font-size: 12px; color: var(--orange); font-weight: 600; margin-left: 8px; }

.interceptor-body {
  flex: 1;
  display: flex;
  min-height: 0;
  padding: 12px;
  gap: 12px;
}

.interceptor-editor {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 6px;
  background: var(--bg-1);
  border: 1px solid var(--border-2);
  border-radius: 8px;
  padding: 10px;
}
.editor-label { font-size: 11px; color: var(--text-muted); font-weight: 600; }

.raw-editor {
  flex: 1;
  width: 100%;
  resize: none;
  background: transparent;
  border: none;
  color: #a7f3d0;
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.7;
  outline: none;
  padding: 0;
}

/* ─── Rules ───────────────────────────────────────────────────────────────── */
.rules-pane {
  flex-direction: row !important;
}

.rules-list {
  width: 280px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  border-right: 1px solid var(--border);
  background: var(--bg-1);
}

.rules-list-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 12px;
  border-bottom: 1px solid var(--border);
  font-size: 12px;
  font-weight: 600;
  flex-shrink: 0;
}

.rules-scroll {
  flex: 1;
  overflow-y: auto;
  padding: 8px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.rule-card {
  padding: 10px 12px;
  background: var(--bg-2);
  border: 1px solid var(--border);
  border-radius: 7px;
  cursor: pointer;
  transition: all 0.15s;
}
.rule-card:hover { border-color: var(--border-2); background: var(--bg-3); }
.rule-card.rule-selected { border-color: var(--purple); background: var(--purple-dim); }
.rule-card.rule-disabled { opacity: 0.45; }

.rule-card-top { display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px; }
.rule-name { font-size: 12px; font-weight: 600; color: var(--text); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 160px; }
.rule-url  { font-family: var(--font-mono); font-size: 11px; color: var(--text-muted); margin-bottom: 6px; display: flex; align-items: center; gap: 4px; }
.method-tiny { font-size: 10px; font-weight: 700; padding: 0 3px; border-radius: 2px; }

.rule-badges { display: flex; gap: 4px; flex-wrap: wrap; }
.rbadge { padding: 1px 5px; border-radius: 3px; font-size: 10px; font-weight: 700; font-family: var(--font-mono); }
.rbadge-orange { background: var(--orange-dim); color: var(--orange); }
.rbadge-red    { background: var(--red-dim);    color: var(--red);    }
.rbadge-purple { background: var(--purple-dim); color: var(--purple); }
.rbadge-yellow { background: var(--yellow-dim); color: var(--yellow); }

.rules-empty { padding: 24px; text-align: center; color: var(--text-muted); font-size: 12px; }

/* Toggle Switch */
.toggle-switch { display: flex; align-items: center; cursor: pointer; }
.toggle-switch input { display: none; }
.toggle-track {
  width: 30px; height: 16px;
  background: var(--bg-3);
  border-radius: 99px;
  position: relative;
  border: 1px solid var(--border-2);
  transition: all 0.2s;
}
.toggle-track::after {
  content: '';
  position: absolute;
  width: 10px; height: 10px;
  border-radius: 50%;
  background: var(--text-muted);
  top: 2px; left: 2px;
  transition: all 0.2s;
}
.toggle-switch input:checked + .toggle-track { background: var(--green-dim); border-color: var(--green); }
.toggle-switch input:checked + .toggle-track::after { left: 16px; background: var(--green); }

/* ─── Rule Editor ─────────────────────────────────────────────────────────── */
.rule-editor {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  min-width: 0;
}

.rule-editor-empty {
  align-items: center;
  justify-content: center;
}
.empty-hint { text-align: center; color: var(--text-muted); }
.empty-icon { font-size: 36px; margin-bottom: 10px; }

.editor-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 16px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}
.editor-title { font-size: 13px; font-weight: 700; }
.editor-actions { display: flex; gap: 6px; }

.editor-form {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.form-section {
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 12px 14px;
  background: var(--bg-2);
  border: 1px solid var(--border);
  border-radius: 8px;
}

.form-section-title {
  font-size: 11px;
  font-weight: 700;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.08em;
  padding: 0 2px;
}

.form-row { display: flex; flex-direction: column; gap: 4px; }
.form-row.two-col { flex-direction: row; gap: 10px; }
.form-row.two-col > * { flex: 1; }

.form-label { font-size: 11px; color: var(--text-muted); font-weight: 600; margin-bottom: 2px; }
.form-input { width: 100%; }
.form-unit  { font-size: 12px; color: var(--text-muted); }
.form-hint  { font-size: 11px; color: var(--text-muted); font-family: var(--font-mono); }

/* Inject block */
.inject-block {
  padding: 10px 14px;
  background: var(--bg-2);
  border: 1px solid var(--border);
  border-radius: 8px;
  transition: border-color 0.15s;
}
.inject-block.active { border-color: var(--border-2); }

.inject-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.inject-toggle {
  display: flex; align-items: center; gap: 8px;
  cursor: pointer;
}
.inject-toggle input[type=checkbox] { accent-color: var(--purple); width: 14px; height: 14px; }
.inject-name { font-size: 12px; font-weight: 600; }
.inject-val  { font-family: var(--font-mono); color: var(--orange); font-size: 12px; font-weight: 700; }
.inject-warn { font-size: 11px; color: var(--yellow); }

.inject-body {
  margin-top: 10px;
  display: flex;
  align-items: center;
  gap: 8px;
}
.inject-body.body-edit { flex-direction: column; align-items: stretch; }

.delay-slider {
  flex: 1;
  height: 4px;
  accent-color: var(--orange);
  cursor: pointer;
  background: transparent;
  border: none;
}
.delay-number { width: 80px; text-align: center; }

/* ─── Repeater ────────────────────────────────────────────────────────────── */
.repeater-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 14px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}
.target-group { display: flex; align-items: center; gap: 6px; font-size: 12px; color: var(--text-2); }
.target-input { width: 260px; }

.repeater-body {
  flex: 1;
  display: flex;
  min-height: 0;
  gap: 1px;
  background: var(--border);
}

.repeater-col {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: var(--bg);
  min-width: 0;
}

.repeater-col-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 7px 12px;
  background: var(--bg-1);
  border-bottom: 1px solid var(--border);
  font-size: 11px;
  font-weight: 600;
  flex-shrink: 0;
}

.repeater-col .raw-editor {
  flex: 1;
  padding: 10px 14px;
  font-size: 12px;
  color: #a7f3d0;
  line-height: 1.7;
}
.repeater-col pre.raw-editor { color: #a7c4e0; }

/* ─── Log Panel ───────────────────────────────────────────────────────────── */
.log-panel {
  border-top: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  transition: height 0.2s ease;
  height: 160px;
  flex-shrink: 0;
  background: var(--bg-1);
}
.log-panel.collapsed { height: 0; overflow: hidden; border-top: none; }

.log-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 5px 12px;
  border-bottom: 1px solid var(--border);
  font-size: 11px;
  font-weight: 600;
  flex-shrink: 0;
}

.log-body { flex: 1; overflow-y: auto; padding: 4px 0; }
.log-line {
  font-family: var(--font-mono);
  font-size: 11px;
  padding: 2px 14px;
  line-height: 1.55;
  white-space: pre-wrap;
  word-break: break-all;
}
.log-info  { color: var(--text-2); }
.log-chaos { color: var(--orange); }
.log-warn  { color: var(--yellow); }
.log-err   { color: var(--red); }
.log-empty { padding: 10px 14px; color: var(--text-muted); font-size: 11px; font-style: italic; }

/* ─── Toast ───────────────────────────────────────────────────────────────── */
.toast {
  position: fixed;
  bottom: 24px;
  left: 50%;
  transform: translateX(-50%);
  padding: 9px 20px;
  border-radius: 8px;
  font-size: 13px;
  font-weight: 600;
  z-index: 9999;
  pointer-events: none;
  box-shadow: 0 4px 24px rgba(0,0,0,0.4);
}
.toast-success { background: var(--green-dim);  color: var(--green);  border: 1px solid var(--green);  }
.toast-warn    { background: var(--yellow-dim); color: var(--yellow); border: 1px solid var(--yellow); }
.toast-error   { background: var(--red-dim);    color: var(--red);    border: 1px solid var(--red);    }
.toast-info    { background: var(--bg-3); color: var(--text); border: 1px solid var(--border-2); }

.toast-fade-enter-active, .toast-fade-leave-active { transition: all 0.25s ease; }
.toast-fade-enter-from, .toast-fade-leave-to { opacity: 0; transform: translateX(-50%) translateY(10px); }

/* ─── History pane specific ───────────────────────────────────────────────── */
.history-pane { overflow: hidden; }
</style>

#!/bin/bash

# Chaos-Proxy 自动化功能测试脚本

PORT=8888
RULES="rules.json"
PROXY_BIN="build/bin/chaos-proxy"

echo "=================================================="
echo "          Chaos-Proxy 自动化构建与测试           "
echo "=================================================="

# 1. 编译项目
echo "[1/4] 编译项目..."
make clean > /dev/null
make
if [ $? -ne 0 ]; then
    echo "❌ 编译失败！"
    exit 1
fi
echo "✅ 编译成功: $PROXY_BIN"

# 2. 后台启动 Chaos-Proxy 代理进程
echo ""
echo "[2/4] 启动 Chaos-Proxy (端口: $PORT, 配置: $RULES)..."
$PROXY_BIN -p $PORT -c $RULES &
PROXY_PID=$!
sleep 1 # 等待代理 Socket 监听准备就绪

cleanup() {
    echo ""
    echo "[4/4] 清理并关闭后台代理进程 (PID: $PROXY_PID)..."
    kill -SIGINT $PROXY_PID 2>/dev/null
    wait $PROXY_PID 2>/dev/null
    echo "✅ 测试完成！"
}
trap cleanup EXIT

echo ""
echo "[3/4] 开始模拟 HTTP 请求测试..."
echo "--------------------------------------------------"

# 测试 A: 正常未命中规则的请求 (预期: 200 OK)
echo "➜ 测试 A: 发送未匹配规则的普通请求 (/api/ping)"
curl -s -i "http://127.0.0.1:$PORT/api/ping"
echo -e "\n--------------------------------------------------"

# 测试 B: 匹配登录故障规则 (预期: 2秒延迟 + 503 状态码 + code改写为5)
echo "➜ 测试 B: 发送登录接口请求 (/api/user/login, 预定期望: 2s延时 + 503状态码 + Body篡改)"
START_TIME=$(date +%s)
curl -s -i -X POST "http://127.0.0.1:$PORT/api/user/login"
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
echo -e "\n⏱ 实际耗时: ${ELAPSED}s"
echo "--------------------------------------------------"

# 测试 C: 匹配慢查询规则 (预期: 3秒延迟)
echo "➜ 测试 C: 发送订单列表接口请求 (/api/order/list, 预定期望: 3s延时)"
START_TIME=$(date +%s)
curl -s -i "http://127.0.0.1:$PORT/api/order/list"
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
echo -e "\n⏱ 实际耗时: ${ELAPSED}s"
echo "--------------------------------------------------"

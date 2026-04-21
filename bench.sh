#!/usr/bin/env bash
# bench.sh —— 线程池 vs 单线程 wrk 压测对比
# 用法：./bench.sh [并发连接数] [测试时长(s)] [wrk线程数]
set -e

CONNECTIONS=${1:-100}
DURATION=${2:-10}
WRK_THREADS=${3:-4}
PORT=8888
URL="http://127.0.0.1:${PORT}/"
BUILD_DIR="$(dirname "$0")/cmake-build-debug"

# ——— 找 cmake ——————————————————————————————————————
CMAKE=$(command -v cmake 2>/dev/null \
    || echo /Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake)
if [ ! -x "$CMAKE" ]; then
    echo "ERROR: 找不到 cmake，请安装或将 cmake 加入 PATH"; exit 1
fi

# ——— 构建 ———————————————————————————————————————————
echo ">>> 构建 Release 版本..."
"$CMAKE" -S "$(dirname "$0")" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -Wno-dev 2>/dev/null
"$CMAKE" --build "$BUILD_DIR" --target server server_single -- -j"$(sysctl -n hw.logicalcpu 2>/dev/null || nproc)"
echo ">>> 构建完成"
echo ""

wait_for_port() {
    local max=20
    for i in $(seq 1 $max); do
        if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
            return 0
        fi
        sleep 0.2
    done
    echo "ERROR: 服务器未在 ${PORT} 端口就绪"
    exit 1
}

run_bench() {
    local label="$1"
    local binary="$2"

    echo "╔══════════════════════════════════════════════════════╗"
    printf  "║  %-52s║\n" "$label"
    echo "╚══════════════════════════════════════════════════════╝"

    # 确保端口空闲
    lsof -ti tcp:"$PORT" | xargs kill -9 2>/dev/null; sleep 0.3

    # 启动服务器（从项目根目录运行，保证 ./root 路径正确）
    cd "$(dirname "$0")"
    "$binary" &
    local pid=$!
    wait_for_port

    wrk -t"$WRK_THREADS" -c"$CONNECTIONS" -d"${DURATION}s" --latency "$URL"

    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
    echo ""
    sleep 1
}

echo "压测参数：连接数=${CONNECTIONS}  时长=${DURATION}s  wrk线程=${WRK_THREADS}"
echo ""

run_bench "线程池服务器（8 worker threads）" "$BUILD_DIR/server"
run_bench "单线程服务器（无线程池）" "$BUILD_DIR/server_single"
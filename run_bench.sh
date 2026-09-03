#!/usr/bin/env bash
# run_bench.sh — Start server, run bench client, cleanup
cd "$(dirname "$0")"

# Auto-cleanup on exit
cleanup() {
    echo "=== Cleaning up ==="
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT

echo "=== Stopping old Server & Starting new Server ==="
pkill -f kv_store_app || true
rm -f wal.log
nohup ./build/kv_store_app > /tmp/kv_server.log 2>&1 &
SERVER_PID=$!

# Wait for port 8080 to open
while ! ss -tln 2>/dev/null | grep -q ':8080 '; do sleep 0.1; done
echo "Server is ready!"

echo "=== Starting Benchmark ==="
./build/bench_client 127.0.0.1 8080 128 64 5 4

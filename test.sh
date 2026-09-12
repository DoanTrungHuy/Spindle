#!/bin/bash
set -euo pipefail

# Rebuild the server with the new changes
mkdir -p build && cd build
cmake ..
make -j
cd ..

echo ">>> Starting spindle_app in background..."
./build/spindle_app > server.log 2>&1 &
SERVER_PID=$!

echo ">>> Waiting for server to be ready..."
for i in $(seq 1 30); do
  if nc -z 127.0.0.1 8888 2>/dev/null; then break; fi
  sleep 0.2
done

echo ""
echo ">>> Running test_correctness.py..."
python3 test_correctness.py

echo ""
echo ">>> Running bench_client for 4 seconds..."
./build/bench_client 127.0.0.1 8888 128 64 4 4

echo ""
echo ">>> Sending SIGINT to spindle_app (PID $SERVER_PID)..."
kill -SIGINT $SERVER_PID

echo ">>> Waiting for Graceful Shutdown..."
wait $SERVER_PID || true

echo ""
echo ">>> SERVER LOG OUTPUT:"
cat server.log

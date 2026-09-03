#!/bin/bash
# Rebuild the server with the new changes
cmake .
make

# Compile the bench client
g++ src/bench_client.cpp -o bench_client -O3 -pthread -std=c++20

echo ">>> Starting kv_store_app in background..."
./kv_store_app > server.log 2>&1 &
SERVER_PID=$!

sleep 1

echo ">>> Running bench_client for 4 seconds..."
./bench_client 127.0.0.1 8080 128 64 4 4

echo ""
echo ">>> Sending SIGINT to kv_store_app (PID $SERVER_PID)..."
kill -SIGINT $SERVER_PID

echo ">>> Waiting for Graceful Shutdown..."
wait $SERVER_PID

echo ""
echo ">>> SERVER LOG OUTPUT:"
cat server.log

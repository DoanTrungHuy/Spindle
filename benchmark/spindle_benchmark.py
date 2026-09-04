import asyncio
import time
import argparse

async def benchmark_worker(host, port, num_requests, payload):
    try:
        reader, writer = await asyncio.open_connection(host, port)
        for _ in range(num_requests):
            writer.write(payload.encode())
            await writer.drain()
            response = await reader.readline()
            if not response:
                break
        writer.close()
        await writer.wait_closed()
        return num_requests
    except Exception as e:
        print(f"Worker error: {e}")
        return 0

async def run_benchmark(host, port, concurrency, total_requests, command):
    print(f"--- Spindle Benchmark Tool ---")
    print(f"Target: {host}:{port}")
    print(f"Concurrency: {concurrency}")
    print(f"Total Requests: {total_requests}")
    print(f"Command: {command.strip()}")
    print("Benchmarking...")

    req_per_worker = total_requests // concurrency
    payload = command + "\n"

    start_time = time.time()
    
    tasks = []
    for _ in range(concurrency):
        tasks.append(benchmark_worker(host, port, req_per_worker, payload))
    
    results = await asyncio.gather(*tasks)
    
    end_time = time.time()
    elapsed = end_time - start_time
    total_completed = sum(results)
    
    rps = total_completed / elapsed if elapsed > 0 else 0
    
    print(f"--- Results ---")
    print(f"Time Taken: {elapsed:.2f} seconds")
    print(f"Completed Requests: {total_completed}")
    print(f"Requests Per Second (RPS): {rps:.2f}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Spindle Benchmark Tool")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=8888, help="Server port")
    parser.add_argument("-c", "--concurrency", type=int, default=50, help="Number of concurrent clients")
    parser.add_argument("-n", "--requests", type=int, default=100000, help="Total number of requests")
    parser.add_argument("--command", type=str, default="SET bench_key 123", help="Command to run")
    
    args = parser.parse_args()
    
    # Run asyncio loop
    asyncio.run(run_benchmark(args.host, args.port, args.concurrency, args.requests, args.command))

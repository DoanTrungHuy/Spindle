#include "common.h"
#include "slab_allocator.h"
#include "sharded_map.h"
#include "mpsc_ring_buffer.h"
#include "wal_flusher.h"
#include "reactor.h"
#include "expiry_cleaner.h"
#include <fstream>
#include <filesystem>
#include <csignal>
#include <condition_variable>
#include <mutex>

std::condition_variable cv_shutdown;
std::mutex mtx_shutdown;

void signal_handler(int) {
    std::cout << "\n[Graceful Shutdown] Received signal. Flushing WAL and stopping...\n";
    cv_shutdown.notify_all();
}

void verify_wal_log(const std::string& filepath) {
    std::cout << "\n=== Verifying Write-Ahead Log (WAL) ===\n";
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open WAL file for verification: " << filepath << "\n";
        return;
    }

    size_t count = 0;
    size_t set_count = 0;
    size_t del_count = 0;

    std::string line;

    while (std::getline(file, line)) {
        count++;
        if (line.starts_with("SET ")) {
            set_count++;
        } else if (line.starts_with("DEL ")) {
            del_count++;
        }
        if (count <= 5) {
            std::cout << "WAL Entry #" << count << ": " << line << "\n";
        }
    }

    std::cout << "WAL verification finished.\n";
    std::cout << "Total entries decoded from WAL: " << count 
              << " (SET: " << set_count << ", DEL: " << del_count << ")\n";
    std::cout << "=======================================\n";
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================================\n";
    std::cout << "  Starting High-Performance Key-Value Store Engine\n";
    std::cout << "========================================================\n";

    std::string wal_path = "wal.log";
    if (std::filesystem::exists(wal_path)) {
        std::filesystem::remove(wal_path);
    }

    std::cout << "[1/4] Initializing Slab Allocator (64MB Arena)...\n";
    SlabAllocator allocator(64 * 1024 * 1024);

    std::cout << "[2/4] Initializing Sharded Concurrent Hash Map...\n";
    ShardedMap kv_map(allocator);

    std::cout << "[3/4] Initializing Write-Ahead Log Flusher...\n";
    MPSCRingBuffer<4096> ring_buffer;
    WALFlusher wal_flusher(ring_buffer, wal_path);

    std::cout << "[4/5] Initializing 4 Network Reactors (SO_REUSEPORT)...\n";
    std::vector<std::unique_ptr<Reactor>> reactors;
    for (int i = 0; i < 4; ++i) {
        reactors.push_back(std::make_unique<Reactor>(8888, kv_map, ring_buffer));
    }

    std::cout << "[5/5] Initializing Expiry Cleaner (TTL background thread)...\n";
    ExpiryCleaner expiry_cleaner(kv_map, 100); // Scan every 100ms

    std::cout << "\n>>> Starting Engine Subsystems...\n";
    wal_flusher.start();
    expiry_cleaner.start();
    for (auto& r : reactors) {
        r->start();
    }

    std::cout << "\n>>> Engine is running! Press Ctrl+C to safely shutdown...\n";

    // Block main thread, wait for Ctrl+C signal to proceed with cleanup
    std::unique_lock<std::mutex> lock(mtx_shutdown);
    cv_shutdown.wait(lock);

    std::cout << "\n>>> Shutting down Engine Subsystems...\n";
    for (auto& r : reactors) {
        r->stop();
    }
    expiry_cleaner.stop();
    wal_flusher.stop();
    std::cout << "All subsystems stopped.\n";

    // Output final system statistics
    std::cout << "\n>>> Final Diagnostics <<<\n";
    kv_map.print_stats();
    allocator.print_stats();
    
    // Verify WAL contents
    verify_wal_log(wal_path);

    std::cout << "\nClean exit. Project execution completed successfully.\n";
    return 0;
}

#ifndef EXPIRY_CLEANER_H
#define EXPIRY_CLEANER_H

#include "sharded_map.h"
#include <thread>
#include <atomic>
#include <chrono>

class ExpiryCleaner {
public:
    explicit ExpiryCleaner(ShardedMap& map, int interval_ms = 100)
        : m_map(map), m_interval_ms(interval_ms), m_running(false) {}

    ~ExpiryCleaner() { stop(); }

    // Prevent copying
    ExpiryCleaner(const ExpiryCleaner&) = delete;
    ExpiryCleaner& operator=(const ExpiryCleaner&) = delete;

    void start() {
        m_running.store(true, std::memory_order_relaxed);
        m_thread = std::thread(&ExpiryCleaner::run, this);
    }

    void stop() {
        m_running.store(false, std::memory_order_relaxed);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    void run() {
        while (m_running.load(std::memory_order_relaxed)) {
            m_map.cleanup_expired_keys();
            std::this_thread::sleep_for(std::chrono::milliseconds(m_interval_ms));
        }
    }

    ShardedMap& m_map;
    int m_interval_ms;
    std::atomic<bool> m_running;
    std::thread m_thread;
};

#endif // EXPIRY_CLEANER_H

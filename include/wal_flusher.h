#ifndef WAL_FLUSHER_H
#define WAL_FLUSHER_H

#include "common.h"
#include "mpsc_ring_buffer.h"
#include <thread>
#include <atomic>
#include <string>
#include <cstdio>

class WALFlusher {
public:
    WALFlusher(MPSCRingBuffer<4096>& ring_buffer, const std::string& filepath);
    ~WALFlusher();

    // Start background flusher thread
    void start();

    // Stop and flush remaining items
    void stop();

    // Diagnostics
    size_t get_total_flushed() const { return m_total_flushed.load(std::memory_order_relaxed); }

private:
    void run();

    MPSCRingBuffer<4096>& m_ring_buffer;
    std::string m_filepath;
    FILE* m_file = nullptr;

    std::thread m_thread;
    std::atomic<bool> m_running;
    std::atomic<size_t> m_total_flushed;

    // Helper to perform cross-platform fsync
    void perform_fsync();
};

#endif // WAL_FLUSHER_H

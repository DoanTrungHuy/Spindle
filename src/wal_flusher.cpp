#include "wal_flusher.h"
#include <iostream>
#include <chrono>

#include <unistd.h>

WALFlusher::WALFlusher(MPSCRingBuffer<4096>& ring_buffer, const std::string& filepath)
    : m_ring_buffer(ring_buffer), m_filepath(filepath), m_running(false), m_total_flushed(0) {
}

WALFlusher::~WALFlusher() {
    stop();
}

void WALFlusher::start() {
    m_file = std::fopen(m_filepath.c_str(), "a"); // Append text mode
    if (!m_file) {
        std::cerr << "Failed to open WAL file: " << m_filepath << std::endl;
        return;
    }

    m_running = true;
    m_thread = std::thread(&WALFlusher::run, this);
}

void WALFlusher::stop() {
    if (m_running) {
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

void WALFlusher::perform_fsync() {
    if (!m_file) return;
    std::fflush(m_file);
    int fd = fileno(m_file);
    if (fd != -1) {
        fsync(fd);
    }
}

void WALFlusher::run() {
    LogEntry batch[128];
    
    while (m_running) {
        size_t batch_count = 0;
        
        // Batch consume up to 128 elements
        while (batch_count < 128) {
            if (m_ring_buffer.pop(batch[batch_count])) {
                batch_count++;
            } else {
                break; // Queue is empty
            }
        }

        if (batch_count > 0) {
            for (size_t i = 0; i < batch_count; ++i) {
                const LogEntry& entry = batch[i];
                
                if (entry.type == CommandType::SET) {
                    std::fprintf(m_file, "SET %.*s %.*s\n", entry.key_len, entry.key, entry.val_len, entry.val);
                } else if (entry.type == CommandType::DEL) {
                    std::fprintf(m_file, "DEL %.*s\n", entry.key_len, entry.key);
                }
            }

            perform_fsync();
            m_total_flushed.fetch_add(batch_count, std::memory_order_relaxed);
        } else {
            // Idle sleep to save CPU cycles
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Drain remaining items in the queue before exit
    size_t final_flushed = 0;
    LogEntry entry;
    while (m_ring_buffer.pop(entry)) {
        if (entry.type == CommandType::SET) {
            std::fprintf(m_file, "SET %.*s %.*s\n", entry.key_len, entry.key, entry.val_len, entry.val);
        } else if (entry.type == CommandType::DEL) {
            std::fprintf(m_file, "DEL %.*s\n", entry.key_len, entry.key);
        }
        final_flushed++;
    }

    if (final_flushed > 0) {
        perform_fsync();
        m_total_flushed.fetch_add(final_flushed, std::memory_order_relaxed);
    }
}

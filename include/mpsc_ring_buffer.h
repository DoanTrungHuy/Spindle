#ifndef MPSC_RING_BUFFER_H
#define MPSC_RING_BUFFER_H

#include "common.h"
#include <atomic>
#include <cassert>
#include <new>

struct LogEntry {
    CommandType type;
    uint32_t key_len;
    uint32_t val_len;
    char key[64];
    char val[512];

    bool set_data(CommandType t, std::string_view k, std::string_view v) {
        if (k.size() >= sizeof(key) || v.size() >= sizeof(val)) {
            return false; // Exceeds fixed buffers for zero-allocation path
        }
        type = t;
        key_len = static_cast<uint32_t>(k.size());
        val_len = static_cast<uint32_t>(v.size());
        std::memcpy(key, k.data(), key_len);
        key[key_len] = '\0';
        std::memcpy(val, v.data(), val_len);
        val[val_len] = '\0';
        return true;
    }
};

template <size_t Capacity = 4096>
class MPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    MPSCRingBuffer() {
        for (size_t i = 0; i < Capacity; ++i) {
            m_buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
        m_enqueue_pos.store(0, std::memory_order_relaxed);
        m_dequeue_pos.store(0, std::memory_order_relaxed);
    }

    ~MPSCRingBuffer() = default;

    // Push item - thread-safe for multiple producers (Worker Threads)
    bool push(const LogEntry& item) {
        Node* cell;
        size_t pos = m_enqueue_pos.load(std::memory_order_relaxed);
        for (;;) {
            cell = &m_buffer[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (m_enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                // Buffer is full
                return false;
            } else {
                pos = m_enqueue_pos.load(std::memory_order_relaxed);
            }
        }
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // Pop item - thread-safe only for a single consumer (WAL Flusher Thread)
    bool pop(LogEntry& item) {
        Node* cell;
        size_t pos = m_dequeue_pos.load(std::memory_order_relaxed);
        for (;;) {
            cell = &m_buffer[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (dif == 0) {
                if (m_dequeue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                // Buffer is empty
                return false;
            } else {
                pos = m_dequeue_pos.load(std::memory_order_relaxed);
            }
        }
        item = cell->data;
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    // Returns approximate size of queue
    size_t size() const {
        size_t eq = m_enqueue_pos.load(std::memory_order_relaxed);
        size_t dq = m_dequeue_pos.load(std::memory_order_relaxed);
        return eq > dq ? (eq - dq) : 0;
    }

private:
    struct Node {
        std::atomic<size_t> sequence;
        LogEntry data;
    };

    // Align variables to different cache lines to prevent false sharing
    alignas(64) Node m_buffer[Capacity];
    alignas(64) std::atomic<size_t> m_enqueue_pos;
    alignas(64) std::atomic<size_t> m_dequeue_pos;
};

#endif // MPSC_RING_BUFFER_H

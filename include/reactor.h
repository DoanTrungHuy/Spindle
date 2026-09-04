#ifndef REACTOR_H
#define REACTOR_H

#include "common.h"
#include "sharded_map.h"
#include "mpsc_ring_buffer.h"
#include <thread>
#include <atomic>
#include <vector>

class Reactor {
public:
    Reactor(const std::string& host, int port, ShardedMap& sharded_map, MPSCRingBuffer<4096>& ring_buffer);
    ~Reactor();

    // Start reactor event loop
    void start();

    // Stop reactor event loop
    void stop();

    // Check if running
    bool is_running() const { return m_running.load(std::memory_order_relaxed); }

    // Diagnostics
    size_t get_connections() const { return m_connections_handled.load(std::memory_order_relaxed); }
    size_t get_requests() const { return m_requests_handled.load(std::memory_order_relaxed); }

private:
    void run_loop();

    std::string m_host;
    int m_port;
    ShardedMap& m_sharded_map;
    MPSCRingBuffer<4096>& m_ring_buffer;
    
    std::atomic<bool> m_running;
    std::thread m_reactor_thread;

    int m_epoll_fd = -1;
    int m_listen_fd = -1;
    
    std::atomic<size_t> m_connections_handled{0};
    std::atomic<size_t> m_requests_handled{0};
    
    // Per-client TCP buffer for partial stream reads (Fix stream corruption bug)
    std::string m_client_buffers[65536];

    void set_nonblocking(int fd);
    void handle_new_connection();
    void handle_client_data(int client_fd);
};

#endif // REACTOR_H

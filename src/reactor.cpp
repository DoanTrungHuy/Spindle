#include "reactor.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

Reactor::Reactor(const std::string& host, int port, ShardedMap& sharded_map, MPSCRingBuffer<4096>& ring_buffer)
    : m_host(host), m_port(port), m_sharded_map(sharded_map), m_ring_buffer(ring_buffer), m_running(false) {
}

Reactor::~Reactor() {
    stop();
}

void Reactor::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void Reactor::start() {
    m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listen_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    // Increase kernel socket buffers for high throughput
    int buf_sz = 1 << 20; // 1MB
    setsockopt(m_listen_fd, SOL_SOCKET, SO_RCVBUF, &buf_sz, sizeof(buf_sz));
    setsockopt(m_listen_fd, SOL_SOCKET, SO_SNDBUF, &buf_sz, sizeof(buf_sz));
    set_nonblocking(m_listen_fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (m_host == "0.0.0.0" || m_host.empty()) {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, m_host.c_str(), &address.sin_addr) <= 0) {
            std::cerr << "Invalid address/ Address not supported: " << m_host << "\n";
            close(m_listen_fd);
            return;
        }
    }
    address.sin_port = htons(m_port);

    if (bind(m_listen_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to " << m_host << ":" << m_port << "\n";
        close(m_listen_fd);
        return;
    }

    if (listen(m_listen_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen\n";
        close(m_listen_fd);
        return;
    }

    m_epoll_fd = epoll_create1(0);
    if (m_epoll_fd < 0) {
        std::cerr << "Failed to create epoll instance\n";
        close(m_listen_fd);
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = m_listen_fd;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_listen_fd, &ev) < 0) {
        std::cerr << "Failed to add listen fd to epoll\n";
        close(m_epoll_fd);
        close(m_listen_fd);
        return;
    }

    m_running = true;
    m_reactor_thread = std::thread(&Reactor::run_loop, this);
    std::cout << "Reactor listening on " << m_host << ":" << m_port << " using Epoll (Linux)\n";
}

void Reactor::stop() {
    if (m_running) {
        m_running = false;
        if (m_reactor_thread.joinable()) {
            m_reactor_thread.join();
        }
        if (m_epoll_fd != -1) {
            close(m_epoll_fd);
            m_epoll_fd = -1;
        }
        if (m_listen_fd != -1) {
            close(m_listen_fd);
            m_listen_fd = -1;
        }
    }
}

void Reactor::run_loop() {
    epoll_event events[64];
    while (m_running) {
        int nfds = epoll_wait(m_epoll_fd, events, 64, 10); // 10ms timeout
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == m_listen_fd) {
                handle_new_connection();
            } else {
                handle_client_data(events[i].data.fd);
            }
        }
    }
}

void Reactor::handle_new_connection() {
    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(m_listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Handled all pending connections
            }
            break;
        }

        set_nonblocking(client_fd);
        m_connections_handled.fetch_add(1, std::memory_order_relaxed);
        // Disable Nagle: ensures responses are sent immediately
        int one = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        int buf_sz = 1 << 20;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &buf_sz, sizeof(buf_sz));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &buf_sz, sizeof(buf_sz));
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            close(client_fd);
        }
    }
}

void Reactor::handle_client_data(int client_fd) {
    char buf[8192];
    std::string& request_str = m_client_buffers[client_fd];

    while (true) {
        ssize_t bytes_read = recv(client_fd, buf, sizeof(buf), 0);
        if (bytes_read > 0) {
            request_str.append(buf, bytes_read);
            // Prevent DoS: limit per-client buffer to 16MB
            if (request_str.size() > 16 * 1024 * 1024) {
                close(client_fd);
                m_client_buffers.erase(client_fd);
                m_client_out_buffers.erase(client_fd);
                return;
            }
        } else if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // All data read
            }
            close(client_fd);
            m_client_buffers.erase(client_fd);
            m_client_out_buffers.erase(client_fd);
            return;
        } else {
            close(client_fd); // Client disconnected
            m_client_buffers.erase(client_fd);
            m_client_out_buffers.erase(client_fd);
            return;
        }
    }

    std::string_view req_view(request_str);
    size_t start = 0;
    
    std::string& out_buf = m_client_out_buffers[client_fd];

    auto flush_output = [&]() {
        if (!out_buf.empty()) {
            ssize_t sent = send(client_fd, out_buf.data(), out_buf.size(), MSG_NOSIGNAL);
            if (sent > 0) {
                out_buf.erase(0, sent);
            }
        }
    };

    auto append_output = [&](std::string_view msg) {
        out_buf.append(msg);
        if (out_buf.size() > 65536) flush_output();
    };

    while (start < req_view.size()) {
        size_t end = req_view.find('\n', start);
        if (end == std::string_view::npos) {
            // Partial packet received! We MUST leave it in the buffer and wait for next TCP chunk.
            break; 
        }
        
        std::string_view line = req_view.substr(start, end - start);
        start = end + 1; // Advance to next command
        
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;

        size_t first_space = line.find(' ');
        std::string_view cmd, key, val;

        if (first_space == std::string_view::npos) {
            cmd = line;
        } else {
            cmd = line.substr(0, first_space);
            std::string_view rest = line.substr(first_space + 1);
            while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
            size_t second_space = rest.find(' ');
            if (second_space == std::string_view::npos) {
                key = rest;
            } else {
                key = rest.substr(0, second_space);
                val = rest.substr(second_space + 1);
                while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
            }
        }

        // Inline Processing (No worker threads needed)
        m_requests_handled.fetch_add(1, std::memory_order_relaxed);
        if (cmd == "SET") {
            if (!key.empty() && !val.empty()) {
                // Parse optional EX/PX for TTL
                uint64_t ttl_ns = 0;
                std::string_view actual_val = val;

                size_t last_space = actual_val.rfind(' ');
                if (last_space != std::string_view::npos) {
                    std::string_view last_word = actual_val.substr(last_space + 1);
                    bool is_number = true;
                    for (char c : last_word) {
                        if (c < '0' || c > '9') { is_number = false; break; }
                    }
                    if (is_number && !last_word.empty()) {
                        size_t second_last_space = actual_val.rfind(' ', last_space - 1);
                        if (second_last_space == std::string_view::npos && last_space > 0) {
                            second_last_space = 0; // The whole string starts with EX/PX
                        }
                        
                        if (second_last_space != std::string_view::npos) {
                            size_t start_idx = (second_last_space == 0 && actual_val[0] != ' ') ? 0 : second_last_space + 1;
                            std::string_view second_last_word = actual_val.substr(start_idx, last_space - start_idx);
                            
                            if (second_last_word == "EX") {
                                uint64_t seconds = 0;
                                for (char c : last_word) seconds = seconds * 10 + (c - '0');
                                ttl_ns = seconds * 1'000'000'000ULL;
                                actual_val = (start_idx == 0) ? "" : actual_val.substr(0, second_last_space);
                            } else if (second_last_word == "PX") {
                                uint64_t ms = 0;
                                for (char c : last_word) ms = ms * 10 + (c - '0');
                                ttl_ns = ms * 1'000'000ULL;
                                actual_val = (start_idx == 0) ? "" : actual_val.substr(0, second_last_space);
                            }
                        }
                    }
                }

                bool evicted = false;
                std::string evicted_key;
                m_sharded_map.set(key, actual_val, evicted, evicted_key, [&]() {
                    // Write-Ahead Log via MPSC Ring Buffer
                    LogEntry log_entry;
                    if (log_entry.set_data(CommandType::SET, key, actual_val)) {
                        while (!m_ring_buffer.push(log_entry)) {
                            std::this_thread::yield();
                        }
                    }
                }, ttl_ns);
                append_output("(integer) 1\n");
            } else {
                append_output("ERR INVALID_FORMAT\n");
            }
        } else if (cmd == "GET") {
            if (!key.empty()) {
                std::string res;
                if (m_sharded_map.get(key, res)) {
                    append_output(res + "\n");
                } else {
                    append_output("(nil)\n");
                }
            } else {
                append_output("ERR INVALID_FORMAT\n");
            }
        } else if (cmd == "DEL") {
            if (!key.empty()) {
                bool success = m_sharded_map.del(key, [&]() {
                    LogEntry log_entry;
                    if (log_entry.set_data(CommandType::DEL, key, "")) {
                        while (!m_ring_buffer.push(log_entry)) {
                            std::this_thread::yield();
                        }
                    }
                });
                if (success) {
                    append_output("(integer) 1\n");
                } else {
                    append_output("(integer) 0\n");
                }
            } else {
                append_output("ERR INVALID_FORMAT\n");
            }
        } else if (cmd == "TTL") {
            if (!key.empty()) {
                int64_t remaining = m_sharded_map.ttl(key);
                append_output("(integer) " + std::to_string(remaining) + "\n");
            } else {
                append_output("ERR INVALID_FORMAT\n");
            }
        } else if (cmd == "PERSIST") {
            if (!key.empty()) {
                if (m_sharded_map.persist(key)) {
                    append_output("(integer) 1\n");
                } else {
                    append_output("(integer) 0\n");
                }
            } else {
                append_output("ERR INVALID_FORMAT\n");
            }
        } else {
            append_output("ERR UNKNOWN_COMMAND\n");
        }
    }
    
    // Clear processed bytes from buffer, keeping only the partial packet (if any)
    request_str.erase(0, start);
    
    flush_output();
}

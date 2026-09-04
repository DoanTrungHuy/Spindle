/**
 * bench_client.cpp — Minimalist Spindle Benchmark
 */
#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <netinet/tcp.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <cstring>

using namespace std::chrono;

struct Config {
    const char* host = "127.0.0.1";
    int port = 8080, conns = 128, batch = 64, dur = 5, threads = 4;
    int payload_size = 1;
    const char* mode = "SET";
} cfg;

std::atomic<uint64_t> g_responses{0};
std::atomic<bool> g_stop{false};

std::mutex g_lat_mut;
std::vector<uint32_t> g_latencies;

int make_conn() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1; struct timeval tv{2, 0};
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in addr{AF_INET, htons(cfg.port), {0}, {0}};
    inet_pton(AF_INET, cfg.host, &addr.sin_addr);
    return connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0 ? fd : (close(fd), -1);
}

void conn_loop(int fd) {
    std::string payload;
    std::string val(cfg.payload_size, 'x');
    for (int i = 0; i < cfg.payload_size; ++i) {
        val[i] = 'A' + (rand() % 26);
    }
    for (int i = 0; i < cfg.batch; ++i) {
        if (strcmp(cfg.mode, "GET") == 0) {
            payload += "GET k" + std::to_string(i%5000) + "\n";
        } else {
            payload += "SET k" + std::to_string(i%5000) + " " + val + "\n";
        }
    }
    
    char rbuf[131072];
    std::vector<uint32_t> latencies;
    auto deadline = steady_clock::now() + seconds(cfg.dur + 1);
    uint64_t local = 0;

    while (!g_stop && steady_clock::now() < deadline) {
        auto t0 = high_resolution_clock::now();
        
        size_t sent = 0;
        while (sent < payload.size()) {
            ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) goto end_loop;
            sent += n;
        }

        int got = 0, buf_len = 0;
        while (got < cfg.batch) {
            ssize_t n = recv(fd, rbuf + buf_len, sizeof(rbuf) - buf_len, 0);
            if (n <= 0) goto end_loop;
            for (int i = 0; i < n; ++i) if (rbuf[buf_len + i] == '\n') got++;
            buf_len = (buf_len + n > sizeof(rbuf) / 2) ? 0 : buf_len + n;
        }
        
        latencies.push_back(duration_cast<microseconds>(high_resolution_clock::now() - t0).count());
        if (((local += cfg.batch) & 0xFFF) == 0) {
            g_responses.fetch_add(local, std::memory_order_relaxed);
            local = 0;
        }
    }
end_loop:
    g_responses.fetch_add(local, std::memory_order_relaxed);
    std::lock_guard lock(g_lat_mut);
    g_latencies.insert(g_latencies.end(), latencies.begin(), latencies.end());
}

int main(int argc, char** argv) {
    if(argc>1) cfg.host = argv[1]; if(argc>2) cfg.port = atoi(argv[2]);
    if(argc>3) cfg.conns = atoi(argv[3]); if(argc>4) cfg.batch = atoi(argv[4]);
    if(argc>5) cfg.dur = atoi(argv[5]); if(argc>6) cfg.threads = atoi(argv[6]);
    if(argc>7) cfg.payload_size = atoi(argv[7]); if(argc>8) cfg.mode = argv[8];
    
    printf("--- Minimal Spindle Benchmark (%d Threads, %d Conns, Mode: %s, Payload: %dB) ---\n", cfg.threads, cfg.conns, cfg.mode, cfg.payload_size);
    auto t_start = steady_clock::now();
    {
        std::vector<std::jthread> threads;
        int cpt = std::max(1, cfg.conns / cfg.threads);
        for (int i = 0; i < cfg.threads; ++i) {
            threads.emplace_back([cpt]() {
                std::vector<int> fds;
                for (int i = 0; i < cpt; ++i) if (int fd = make_conn(); fd >= 0) fds.push_back(fd);
                {
                    std::vector<std::jthread> subs;
                    for (int fd : fds) subs.emplace_back(conn_loop, fd);
                }
                for (int fd : fds) close(fd);
            });
        }
        for (int s = 1; s <= cfg.dur; ++s) {
            std::this_thread::sleep_for(seconds(1));
            printf("[Sec %d] Rate: %.1f k/s\n", s, g_responses.load() / s / 1000.0);
        }
        g_stop = true;
    } // auto join
    
    double elap = duration<double>(steady_clock::now() - t_start).count();
    printf("--- Final: %.0f RPS ---\n", g_responses.load() / elap);
    
    if (!g_latencies.empty()) {
        std::sort(g_latencies.begin(), g_latencies.end());
        auto p = [&](double pct) { return g_latencies[g_latencies.size() * pct] / 1000.0; };
        printf("Latencies(ms): P50=%.3f, P90=%.3f, P99=%.3f\n", p(0.5), p(0.9), p(0.99));
    }
    return 0;
}

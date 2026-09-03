#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <memory>
#include <cstring>
#include <atomic>

enum class CommandType : uint8_t {
    GET = 0,
    SET = 1,
    DEL = 2
};

struct Request {
    CommandType type;
    std::string key;
    std::string value;
};

struct Response {
    bool success;
    std::string value;
    std::string message;
};

// Node structure allocated in the Slab Allocator
inline uint64_t get_current_time_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

struct KVNode {
    uint32_t key_len;
    uint32_t val_len;
    std::atomic<uint64_t> last_access; // For Approximated LRU (timestamp in nanoseconds/microseconds)
    uint64_t expire_at = 0;            // Expiration timestamp in nanoseconds. 0 = no expiry.
    
    bool is_expired() const {
        if (expire_at == 0) return false;
        return get_current_time_ns() >= expire_at;
    }
    
    // Inline data: key first, then value
    char* key_ptr() {
        return reinterpret_cast<char*>(this + 1);
    }
    
    const char* key_ptr() const {
        return reinterpret_cast<const char*>(this + 1);
    }
    
    char* val_ptr() {
        return reinterpret_cast<char*>(this + 1) + key_len;
    }
    
    const char* val_ptr() const {
        return reinterpret_cast<const char*>(this + 1) + key_len;
    }
    
    std::string_view get_key() const {
        return std::string_view(key_ptr(), key_len);
    }
    
    std::string_view get_val() const {
        return std::string_view(val_ptr(), val_len);
    }
};

// WAL Entry structure for persistence
struct WALEntry {
    CommandType type;
    uint32_t key_len;
    uint32_t val_len;
    // Followed by key and value data in the file
};

#endif // COMMON_H

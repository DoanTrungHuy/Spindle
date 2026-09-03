#ifndef SHARDED_MAP_H
#define SHARDED_MAP_H

#include "common.h"
#include "slab_allocator.h"
#include <unordered_map>
#include <string_view>
#include <cstring>
#include <atomic>
#include <random>

class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
    // For compatibility with std::unique_lock and std::shared_lock, 
    // although we will just use it exclusively for maximum speed
    void lock_shared() { lock(); }
    void unlock_shared() { unlock(); }
};

class ShardedMap {
public:
    static constexpr size_t NUM_SHARDS = 256;
    static constexpr size_t MAX_SHARD_CAPACITY = 2000; // Trigger LRU eviction when shard exceeds this
    static constexpr size_t LRU_SAMPLE_SIZE = 5;       // Number of random samples to check for LRU

    struct alignas(64) Shard {
        std::unordered_map<std::string_view, KVNode*> table;
        mutable SpinLock mutex;
    };

    explicit ShardedMap(SlabAllocator& allocator);
    ~ShardedMap();

    // Prevent copying
    ShardedMap(const ShardedMap&) = delete;
    ShardedMap& operator=(const ShardedMap&) = delete;

    // Operations
    bool get(std::string_view key, std::string& value);
    
    template <typename SuccessCallback>
    bool set(std::string_view key, std::string_view value, bool& evicted, std::string& evicted_key, SuccessCallback on_success, uint64_t ttl_ns = 0);
    
    template <typename SuccessCallback>
    bool del(std::string_view key, SuccessCallback on_success);

    // TTL operations
    int64_t ttl(std::string_view key);      // Returns remaining TTL in seconds. -1 = no expiry, -2 = key not found
    bool persist(std::string_view key);      // Remove TTL from a key
    void cleanup_expired_keys();             // Called by ExpiryCleaner background thread

    size_t size() const;
    void print_stats() const;

private:
    SlabAllocator& m_allocator;
    Shard m_shards[NUM_SHARDS];

    // Thread-safe random number generator helper
    uint32_t get_random_index(uint32_t max_val) const;

    // Hash function to map key to shard
    size_t get_shard_index(std::string_view key) const;

    // Evict one item from shard using approximated LRU
    void evict_lru(Shard& shard, std::string& evicted_key);
};

// Template implementations
template <typename SuccessCallback>
inline bool ShardedMap::set(std::string_view key, std::string_view value, bool& evicted, std::string& evicted_key, SuccessCallback on_success, uint64_t ttl_ns) {
    size_t shard_idx = get_shard_index(key);
    Shard& shard = m_shards[shard_idx];

    std::unique_lock<SpinLock> lock(shard.mutex);
    evicted = false;

    // Calculate expiration timestamp
    uint64_t exp = (ttl_ns > 0) ? (get_current_time_ns() + ttl_ns) : 0;

    auto it = shard.table.find(key);
    if (it != shard.table.end()) {
        KVNode* old_node = it->second;

        size_t new_size = sizeof(KVNode) + key.size() + value.size();
        void* mem = m_allocator.allocate(new_size);
        KVNode* new_node = new (mem) KVNode();
        new_node->key_len = static_cast<uint32_t>(key.size());
        new_node->val_len = static_cast<uint32_t>(value.size());
        new_node->last_access.store(get_current_time_ns(), std::memory_order_relaxed);
        new_node->expire_at = exp;
        std::memcpy(new_node->key_ptr(), key.data(), key.size());
        std::memcpy(new_node->val_ptr(), value.data(), value.size());

        shard.table.erase(it);
        size_t old_size = sizeof(KVNode) + old_node->key_len + old_node->val_len;
        m_allocator.deallocate(old_node, old_size);
        shard.table[new_node->get_key()] = new_node;
        
        on_success();
        return true;
    }

    if (shard.table.size() >= MAX_SHARD_CAPACITY) {
        evict_lru(shard, evicted_key);
        evicted = true;
    }

    size_t new_size = sizeof(KVNode) + key.size() + value.size();
    void* mem = m_allocator.allocate(new_size);
    KVNode* new_node = new (mem) KVNode();
    new_node->key_len = static_cast<uint32_t>(key.size());
    new_node->val_len = static_cast<uint32_t>(value.size());
    new_node->last_access.store(get_current_time_ns(), std::memory_order_relaxed);
    new_node->expire_at = exp;

    std::memcpy(new_node->key_ptr(), key.data(), key.size());
    std::memcpy(new_node->val_ptr(), value.data(), value.size());

    shard.table[new_node->get_key()] = new_node;
    
    on_success();
    return true;
}

template <typename SuccessCallback>
inline bool ShardedMap::del(std::string_view key, SuccessCallback on_success) {
    size_t shard_idx = get_shard_index(key);
    Shard& shard = m_shards[shard_idx];

    std::unique_lock<SpinLock> lock(shard.mutex);
    auto it = shard.table.find(key);
    if (it == shard.table.end()) {
        return false;
    }

    KVNode* node = it->second;
    shard.table.erase(it);

    size_t total_size = sizeof(KVNode) + node->key_len + node->val_len;
    m_allocator.deallocate(node, total_size);
    
    on_success();
    return true;
}

#endif // SHARDED_MAP_H

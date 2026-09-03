#include "sharded_map.h"
#include <iostream>
#include <limits>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <cstring>

ShardedMap::ShardedMap(SlabAllocator& allocator)
    : m_allocator(allocator) {
}

ShardedMap::~ShardedMap() {
    // Clean up all allocated nodes in all shards
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        Shard& shard = m_shards[i];
        std::lock_guard<SpinLock> lock(shard.mutex);
        for (auto& [key, node] : shard.table) {
            size_t total_size = sizeof(KVNode) + node->key_len + node->val_len;
            m_allocator.deallocate(node, total_size);
        }
        shard.table.clear();
    }
}

size_t ShardedMap::get_shard_index(std::string_view key) const {
    return std::hash<std::string_view>{}(key) % NUM_SHARDS;
}

uint32_t ShardedMap::get_random_index(uint32_t max_val) const {
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<uint32_t> distribution(0, max_val - 1);
    return distribution(generator);
}

bool ShardedMap::get(std::string_view key, std::string& value) {
    size_t shard_idx = get_shard_index(key);
    Shard& shard = m_shards[shard_idx];

    std::unique_lock<SpinLock> lock(shard.mutex);
    auto it = shard.table.find(key);
    if (it == shard.table.end()) {
        return false;
    }

    KVNode* node = it->second;

    // Lazy expiry: if key has expired, delete it silently
    if (node->is_expired()) {
        shard.table.erase(it);
        size_t total_size = sizeof(KVNode) + node->key_len + node->val_len;
        m_allocator.deallocate(node, total_size);
        return false;
    }

    // Update access time
    node->last_access.store(get_current_time_ns(), std::memory_order_relaxed);
    
    value = std::string(node->get_val());
    return true;
}



void ShardedMap::evict_lru(Shard& shard, std::string& evicted_key) {
    size_t bucket_count = shard.table.bucket_count();
    size_t sampled = 0;
    KVNode* best_node = nullptr;
    uint64_t oldest_time = std::numeric_limits<uint64_t>::max();

    // 1. Try to sample from random buckets to avoid full scan (O(1) sampling)
    if (bucket_count > 0) {
        for (size_t i = 0; i < LRU_SAMPLE_SIZE * 3 && sampled < LRU_SAMPLE_SIZE; ++i) {
            uint32_t bucket = get_random_index(static_cast<uint32_t>(bucket_count));
            size_t bucket_size = shard.table.bucket_size(bucket);
            if (bucket_size > 0) {
                // Get first item of the bucket
                auto it = shard.table.begin(bucket);
                if (it != shard.table.end(bucket)) {
                    KVNode* node = it->second;
                    uint64_t access_time = node->last_access.load(std::memory_order_relaxed);
                    if (access_time < oldest_time) {
                        oldest_time = access_time;
                        best_node = node;
                    }
                    sampled++;
                }
            }
        }
    }

    // 2. Fallback to scan from begin() if bucket sampling didn't get enough samples
    if (sampled == 0 && !shard.table.empty()) {
        for (auto it = shard.table.begin(); it != shard.table.end() && sampled < LRU_SAMPLE_SIZE; ++it) {
            KVNode* node = it->second;
            uint64_t access_time = node->last_access.load(std::memory_order_relaxed);
            if (access_time < oldest_time) {
                oldest_time = access_time;
                best_node = node;
            }
            sampled++;
        }
    }

    // 3. Evict the oldest accessed node
    if (best_node != nullptr) {
        evicted_key = std::string(best_node->get_key());
        shard.table.erase(best_node->get_key());
        size_t total_size = sizeof(KVNode) + best_node->key_len + best_node->val_len;
        m_allocator.deallocate(best_node, total_size);
    }
}

size_t ShardedMap::size() const {
    size_t total_size = 0;
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        std::shared_lock<SpinLock> lock(m_shards[i].mutex);
        total_size += m_shards[i].table.size();
    }
    return total_size;
}

void ShardedMap::print_stats() const {
    std::cout << "=== Sharded Map Statistics ===\n";
    size_t total_keys = 0;
    size_t min_keys = std::numeric_limits<size_t>::max();
    size_t max_keys = 0;
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        std::shared_lock<SpinLock> lock(m_shards[i].mutex);
        size_t shard_size = m_shards[i].table.size();
        total_keys += shard_size;
        min_keys = std::min(min_keys, shard_size);
        max_keys = std::max(max_keys, shard_size);
    }
    std::cout << "Total Keys: " << total_keys << "\n";
    std::cout << "Average keys per shard: " << (total_keys / NUM_SHARDS) << "\n";
    std::cout << "Min keys in shard: " << min_keys << ", Max keys in shard: " << max_keys << "\n";
    std::cout << "==============================\n";
}

int64_t ShardedMap::ttl(std::string_view key) {
    size_t shard_idx = get_shard_index(key);
    Shard& shard = m_shards[shard_idx];

    std::unique_lock<SpinLock> lock(shard.mutex);
    auto it = shard.table.find(key);
    if (it == shard.table.end()) {
        return -2; // Key not found
    }

    KVNode* node = it->second;

    // If expired, clean up and report not found
    if (node->is_expired()) {
        shard.table.erase(it);
        size_t total_size = sizeof(KVNode) + node->key_len + node->val_len;
        m_allocator.deallocate(node, total_size);
        return -2;
    }

    if (node->expire_at == 0) {
        return -1; // Key exists but has no TTL (lives forever)
    }

    // Return remaining time in seconds
    uint64_t now = get_current_time_ns();
    uint64_t remaining_ns = node->expire_at - now;
    return static_cast<int64_t>(remaining_ns / 1'000'000'000ULL);
}

bool ShardedMap::persist(std::string_view key) {
    size_t shard_idx = get_shard_index(key);
    Shard& shard = m_shards[shard_idx];

    std::unique_lock<SpinLock> lock(shard.mutex);
    auto it = shard.table.find(key);
    if (it == shard.table.end()) {
        return false;
    }

    KVNode* node = it->second;

    if (node->is_expired()) {
        shard.table.erase(it);
        size_t total_size = sizeof(KVNode) + node->key_len + node->val_len;
        m_allocator.deallocate(node, total_size);
        return false;
    }

    node->expire_at = 0; // Remove TTL
    return true;
}

void ShardedMap::cleanup_expired_keys() {
    static thread_local std::mt19937 rng(std::random_device{}());

    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        Shard& shard = m_shards[i];
        std::unique_lock<SpinLock> lock(shard.mutex);

        if (shard.table.empty()) continue;

        // Sample up to 20 random keys per shard and delete expired ones
        size_t bucket_count = shard.table.bucket_count();
        size_t checked = 0;
        size_t expired_count = 0;

        for (size_t attempt = 0; attempt < 60 && checked < 20; ++attempt) {
            uint32_t bucket = rng() % bucket_count;
            size_t bucket_size = shard.table.bucket_size(bucket);
            if (bucket_size == 0) continue;

            auto it = shard.table.begin(bucket);
            if (it != shard.table.end(bucket)) {
                KVNode* node = it->second;
                checked++;
                if (node->is_expired()) {
                    size_t total_size = sizeof(KVNode) + node->key_len + node->val_len;
                    shard.table.erase(it);
                    m_allocator.deallocate(node, total_size);
                    expired_count++;
                }
            }
        }
    }
}

#include "slab_allocator.h"
#include <iostream>
#include <algorithm>
#include <cassert>
#include <cstring>

SlabAllocator::SlabAllocator(size_t total_arena_size)
    : m_total_arena_size(total_arena_size) {
    
    // Allocate the raw memory arena
    m_arena_raw = new char[m_total_arena_size];
    std::memset(m_arena_raw, 0, m_total_arena_size);

    // Define slab classes: 64B, 128B, 256B, 512B, 1024B, 2048B, 4096B
    std::vector<size_t> block_sizes = {64, 128, 256, 512, 1024, 2048, 4096};
    size_t num_classes = NUM_CLASSES;
    size_t size_per_class = m_total_arena_size / num_classes;

    char* current_ptr = m_arena_raw;
    for (size_t i = 0; i < num_classes; ++i) {
        m_classes[i].block_size = block_sizes[i];
        m_classes[i].arena_start = current_ptr;
        
        // Calculate total blocks for this class
        size_t total_blocks = size_per_class / block_sizes[i];
        m_classes[i].total_blocks = total_blocks;
        m_classes[i].allocated_blocks = 0;
        
        // Initialize the free list
        if (total_blocks > 0) {
            m_classes[i].free_list_head = reinterpret_cast<Slot*>(current_ptr);
            Slot* curr_slot = m_classes[i].free_list_head;
            for (size_t j = 0; j < total_blocks - 1; ++j) {
                char* next_block = reinterpret_cast<char*>(curr_slot) + block_sizes[i];
                curr_slot->next = reinterpret_cast<Slot*>(next_block);
                curr_slot = curr_slot->next;
            }
            curr_slot->next = nullptr;
        } else {
            m_classes[i].free_list_head = nullptr;
        }

        current_ptr += size_per_class;
    }
}

SlabAllocator::~SlabAllocator() {
    delete[] m_arena_raw;
}

int SlabAllocator::get_class_index(size_t size) const {
    for (size_t i = 0; i < NUM_CLASSES; ++i) {
        if (size <= m_classes[i].block_size) {
            return static_cast<int>(i);
        }
    }
    return -1; // Exceeds max slab size
}

struct ThreadCache {
    SlabAllocator::Slot* free_lists[SlabAllocator::NUM_CLASSES] = {nullptr};
    size_t counts[SlabAllocator::NUM_CLASSES] = {0};
};

static thread_local ThreadCache t_cache;
static constexpr size_t TCACHE_BATCH_SIZE = 16;
static constexpr size_t TCACHE_MAX_SIZE = 32;

void* SlabAllocator::allocate(size_t size) {
    int class_idx = get_class_index(size);
    if (class_idx == -1) {
        return std::malloc(size);
    }

    // 1. Try to allocate from thread-local cache (lock-free)
    if (t_cache.free_lists[class_idx] != nullptr) {
        Slot* allocated_slot = t_cache.free_lists[class_idx];
        t_cache.free_lists[class_idx] = allocated_slot->next;
        t_cache.counts[class_idx]--;
        return reinterpret_cast<void*>(allocated_slot);
    }

    // 2. Cache miss: fetch a batch of blocks from central slab class under lock
    SlabClass& sc = m_classes[class_idx];
    std::lock_guard<std::mutex> lock(sc.mutex);

    if (sc.free_list_head == nullptr) {
        // Central slab class exhausted, fallback to std::malloc
        return std::malloc(size);
    }

    // Fetch up to TCACHE_BATCH_SIZE blocks
    size_t fetched = 0;
    Slot* batch_head = sc.free_list_head;
    Slot* batch_tail = batch_head;
    
    while (batch_tail != nullptr && fetched < TCACHE_BATCH_SIZE - 1) {
        Slot* next = batch_tail->next;
        if (next == nullptr) break;
        batch_tail = next;
        fetched++;
    }

    // Detach the batch from the central free list
    sc.free_list_head = batch_tail->next;
    batch_tail->next = nullptr;
    sc.allocated_blocks += (fetched + 1);

    // Store the rest of the batch in thread-local cache
    if (fetched > 0) {
        t_cache.free_lists[class_idx] = batch_head->next;
        t_cache.counts[class_idx] = fetched;
    }

    return reinterpret_cast<void*>(batch_head);
}

void SlabAllocator::deallocate(void* ptr, size_t size) {
    if (ptr == nullptr) return;

    int class_idx = get_class_index(size);
    if (class_idx == -1) {
        std::free(ptr);
        return;
    }

    SlabClass& sc = m_classes[class_idx];
    
    // Check if pointer is within the bounds of this slab class
    char* char_ptr = reinterpret_cast<char*>(ptr);
    size_t size_per_class = m_total_arena_size / NUM_CLASSES;
    if (char_ptr < sc.arena_start || char_ptr >= sc.arena_start + size_per_class) {
        std::free(ptr);
        return;
    }

    // 1. Push to thread-local cache if it has space (lock-free)
    if (t_cache.counts[class_idx] < TCACHE_MAX_SIZE) {
        Slot* freed_slot = reinterpret_cast<Slot*>(ptr);
        freed_slot->next = t_cache.free_lists[class_idx];
        t_cache.free_lists[class_idx] = freed_slot;
        t_cache.counts[class_idx]++;
        return;
    }

    // 2. Local cache is full: flush a batch of blocks back to the central slab class
    std::lock_guard<std::mutex> lock(sc.mutex);
    
    // Pop TCACHE_BATCH_SIZE blocks from local cache
    Slot* flush_head = t_cache.free_lists[class_idx];
    Slot* flush_tail = flush_head;
    for (size_t i = 0; i < TCACHE_BATCH_SIZE - 1; ++i) {
        flush_tail = flush_tail->next;
    }

    // Update local cache pointers
    t_cache.free_lists[class_idx] = flush_tail->next;
    t_cache.counts[class_idx] -= TCACHE_BATCH_SIZE;

    // Attach flushed batch back to central free list
    flush_tail->next = sc.free_list_head;
    sc.free_list_head = flush_head;
    sc.allocated_blocks -= TCACHE_BATCH_SIZE;

    // Finally, store the current freed block in local cache
    Slot* freed_slot = reinterpret_cast<Slot*>(ptr);
    freed_slot->next = t_cache.free_lists[class_idx];
    t_cache.free_lists[class_idx] = freed_slot;
    t_cache.counts[class_idx]++;
}

void SlabAllocator::print_stats() const {
    std::cout << "=== Slab Allocator Statistics ===\n";
    for (size_t i = 0; i < NUM_CLASSES; ++i) {
        const SlabClass& sc = m_classes[i];
        std::cout << "Slab Class " << i << " (Block Size: " << sc.block_size << " B): "
                  << "Allocated " << sc.allocated_blocks << "/" << sc.total_blocks 
                  << " (" << (sc.total_blocks > 0 ? (sc.allocated_blocks * 100 / sc.total_blocks) : 0) << "%)\n";
    }
    std::cout << "=================================\n";
}

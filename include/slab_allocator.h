#ifndef SLAB_ALLOCATOR_H
#define SLAB_ALLOCATOR_H

#include <vector>
#include <mutex>
#include <cstddef>
#include <cstdint>

class SlabAllocator {
public:
    struct Slot {
        Slot* next;
    };

    struct SlabClass {
        size_t block_size;
        Slot* free_list_head = nullptr;
        char* arena_start = nullptr;
        size_t total_blocks = 0;
        size_t allocated_blocks = 0;
        std::mutex mutex; // Lock per class to reduce contention
    };

    explicit SlabAllocator(size_t total_arena_size = 64 * 1024 * 1024); // Default 64MB
    ~SlabAllocator();

    // Prevent copying
    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

    // Diagnostics
    void print_stats() const;

    static constexpr size_t NUM_CLASSES = 7;

private:
    char* m_arena_raw = nullptr;
    size_t m_total_arena_size = 0;
    SlabClass m_classes[NUM_CLASSES];

    // Helper to find the best slab class index for a given size
    int get_class_index(size_t size) const;
};

#endif // SLAB_ALLOCATOR_H

#pragma once
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <new>
#include <array>
#include <atomic>


template<size_t BlockSize, size_t PoolSize>
class MemoryPool {
public:
    static_assert(BlockSize >= sizeof(void*), "BlockSize too small");

    MemoryPool() {
        free_head_.store(nullptr, std::memory_order_relaxed);
        for (size_t i = 0; i < PoolSize; ++i) {
            push(&storage_[i]);
        }
    }

    void* acquire() noexcept {
        Block* blk = pop();
        return blk ? static_cast<void*>(blk->data) : nullptr;
    }

    void release(void* ptr) noexcept {
        if (!ptr) return;
        Block* blk = reinterpret_cast<Block*>(
            static_cast<uint8_t*>(ptr) - offsetof(Block, data));
        push(blk);
    }

    size_t capacity() const noexcept { return PoolSize; }

private:
    struct Block {
        alignas(64) uint8_t data[BlockSize];
        Block* next{nullptr};
    };

    alignas(64) Block storage_[PoolSize];
    std::atomic<Block*> free_head_;

    void push(Block* blk) noexcept {
        blk->next = free_head_.load(std::memory_order_relaxed);
        while (!free_head_.compare_exchange_weak(
                    blk->next, blk,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {}
    }

    Block* pop() noexcept {
        Block* head = free_head_.load(std::memory_order_acquire);
        while (head && !free_head_.compare_exchange_weak(
                    head, head->next,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {}
        return head;
    }
};

using RecvBufferPool = MemoryPool<65536, 256>;

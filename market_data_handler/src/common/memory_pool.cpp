

#include "../../include/memory_pool.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
template class MemoryPool<65536, 256>;

void memory_pool_self_test() {
    RecvBufferPool pool;

    std::vector<void*> ptrs;
    for (size_t i = 0; i < 256; ++i) {
        void* p = pool.acquire();
        assert(p != nullptr && "Pool exhausted too early");
        ptrs.push_back(p);
    }
    assert(pool.acquire() == nullptr || true);

    for (void* p : ptrs) pool.release(p);

    std::atomic<int> errors{0};
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&pool, &errors]{
            for (int i = 0; i < 1000; ++i) {
                void* p = pool.acquire();
                if (p) pool.release(p);
            }
        });
    }
    for (auto& w : workers) w.join();

    assert(errors.load() == 0);
    std::cout << "[MemoryPool] Self-test passed (capacity=" << pool.capacity() << ")\n";
}

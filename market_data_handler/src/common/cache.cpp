

#include "../../include/cache.h"
#include <iostream>
#include <iomanip>


void dump_cache(const SymbolCache& cache,
                uint16_t from_sym, uint16_t to_sym,
                const char* const* symbol_names)
{
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Symbol       Bid         Ask         LTP         Updates\n";
    std::cout << "─────────────────────────────────────────────────────────\n";

    for (uint16_t i = from_sym; i <= to_sym && i < SymbolCache::MAX_SYMBOLS; ++i) {
        auto snap = cache.get_snapshot(i);
        if (snap.update_count == 0) continue;

        const char* name = (symbol_names && symbol_names[i])
                           ? symbol_names[i] : "???";

        std::cout << std::left  << std::setw(12) << name
                  << std::right << std::setw(12) << snap.best_bid
                  << std::right << std::setw(12) << snap.best_ask
                  << std::right << std::setw(12) << snap.last_traded_price
                  << std::right << std::setw(12) << snap.update_count
                  << "\n";
    }
}

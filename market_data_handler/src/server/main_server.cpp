#include "exchange_simulator.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <stdexcept>

static ExchangeSimulator* g_sim = nullptr;

static void signal_handler(int) {
    std::cout << "\n[Server] Shutting down...\n";
    if (g_sim) g_sim->stop();
}

int main(int argc, char* argv[]) {
    uint16_t port       = 9876;
    size_t   num_sym    = 100;
    uint32_t tick_rate  = 100000;
    bool     fault_inj  = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port"       && i+1 < argc) port      = std::stoi(argv[++i]);
        if (arg == "--symbols"    && i+1 < argc) num_sym   = std::stoul(argv[++i]);
        if (arg == "--rate"       && i+1 < argc) tick_rate = std::stoul(argv[++i]);
        if (arg == "--fault")                    fault_inj = true;
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);  // ignore broken pipe

    try {
        ExchangeSimulator sim(port, num_sym);
        g_sim = &sim;
        sim.set_tick_rate(tick_rate);
        sim.enable_fault_injection(fault_inj);
        sim.start();
        sim.run();
    } catch (const std::exception& e) {
        std::cerr << "[Server] Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

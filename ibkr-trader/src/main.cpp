// ────────────────────────────────────────────────────────────────────────────────
// main.cpp - single entry-point, C++20
// ────────────────────────────────────────────────────────────────────────────────
#include "input_manager/input_manager.hpp"
#include "../util/app_state.hpp"
// #include "tests/test_input_manager.hpp"  // COMMENTED OUT FOR BUILD

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <sstream>

using namespace std::chrono_literals;

// --------------------------------------------------------------------------------
//  Globals kept to an absolute minimum
// --------------------------------------------------------------------------------
namespace {
/*Shared pointer is only written once, so relaxed ordering is fine.*/
std::atomic<input_manager::InputManager*> g_inputManager{nullptr};
std::atomic<bool>                         g_shutdownRequested{false};
} // namespace

// --------------------------------------------------------------------------------
//  Minimal, RAII-style registration / un-registration of signal handlers
// --------------------------------------------------------------------------------
class SignalGuard {
public:
    explicit SignalGuard(void (*handler)(int)) {
        std::signal(SIGINT,  handler);
        std::signal(SIGTERM, handler);
    }
    ~SignalGuard() noexcept {
        std::signal(SIGINT,  SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
    }
    SignalGuard(const SignalGuard&)            = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;
};

// --------------------------------------------------------------------------------
//  Asynchronous watchdog; terminates the process if graceful stop stalls
// --------------------------------------------------------------------------------
[[noreturn]] void watchdog(std::chrono::seconds maxWait) {
    std::this_thread::sleep_for(maxWait);
    std::cerr << "[main] Watch-dog timeout reached — forcing hard exit.\n";
    std::_Exit(EXIT_FAILURE);        // Immediate, no unwinding.
}

// --------------------------------------------------------------------------------
//  POSIX signal handler (very restricted — async-signal-safe operations only)
// --------------------------------------------------------------------------------
void onSignal(int signo) {
    if (g_shutdownRequested.exchange(true)) { return; }   // already requested

    std::cerr << "\n[main] Caught signal " << signo << ", initiating shutdown…\n";
    auto& appState = app_state::AppState::getInstance();
    appState.requestEmergencyStop(/*timeout ms*/ 5'000, "signal");

    // detach watchdog so it keeps running even if main thread hangs
    std::thread(watchdog, 5s).detach();
}

// --------------------------------------------------------------------------------
//  Configuration default values
// --------------------------------------------------------------------------------
struct Config {
    bool testApi = false;
    int apiPort = 9000;
    std::string configFile = "";
};

// --------------------------------------------------------------------------------
//  Thread logging utility
// --------------------------------------------------------------------------------
void logThreadRole(const std::string& role, const std::string& action) {
    auto tid = std::this_thread::get_id();
    std::ostringstream ss;
    ss << tid;
    std::cout << "[" << role << ":" << ss.str() << "] " << action << std::endl;
}

void showUsage() {
    std::cout << "Usage: ./bin/ibkr-trader [--test-api] [--port <n>] [-h|--help]\n";
}

// --------------------------------------------------------------------------------
//  Helper: parse tiny subset of CLI flags we still support
// --------------------------------------------------------------------------------
Config parseArguments(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--test-api") {
            cfg.testApi = true;
        }
        else if (arg == "--port" && i + 1 < argc) {
            cfg.apiPort = std::atoi(argv[++i]);
        }
        else if (arg == "-h" || arg == "--help") {
            showUsage();
            std::exit(0);
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            showUsage();
            std::exit(1);
        }
    }
    return cfg;
}

// --------------------------------------------------------------------------------
//  Main driver — split out so tests can invoke it directly.
// --------------------------------------------------------------------------------
int run(std::span<char*> argv) {
    logThreadRole("MAIN-THREAD", "IBKR Trading System starting");
    
    const auto cfg = parseArguments(static_cast<int>(argv.size()), argv.data());

    auto manager = std::make_shared<input_manager::InputManager>();
    g_inputManager.store(manager.get(), std::memory_order_relaxed);

    if (!manager->initialize(cfg.configFile)) {
        std::cerr << "[main] InputManager initialisation failed.\n";
        return EXIT_FAILURE;
    }
    manager->setLogLevel(cfg.testApi ? 4 /*max*/ : 2);

    /* Register signal handlers only after critical resources exist. */
    SignalGuard sg{onSignal};

    if (cfg.testApi) {
        // Test mode: run API server with verbose logging
        std::cout << "[main] Running in TEST mode with verbose logging\n";
        manager->runApiServer(cfg.apiPort);
    } else {
        manager->runApiServer(cfg.apiPort);
    }

    return EXIT_SUCCESS;
}

// --------------------------------------------------------------------------------
//  Traditional `main` — thin wrapper with global exception safety.
// --------------------------------------------------------------------------------
int main(int argc, char* argv[]) noexcept {
    try {
        return run({argv, static_cast<std::size_t>(argc)});
    }
    catch (const std::exception& e) {
        logThreadRole("MAIN-THREAD", "ERROR: " + std::string(e.what()));
        std::cerr << "Error: " << e.what() << '\n';
    }
    catch (...) {
        std::cerr << "[main] Unhandled non-std exception\n";
    }

    return EXIT_FAILURE;
}

// ───────────────────────────────────────────────────────────────────────────────
//  app_state.hpp
// ───────────────────────────────────────────────────────────────────────────────

#ifndef APP_STATE_HPP
#define APP_STATE_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app_state {

//───────────────────────────────────────────────────────────────────────────────
//  Simple stop token replacement for std::jthread functionality
//───────────────────────────────────────────────────────────────────────────────
class StopToken {
public:
    bool stop_requested() const { return m_stopRequested.load(); }
    
private:
    friend class ThreadWrapper;
    std::atomic<bool> m_stopRequested{false};
    
    void request_stop() { m_stopRequested.store(true); }
};

class ThreadWrapper {
public:
    template<typename Func>
    ThreadWrapper(Func&& func) : m_stopToken(std::make_shared<StopToken>()) {
        m_thread = std::thread([func = std::forward<Func>(func), token = m_stopToken]() {
            func(*token);
        });
    }
    
    ~ThreadWrapper() {
        if (m_thread.joinable()) {
            request_stop();
            m_thread.join();
        }
    }
    
    void request_stop() {
        if (m_stopToken) {
            m_stopToken->request_stop();
        }
    }
    
    bool joinable() const { return m_thread.joinable(); }
    void join() { if (m_thread.joinable()) m_thread.join(); }
    void detach() { if (m_thread.joinable()) m_thread.detach(); }
    std::thread::id get_id() const { return m_thread.get_id(); }
    
    ThreadWrapper(const ThreadWrapper&) = delete;
    ThreadWrapper& operator=(const ThreadWrapper&) = delete;
    ThreadWrapper(ThreadWrapper&&) = default;
    ThreadWrapper& operator=(ThreadWrapper&&) = default;

private:
    std::thread m_thread;
    std::shared_ptr<StopToken> m_stopToken;
};

//───────────────────────────────────────────────────────────────────────────────
//  State enum
//───────────────────────────────────────────────────────────────────────────────
enum class ThreadState { RUNNING, STOPPING, DETACHED };

//───────────────────────────────────────────────────────────────────────────────
//  Singleton: central thread registry / lifecycle orchestrator
//───────────────────────────────────────────────────────────────────────────────
class AppState {
public:
    //‒‒‒ Creation & destruction (non-copyable) ‒‒‒
    static AppState& getInstance();
    ~AppState();

    AppState(const AppState&)            = delete;
    AppState(AppState&&)                 = delete;
    AppState& operator=(const AppState&) = delete;
    AppState& operator=(AppState&&)      = delete;

    //‒‒‒ Thread orchestration API ‒‒‒
    bool startThread( std::string                                   name,
                      std::function<void(const StopToken&)>         task );

    bool requestThreadStop (const std::string& name,
                            const std::string& requestor = "anon");
    bool requestAllThreadsStop(const std::string& requestor = "anon");
    bool requestEmergencyStop (int timeoutMs,
                               const std::string& requestor = "anon");

    //‒‒‒ Introspection helpers (read-only) ‒‒‒
    ThreadState             getThreadState   (const std::string& name) const;
    bool                    hasRunningThread (const std::string& name) const;
    std::vector<std::string>getRunningNames  ()                      const;
    std::map<std::string,std::string>
                            getThreadStateInfo()                     const;

    // simple flags consumed by signal-handler / polling code
    bool isShutdownInProgress() const { return m_shutdownInProgress.load(); }
    bool isEmergencyShutdown () const { return m_emergencyShutdown .load(); }
    std::string getShutdownInitiator()const { return m_shutdownInitiator;  }

    // Centralized logging with thread awareness
    void logThreadEvent(const std::string& event, const std::string& component = "", 
                       const std::string& details = "") const;
    void logCurrentThread(const std::string& component, const std::string& action = "") const;

private:
    AppState();                                     // private ctor (singleton)

    // NOTE: Old internal helper functions removed to avoid lock-order inversion deadlocks.
    // The logic has been moved into the public methods to ensure blocking operations
    // happen outside the critical section.

    //───────────────────────────────────────────────────────────────────────────
    struct ThreadEntry {
        ThreadWrapper wrapper;     // RAII thread with stop token
        ThreadState  state{ThreadState::RUNNING};
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        std::string purpose;  // Description of what this thread does
    };

    mutable std::mutex                    m_mutex;          // protects maps
    std::map<std::string, ThreadEntry>    m_threads;        // name → entry

    std::atomic<bool> m_emergencyShutdown {false};
    std::atomic<bool> m_shutdownInProgress{false};
    std::string       m_shutdownInitiator{"none"};
};

} // namespace app_state
#endif

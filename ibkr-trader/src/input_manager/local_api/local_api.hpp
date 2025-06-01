#pragma once
// ────────────────────────────────────────────────────────────────────────────────
//  local_api.hpp – lightweight single-thread HTTP façade
// ────────────────────────────────────────────────────────────────────────────────
#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace input_manager { class InputManager; }

namespace local_api {

class LocalAPI {
public:
    LocalAPI();
    ~LocalAPI();

    /* one-off init (optional config file in JSON) */
    bool initialize(const std::string& cfgPath = "");

    /* lifecycle – *blocking* HTTP server on its own worker thread               */
    bool start(int port = 9'000);        // returns immediately
    void stop();

    /* queue -> confirm workflow (thread-safe)                                    */
    bool queueTradingRequest (const nlohmann::json& req);
    bool confirmQueuedRequests();

    /* direct immediate execution (legacy)                                        */
    bool processTradingRequest (const nlohmann::json& req);
    bool processBatchRequests  (const std::vector<nlohmann::json>& batch);

    /* symbol helpers (no recursion into InputManager to avoid tight coupling)    */
    bool clearSymbol  (const std::string& symbol);
    void clearAllRequests();

    /* status endpoints                                                           */
    nlohmann::json getStatus()            const;
    nlohmann::json getFormattedRequests() const;
    nlohmann::json getPendingRequests()   const;
    nlohmann::json getSymbolQueueData(const std::string& symbol) const;

    /* misc settings                                                              */
    void setLogLevel(int lvl);
    void setAutoConfirm(bool ac);   bool getAutoConfirm() const;

    /* callbacks back to InputManager                                             */
    void registerTradeCallback (std::function<void(const nlohmann::json&)> cb);
    void registerErrorCallback (std::function<void(const std::string&,
                                                   const std::string&)> cb);

    /* hard kill                                                                  */
    void emergencyStop();

    /* back reference into owning InputManager (weak)                             */
    void setInputManager(std::shared_ptr<input_manager::InputManager> im);

private:
    /* HTTP plumbing                                                              */
    void        serverWorker(int port);
    void        handleClient(int sock);
    std::string processHttpRequest(const std::string& raw);
    std::string buildHttpResp(int code, std::string_view body,
                              std::string_view mime = "application/json") const;

    /* helpers                                                                    */
    bool validateRequest (const nlohmann::json&) const;
    bool executeRequest  (const nlohmann::json&);

    void notifyParent();                        // push queue -> tradeCallback
    void log(int lvl, std::string_view msg) const;
    bool parseConfig(const std::string& path);
    void printHelpBanner(int port) const;       // ←-- required banner

    /* data                                                                       */
    std::weak_ptr<input_manager::InputManager> m_parent;

    nlohmann::json              m_cfg;
    std::vector<nlohmann::json> m_queue;
    std::vector<nlohmann::json> m_pending;
    mutable std::mutex          m_mx;           // guards queues

    int                m_logLvl{0};
    bool               m_autoConfirm{false};
    bool               m_running{false};
    std::atomic<int>   m_sock{-1};
    std::thread        m_worker;

    // last connection diagnostics (populated by confirmQueuedRequests)
    nlohmann::json     m_ibkrConnDiag;
    bool               m_ibkrProblems{false};

    std::function<void(const nlohmann::json&)>          m_tradeCB;
    std::function<void(const std::string&,std::string)> m_errorCB;
};

} // namespace local_api

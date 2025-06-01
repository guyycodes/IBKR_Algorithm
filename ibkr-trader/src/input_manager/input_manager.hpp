#pragma once
// ────────────────────────────────────────────────────────────────────────────────
//  input_manager.hpp  –  HTTP-only glue between LocalAPI (front-end) and the
//  ModelManager / AppState thread network.
// ────────────────────────────────────────────────────────────────────────────────
#include "local_api/local_api.hpp"
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace input_manager {

enum class InputSource { NONE, API };

class InputManager : public std::enable_shared_from_this<InputManager> {
public:
    InputManager();
    ~InputManager();

    // One-shot configuration + LocalAPI bootstrap
    bool initialize(const std::string& configPath = "");
    void runApiServer(int port = 9'000);          // blocking until <Enter> or SIGINT
    void stop();                                  // idempotent

    // REST-facing helpers (thread-safe, called from LocalAPI worker threads)
    bool processApiRequest (const nlohmann::json& single);
    bool processBatchRequests(const std::vector<nlohmann::json>& batch);

    // queue/confirm helpers (optional client workflow)
    bool queueApiRequest   (const nlohmann::json& req);
    bool confirmQueuedRequests();

    // symbol / model lifecycle helpers
    bool clearSymbol (const std::string& symbol);
    void clearAllInputs();

    // observability
    void              setLogLevel(int level);
    [[nodiscard]] int getLogLevel() const { return m_logLevel; }

    // status endpoints
    nlohmann::json getStatus () const;
    nlohmann::json getOutput () const;
    nlohmann::json getPendingRequests() const;

    // call from LocalAPI routes when a fatal condition arises
    void emergencyStop();

private:
    /*— implementation —*/
    void          processOutput();                // core “bottleneck”
    void          log(int lvl, std::string_view msg) const;
    bool          parseConfig(const std::string& path);

    // state
    std::unique_ptr<local_api::LocalAPI> m_localApi;
    InputSource                          m_activeSource{InputSource::NONE};

    nlohmann::json m_config;
    nlohmann::json m_outputJson;

    int                                      m_logLevel{0};
};

} // namespace input_manager

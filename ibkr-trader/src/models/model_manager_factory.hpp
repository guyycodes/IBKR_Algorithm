// model_manager_factory.hpp
// ───────────────────────────────────────────────────────────────────────────────


#ifndef MODEL_MANAGER_FACTORY_HPP
#define MODEL_MANAGER_FACTORY_HPP
// ────────────────────────────────────────────────────────────────────────────────
//  ModelManagerFactory – central registry of ModelManager singletons *only*.
//  Thread lifecycle is no longer handled here; callers that need background
//  processing must invoke AppState::startThread() themselves (or pass
//  `startThread=true` to the helper overload – see below).
// ────────────────────────────────────────────────────────────────────────────────
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace model_manager {

class ModelManager;
enum  class TimeWindowUnit;

/*  Diagram (post-refactor)
    ┌─────────────────────────────────────────────────────────────────────────┐
    │  ModelManagerFactory          (Singleton, thread-safe)                 │
    │                                                                         │
    │  m_managers : std::map<std::string, std::shared_ptr<ModelManager>>      │
    │                                                                         │
    │  * createModelManager()        – constructs + stores instance           │
    │  * getModelManager()           – retrieves existing or creates (no I/O) │
    │  * removeModel()               – disconnects + forgets                  │
    │  * clearAll()                  – disconnects + forgets all              │
    │                                                                         │
    │  IMPORTANT:                                                            │
    │  – NO threads are spawned here.                                         │
    │  – InputManager (or other caller) decides whether to run a ModelManager │
    │    in a worker thread by calling:                                       │
    │        AppState::startThread(sym, [mm](std::stop_token t){…});          │
    └─────────────────────────────────────────────────────────────────────────┘
*/
class ModelManagerFactory {
public:
    // singleton access
    static ModelManagerFactory& getInstance();

    // non-copyable
    ModelManagerFactory(const ModelManagerFactory&)            = delete;
    ModelManagerFactory& operator=(const ModelManagerFactory&) = delete;

    /* CRUD interface (no threads) ------------------------------------------ */
    std::shared_ptr<ModelManager> createModelManager(const std::string& symbol,
                                                     size_t windowSize,
                                                     TimeWindowUnit unit);

    std::shared_ptr<ModelManager> getModelManager(const std::string& symbol);

    bool initModelFromJson(const std::string& symbol,
                           const nlohmann::json& jsonData,
                           size_t windowSize,
                           TimeWindowUnit unit);

    bool hasModel   (const std::string& symbol);
    bool removeModel(const std::string& symbol);

    std::vector<std::string>                         getAllSymbols() const;
    std::map<std::string,std::shared_ptr<ModelManager>> getAllModelManagers() const;

    void clearAll();   // disconnect + purge map

    /* Back-compat helper: optionally spin thread via AppState --------------- */
    std::shared_ptr<ModelManager> getModelManager(const std::string& symbol,
                                                  bool           startThread,
                                                  const nlohmann::json& params = {});

private:
    ModelManagerFactory() = default;

    static std::unique_ptr<ModelManagerFactory> s_instance;
    static std::mutex                           s_mxSingleton;

    std::map<std::string,std::shared_ptr<ModelManager>> m_managers;
    mutable std::mutex                                  m_mx; // protects map
};

} // namespace model_manager
#endif

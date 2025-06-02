// connection.hpp
// ───────────────────────────────────────────────────────────────

#pragma once
// ───────────────────────────────────────────────────────────────
//  Lightweight wrapper around IBKR's EClient / EWrapper pair
//  Provides just enough functionality for ModelManager &
//  ConnectionManager – everything else lives in higher layers.
// ───────────────────────────────────────────────────────────────
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "ibkr/cppclient/client/CommonDefs.h"
#include "ibkr/cppclient/client/DefaultEWrapper.h"
#include "ibkr/cppclient/client/EClientSocket.h"
#include "ibkr/cppclient/client/EReader.h"
#include "ibkr/cppclient/client/EReaderOSSignal.h"
#include "ibkr/cppclient/client/Contract.h"

#include "../decoder/frame_analyzer.hpp"
#include "../decoder/decoder.hpp"  

namespace model_manager { class ModelManager; }
namespace stock_data_tick { struct StockData; }          // full type in metrics_model

// Forward-declared heavy helpers (optional – plug in later)
// namespace ibkr_frame_analyzer { class FrameAnalyzer; }
// namespace ibkr_decoder         { class IBKRDecoder;   }

namespace connection {

//------------------------------------------------------------------------------
// Connection-wide constants
//------------------------------------------------------------------------------
inline constexpr const char* HOST = "host.docker.internal";
inline constexpr int         PORT = 4002;      // IBKR paper-trading gateway

//------------------------------------------------------------------------------
//  Utility: uniform, thread-aware log helper
//------------------------------------------------------------------------------
inline void logCb(const std::string& cb,
                  const std::string& sym,
                  const std::string& payload)
{
    std::ostringstream ssTid;
    ssTid << std::this_thread::get_id();
    std::cout << "[IBKR][" << cb << "][sym:" << sym
              << "][tid:" << ssTid.str() << "] " << payload << '\n';
}

//------------------------------------------------------------------------------
//  IBKRTrader – thin EWrapper implementation
//------------------------------------------------------------------------------
class IBKRTrader : public DefaultEWrapper {
public:
    IBKRTrader();                              // ctor / dtor
    ~IBKRTrader() override;

    /* connection lifecycle -------------------------------------------------- */
    bool connect (int clientId,
                  const std::string& symbol,
                  const Contract&    c);
    void disconnect();
    [[nodiscard]] bool isConnected() const { return m_client && m_client->isConnected(); }

    /* integration points ---------------------------------------------------- */
    std::unique_ptr<EReader> createReader();   // reader will be pumped by ConnectionManager
    EClientSocket*           getClient()      { return m_client.get(); }
    EReaderOSSignal&         getOSSignal()    { return m_osSignal; }
    
    void                     startDataStream(const std::string& symbol);   // convenience

    void                     setModelManager(model_manager::ModelManager* mgr,
                                             const std::string&            sym);

    /* DefaultEWrapper overrides (subset) ----------------------------------- */
    void tickString        (TickerId, TickType, const std::string&) override;
    void realtimeBar       (TickerId, long, double, double, double,
                            double, Decimal, Decimal, int)          override;
    void tickByTickAllLast (int, int, time_t, double, Decimal,
                            const TickAttribLast&,
                            const std::string&, const std::string&) override;
    void tickByTickBidAsk  (int, time_t, double, double,
                            Decimal, Decimal,
                            const TickAttribBidAsk&)               override;
    void error             (int id, long time, int code,
                            const std::string& msg,
                            const std::string& json)               override;

private:
    /* helper that converts raw info → StockData and pushes to ModelManager */
    void routeToModel(double   last,
                      double   bid,
                      double   ask,
                      int      bidSz,
                      int      askSz,
                      double   vol,
                      std::uint64_t ts_ms);

    /* members -------------------------------------------------------------- */
    EReaderOSSignal                               m_osSignal{0};  // 0ms = non-blocking
    std::unique_ptr<EClientSocket>                m_client;
    
    std::unique_ptr<ibkr_frame_analyzer::FrameAnalyzer> m_an;
    std::unique_ptr<ibkr_decoder::IBKRDecoder>          m_dec;

    model_manager::ModelManager*  m_mgr   {nullptr};
    std::string                   m_sym;
    Contract                      m_contract;
    int                           m_reqId {-1};

    /* misc state */
    std::atomic<double>           m_lastKnownPrice{0.0};
};

} // namespace connection

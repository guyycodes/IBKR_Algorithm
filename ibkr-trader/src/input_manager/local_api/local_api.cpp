// ────────────────────────────────────────────────────────────────────────────────
// local_api.cpp – minimal HTTP server (select + blocking I/O)
// ────────────────────────────────────────────────────────────────────────────────
#include "../../input_manager/local_api/local_api.hpp"
#include "../../../util/app_state.hpp"
#include "models/model_manager_factory.hpp"
#include "models/model_manager.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace std::chrono_literals;
namespace local_api {

// ────────────────────────────────────────────────────────────────────────────────
// ctor / dtor
// ────────────────────────────────────────────────────────────────────────────────
LocalAPI::LocalAPI() {
    m_tradeCB = [](auto&&){};           // no-op defaults
    m_errorCB = [](auto&&,auto&&){};
    log(0,"LocalAPI constructed");
}
LocalAPI::~LocalAPI(){ stop(); log(0,"LocalAPI destroyed"); }

// ────────────────────────────────────────────────────────────────────────────────
// initialisation
// ────────────────────────────────────────────────────────────────────────────────
bool LocalAPI::initialize(const std::string& cfgPath){
    return cfgPath.empty() || parseConfig(cfgPath);
}

// ────────────────────────────────────────────────────────────────────────────────
// start/stop
// ────────────────────────────────────────────────────────────────────────────────
bool LocalAPI::start(int port){
    if (m_running) { return true; }
    m_running = true;
    m_worker  = std::thread(&LocalAPI::serverWorker, this, port);
    printHelpBanner(port);              // ← crucial banner
    return true;
}
void LocalAPI::stop(){
    if (!m_running) return;
    m_running = false;

    // wake select/accept by closing the listening socket
    if (int s = m_sock.exchange(-1); s != -1) ::shutdown(s,SHUT_RDWR);

    if (m_worker.joinable()) m_worker.join();
}

/*─ server worker (select-loop on listen socket) ─*/
void LocalAPI::serverWorker(int port){
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { log(2,"socket err"); return; }

    int opt=1;  ::setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    ::fcntl(srv,F_SETFL, ::fcntl(srv,F_GETFL,0) | O_NONBLOCK);

    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(srv,(sockaddr*)&addr,sizeof(addr))<0
     || ::listen(srv,16)<0){
        log(2,"bind/listen err"); ::close(srv); return;
    }
    m_sock.store(srv);
    log(0,"HTTP listening on :"+std::to_string(port));

    fd_set rfds; timeval tv{};
    while (m_running){
        FD_ZERO(&rfds); FD_SET(srv,&rfds);
        tv.tv_sec=1; tv.tv_usec=0;
        if (::select(srv+1,&rfds,nullptr,nullptr,&tv)<=0) continue;
        if (!FD_ISSET(srv,&rfds)) continue;

        int cli = ::accept(srv,nullptr,nullptr);
        if (cli<0) continue;
        handleClient(cli);
        ::close(cli);
    }
    ::close(srv);
}

/*─ one request (no keep-alive) ─*/
void LocalAPI::handleClient(int sock){
    char buf[4096]; int n = ::read(sock,buf,sizeof(buf));
    if (n<=0) return;
    std::string resp = processHttpRequest({buf,(size_t)n});
    ::send(sock,resp.data(),resp.size(),0);
}

/*─ routing logic (GET/POST minimal parser) ─*/
std::string LocalAPI::processHttpRequest(const std::string& req){
    std::istringstream s(req); std::string verb,path,vers; s>>verb>>path>>vers;
    int code=404; std::string body="{\"error\":\"Not Found\"}";

    auto jsonOK=[&](nlohmann::json j){ code=200; body=j.dump(2); };

    if (verb=="GET"){
        if (path=="/status")       jsonOK(getStatus());
        else if (path=="/trades")  jsonOK(getFormattedRequests());
        else if (path=="/pending") jsonOK(getPendingRequests());
        else if (path.rfind("/queue-data?",0)==0){
            auto pos=path.find("symbol="); std::string sym;
            if (pos!=std::string::npos) sym=path.substr(pos+7);
            if (!sym.empty())      jsonOK(getSymbolQueueData(sym));
            else { code=400; body="{\"err\":\"symbol param required\"}"; }
        }
    } else if (verb=="POST"){
        auto bodyPos=req.find("\r\n\r\n"); std::string jBody;
        if (bodyPos!=std::string::npos) jBody=req.substr(bodyPos+4);
        nlohmann::json j;
        try{ j=nlohmann::json::parse(jBody.empty()?"{}":jBody); }catch(...){}
        if (path=="/trade") {
            if (queueTradingRequest(j)){
                if (m_autoConfirm) confirmQueuedRequests();
                jsonOK({{"status","queued"}});
            } else { code=400; body="{\"err\":\"queue failed\"}"; }
        }
        else if (path=="/confirm"){
            confirmQueuedRequests()?jsonOK({{"status","confirmed"}})
                                   :jsonOK({{"status","empty"}});
        }
        else if (path=="/clear"){ clearSymbol(j.value("symbol",""));
                                  jsonOK({{"status","cleared"}}); }
        else if (path=="/clear-all"){ clearAllRequests();
                                      jsonOK({{"status","cleared_all"}}); }
        else if (path=="/emergency-stop"){ emergencyStop();
                                           jsonOK({{"status","stop"}}); }
        else if (path=="/set-auto-confirm"){
            setAutoConfirm(j.value("autoConfirm",false));
            jsonOK({{"status","ok"},{"autoConfirm",m_autoConfirm}});
        }
    }

    return buildHttpResp(code,body);
}

/*─ HTTP formatter ─*/
std::string LocalAPI::buildHttpResp(int code,std::string_view body,
                                    std::string_view mime) const{
    std::string txt =(code==200)?"OK":(code==400)?"Bad Request":"Not Found";
    std::ostringstream o;
    o<<"HTTP/1.1 "<<code<<' '<<txt<<"\r\n"
     <<"Content-Type: "<<mime<<"\r\n"
     <<"Content-Length: "<<body.size()<<"\r\n"
     <<"Connection: close\r\n"
     <<"Access-Control-Allow-Origin: *\r\n\r\n"<<body;
    return o.str();
}

// ────────────────────────────────────────────────────────────────────────────────
//  queue / confirm / execution helpers
// ────────────────────────────────────────────────────────────────────────────────
bool LocalAPI::validateRequest(const nlohmann::json& j) const {
    return j.contains("symbol") && j.contains("params");
}
bool LocalAPI::queueTradingRequest(const nlohmann::json& j){
    if (!m_running) return false;
    if (!validateRequest(j))   return false;
    std::lock_guard lg(m_mx); m_pending.push_back(j);
    return true;
}
bool LocalAPI::confirmQueuedRequests(){
    std::vector<nlohmann::json> pend;
    {
        std::lock_guard lg(m_mx);
        pend.swap(m_pending);
        m_queue.insert(m_queue.end(), pend.begin(), pend.end());
    }
    for (auto& r : pend) executeRequest(r);
    notifyParent();
    return !pend.empty();
}
bool LocalAPI::executeRequest(const nlohmann::json& j){
    // fire trade callback (InputManager will in turn feed AppState)
    m_tradeCB({ { j["symbol"].get<std::string>(), j["params"] } });
    return true;
}

// ────────────────────────────────────────────────────────────────────────────────
//  symbol helpers
// ────────────────────────────────────────────────────────────────────────────────
bool LocalAPI::clearSymbol(const std::string& sym){
    std::lock_guard lg(m_mx);
    auto match=[&](const nlohmann::json& r){
        return r.contains("symbol") &&
               r["symbol"].get<std::string>()==sym; };
    auto eraseAll=[&](std::vector<nlohmann::json>& v){
        v.erase(std::remove_if(v.begin(),v.end(),match),v.end()); };
    eraseAll(m_queue); eraseAll(m_pending);
    app_state::AppState::getInstance()
        .requestThreadStop(sym,"LocalAPI::clearSymbol");
    notifyParent();
    return true;
}
void LocalAPI::clearAllRequests(){
    { std::lock_guard lg(m_mx);
      m_queue.clear(); m_pending.clear();}
    app_state::AppState::getInstance()
        .requestAllThreadsStop("LocalAPI::clearAllRequests");
    notifyParent();
}

// ────────────────────────────────────────────────────────────────────────────────
//  status helpers
// ────────────────────────────────────────────────────────────────────────────────
nlohmann::json LocalAPI::getStatus() const {
    std::lock_guard lg(m_mx);
    return {{"running",m_running},
            {"queued",m_queue.size()},
            {"pending",m_pending.size()},
            {"autoConfirm",m_autoConfirm}};
}
nlohmann::json LocalAPI::getFormattedRequests() const{
    std::lock_guard lg(m_mx);
    nlohmann::json j;
    for (auto& r : m_queue) j[r["symbol"].get<std::string>()]=r["params"];
    return j;
}
nlohmann::json LocalAPI::getPendingRequests() const{
    std::lock_guard lg(m_mx);
    nlohmann::json j;
    for (auto& r : m_pending) j[r["symbol"].get<std::string>()]=r["params"];
    return j;
}

// simplified diagnostic – calls into ModelManagerFactory only if present
nlohmann::json LocalAPI::getSymbolQueueData(const std::string& sym) const{
    nlohmann::json out; out["symbol"]=sym;
    try{
        auto& f = model_manager::ModelManagerFactory::getInstance();
        if (auto mm=f.getModelManager(sym)) {
            out["connected"]=mm->isConnected();
            out["tickCount"]=mm->getTickCount();
        } else out["error"]="no model";
    }catch(...){ out["error"]="factory unavailable"; }
    return out;
}

// ────────────────────────────────────────────────────────────────────────────────
//  misc util
// ────────────────────────────────────────────────────────────────────────────────
void LocalAPI::notifyParent(){
    if (auto p=m_parent.lock()) m_tradeCB(getFormattedRequests());
}
void LocalAPI::setInputManager(std::shared_ptr<input_manager::InputManager> p){
    m_parent=p;
}
void LocalAPI::setLogLevel(int l){ m_logLvl=l; }
void LocalAPI::setAutoConfirm(bool ac){ m_autoConfirm=ac; }
void LocalAPI::registerTradeCallback(std::function<void(const nlohmann::json&)> cb){ m_tradeCB = std::move(cb); }
void LocalAPI::registerErrorCallback(std::function<void(const std::string&, const std::string&)> cb){ m_errorCB = std::move(cb); }

void LocalAPI::log(int lvl,std::string_view msg) const{
    if (lvl<=m_logLvl)
        std::cout<<"[LocalAPI]["<<(lvl==0?"INFO":lvl==1?"DBG":"ERR")<<"] "
                 <<msg<<'\n';
}
bool LocalAPI::parseConfig(const std::string& p){
    std::ifstream f(p); if (!f) return false; f>>m_cfg; return true;
}

// ────────────────────────────────────────────────────────────────────────────────
//  help banner demanded by user
// ────────────────────────────────────────────────────────────────────────────────
void LocalAPI::printHelpBanner(int port) const {
    std::cout << "\n=== API SERVER RUNNING ===\n\n"
              << "The server is now listening on port " << port << "\n"
              << "Available endpoints:\n"
              << "  GET  /status                     – Get server status\n"
              << "  GET  /trades                     – Current trades\n"
              << "  GET  /pending                    – Pending (queued) trades\n"
              << "  GET  /queue-data?symbol=AAPL     – All ticks for symbol\n"
              << "  POST /trade                      – Queue a trade request\n"
              << "  POST /confirm                    – Confirm all queued\n"
              << "  POST /clear                      – Clear a symbol\n"
              << "  POST /clear-all                  – Clear all symbols\n"
              << "  POST /emergency-stop             – Trigger emergency stop\n"
              << "  POST /set-auto-confirm           – Toggle auto confirm\n\n"
              << "Example curl commands:\n"
              << "  curl -X GET  http://localhost:" << port << "/status\n"
              << "  curl -X GET  http://localhost:" << port << "/trades\n"
              << "  curl -X GET  http://localhost:" << port << "/pending\n"
              << "  curl -X GET  http://localhost:" << port << "/queue-data?symbol=AAPL\n"
              << "  curl -X POST -H 'Content-Type: application/json' \\\n"
              << "       -d '{\"symbol\":\"AAPL\",\"params\":{\"lots\":5,\"margin\":\".10\","
                 "\"stopLoss\":\".05\",\"maxTrades\":10,\"lossThreshold\":3,"
                 "\"winThreshold\":5,\"minWinRate\":\".50\",\"maxHoldSeconds\":3600}}' \\\n"
              << "       http://localhost:" << port << "/trade\n"
              << "  curl -X POST http://localhost:" << port << "/confirm\n"
              << "  curl -X POST -H 'Content-Type: application/json' -d '{\"symbol\":\"AAPL\"}' "
                 "http://localhost:" << port << "/clear\n"
              << "  curl -X POST http://localhost:" << port << "/clear-all\n"
              << "  curl -X POST http://localhost:" << port << "/emergency-stop\n"
              << "  curl -X POST -H 'Content-Type: application/json' "
                 "-d '{\"autoConfirm\":true}' http://localhost:" << port
              << "/set-auto-confirm\n";
}

// ────────────────────────────────────────────────────────────────────────────────
void LocalAPI::emergencyStop(){
    log(0,"Emergency stop");
    stop();
    app_state::AppState::getInstance()
        .requestEmergencyStop(1'000,"LocalAPI::emergencyStop");
}

} // namespace local_api

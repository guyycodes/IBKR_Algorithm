# IBKR Trading System

## System Overview

### Architecture Diagram

```
+----------------+                                  +------------------+
|    CLI Tool    |                                  |   HTTP Client    |
+-------+--------+                                  +--------+---------+
        |                                                    |
        | (commands)                                   (API requests)
        v                                                    v
+-------+---------+         +-------------------+    +------+---------+
|  Input Manager  | <-------+ Signal Handler    |    |    Local API   |
+-------+---------+         | (CTRL+C, SIGTERM) |    +------+---------+
        |                   +-------------------+           |
        |                             |                     |
        |                             v                     |
        |                   +---------+---------------------+------+
        |                   |                                      |
        |                   |              AppState                |
        |                   |     (Thread Management Hub)          |
        |                   |                                      |
        |                   +------+---------------------------+---+
        |                          |                           |
        v                          |                           |
+-------+---------+     registerModelThread()                  |
| Model Manager   |     requestThreadStop()                    |
|    Factory      |     requestAllThreadsStop()                |
+-------+---------+     requestEmergencyStop()                 |
        |                                                      |
        | creates                                              | manages
        v                                                      v
+-----------------------+                             +------------------+
|    Model Manager      |<--------------------------- | Model Threads    |
|    (per symbol)       |         runs in             | (per symbol)     |
|                       |                             +------------------+
| +-------------------+ |
| |  Connection Mgr   | |                         
| +-------------------+ |
| |                   | |
| |  STK_Q (Stock     | |                            +------------------+
| |  Queue)           +-------------------------------->  TWS / Gateway |
| |                   | |                            +------------------+
| +-------------------+ |
| |                   | |
| |  API Callbacks    | |
| |                   | |
| +-------------------+ |
| |                   | |
| |  Raw Data Model   | |
| |                   | |
| +-------------------+ |
+-----------------------+

```

## Testing the Input Manager

The Input Manager component can be tested in two different modes: CLI Mode (for interactive testing) and API Mode (for programmatic testing via HTTP requests).

### Building the Test Environment

```bash
# Install required dependencies (if needed)
mkdir -p include/nlohmann
wget https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp -O include/nlohmann/json.hpp

# Build the project
make clean
make
```

### CLI Mode Testing

CLI Mode provides an interactive terminal interface for manually entering trading parameters:

```bash
# Run CLI test
make test
```

In CLI Mode, you can:
- Add trading symbols with parameters (lots, margin, stopLoss, etc.)
- Clear specific symbols
- Submit all inputs for processing
- Trigger emergency exit

This mode is best for direct user interaction and local development testing.

### API Mode Testing

API Mode runs an HTTP server that accepts requests via curl or other HTTP clients:

```bash
# Run API server on the default port (9000)
make test-api

# Run API server on a specific port
./bin/ibkr-trader --api --port 8080
```

#### Available Endpoints

| Method | Endpoint          | Description                    |
|--------|-------------------|--------------------------------|
| GET    | /status           | Get server status              |
| GET    | /trades           | Get current trades             |
| POST   | /trade            | Submit a trade request         |
| POST   | /clear            | Clear a specific symbol        |
| POST   | /clear-all        | Clear all symbols              |
| POST   | /emergency-stop   | Trigger emergency stop         |

#### Example curl Commands

Get server status:
```bash
curl -X GET http://localhost:9000/status
```

Get current trades:
```bash
curl -X GET http://localhost:9000/trades
```

Submit a trade request:
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{
    "symbol": "AAPL",
    "params": {
      "lots": 5,
      "margin": ".10",
      "stopLoss": ".05",
      "maxTrades": 10,
      "lossThreshold": 30,  // daily loss threshold before giving up on this stock for the day
      "winThreshold": 50,  // daily win threshold before giving up on this stock for the day
      "minWinRate": ".50",
      "maxHoldSeconds": 3600
    }
  }' \
  http://localhost:9000/trade
```

Clear a specific symbol:
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL"}' \
  http://localhost:9000/clear
```

Clear all symbols:
```bash
curl -X POST http://localhost:9000/clear-all
```

Trigger emergency stop:
```bash
curl -X POST http://localhost:9000/emergency-stop
```

### Notes for Docker/Container Testing

When testing in a container environment, ensure:
1. The port used by the API server (default: 9000) is exposed in your container configuration
2. Use the container IP or hostname instead of localhost in curl commands from your host machine
3. For complete container isolation, you may need to use `docker-compose` to set up networking between services 


🧵  Main thread
src/
│   ├─ main.cpp ← program entry-point
│   │   ├─ variables
│   │   │   └─ useCliMode
│   │   └─ methods
│   │       ├─ signalHandler()
│   │       ├─ main()
│   │       └─ g_shutdownRequested()
│   ├─ main.hpp
│   │   ├─ variables
│   │   └─ methods
│   ├─ app_state.hpp
│   │   ├─ Roles:
│   │   │   ├─ recieve thread requests like an api from manager classes
│   │   │   ├─ manages thread lifcycle in one place
│   │   │   ├─ proper thread cleanup and creation
│   │   │   └─ keep inventory of all threads
│   │   ├─ variables
│   │   └─ methods
│   └─ app_state.cpp
│       ├─ variables
│       └─ methods
└──owns ──► 🧵 Thread #1
            │
        input_manager/
            │     └─ input_manager.cpp ⇢ model_manager_factory & postition_handler
            │         │    │   ├─ variables
            │         │    │   └─ methods
            │         │    ├─Roles:
            │         │    ├─ recieve position details from postition_handler.cpp
            │         │    ├─ process stk_q elements and passes to local_api.cpp
            │         │    └─ recieve HTTP input payloads from local_api.cpp pass JSON to model_manager_factory & postition_handler
            │         ├─local_api.cpp (HTTP server)   ⇢  (input_manager.cpp)
            │         │   │   ├─ variables
            │         │   │   └─ methods
            │         │   ├─Roles:
            │         │   ├─ recieve position details from postition_handler.cpp
            │         │   ├─ process stk_q elements and passes to local_api.cpp
            │         │   └─ recieve HTTP input payloads from local_api.cpp pass JSON to model_manager_factory & postition_handler
            │         └─ local_api.hpp
            │            ├─ variables
            │            └─ methods
        model_manager_factory/
            │     └─ model_manager_factory.cpp (factory pattern)
            │         │   ├─ variables:
            │         │   └─ methods
            │         ├─Roles:
            │         │   ├─ Factory Pattern, spawns / reuses model_manager singletons per symbol ──►  model_manager.cpp (Thread #2)
            │         │   └─ recieves user input from input_manager.cpp to setup model_manager singletons on thread #2
            │         └─ model_manager_factory.hpp
            │             ├─ variables
            │             └─ methods
            └──owns ──► 🧵 Thread #2  (per symbol)
                        ⬚
                        │
                    model_manager/
                        │     └─ model_manager.cpp (singleton pattern)
                        │         │   │   ├─ variables
                        │         │   │   └─ methods
                        │         │   ├─Roles:
                        │         │   ├─ instantiates connection.cpp, time_ordered_tick_buffer.cpp, ring_buffer_trade_handler.cpp, trade_handler.cpp, position_handler.cpp, stk_q.cpp, raw_data_model.cpp, connection_cache.cpp
                        │         │   ├─ prunes tick data from stk_q
                        │         │   ├─ delegate
                        │         │   └─ makes initial call to  ⇢  (reqmarketdata() in connection.cpp)
                        │         └─ model_manager.hpp
                        │             │   ├─ variables
                        │             │   └─ methods
                        │             ├─Roles:
                        │             ├─ 
                        │             ├─ 
                        │             └─ 
                    model_manager_factory/
                        │     └─ model_manager_factory.cpp ──► spawns / reuses  ──►  ModelManager (Thread #2)
                        │         │   ├─ variables:
                        │         │   └─ methods
                        │         ├─Roles:
                        │         │   ├─ Factory Pattern, spawns / reuses model_manager singletons ──►  model_manager.cpp (Thread #2)
                        │         │   └─ recieves user input from input_manager.cpp to setup model_manager singletons on thread #2
                        │         └─ model_manager_factory.hpp
                        │             ├─ variables
                        │             └─ methods
                    connection_manager/
                        │     └─ connection_manager.cpp
                        │         │   │   ├─ variables:
                        │         │   │   └─ methods
                        │         │   ├─Roles:
                        │         │   ├─ wrapper for the IBKR connection setup
                        │         │   ├─ implements the connection.cpp
                        │         │   └─ 
                        │         └─ connection_manager.hpp
                        │             │   ├─ variables
                        │             │   └─ methods
                        │             ├─Roles:
                        │             └─ 
                    connection/
                        │   ├─ connection.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ recieves tick data from IBKR api
                        │   │   ├─ hold all the callback methods for IBKR api
                        │   │   ├─ injects itself with model_manager.cpp
                        │   │   ├─ uses frame_analyzer.cpp to inspect incoming tick data
                        │   │   ├─ uses the decoder to decode BID64 data
                        │   │   ├─ routes complete stock_ticks to the model manager
                        │   │   └─ sends ticks and real time bars into connection_cache.cpp
                        │   └─ connection.hpp
                        │       │   ├─ variables
                        │       │   └─ methods
                        │       ├─Roles:
                        │       ├─ 
                        │       ├─ 
                        │       └─ 
                    connection_cache/
                        │   ├─ connection_cache.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ recieves tick data from connection.cpp
                        │   │   ├─ uses in place efficent aggregation methods
                        │   │   ├─ aggregates tick data from partial streams into a useable stoch_data_tick.cpp
                        │   │   └─ sends completed stock_data_tick back to connection.cpp to be routed to model_manager.cpp
                        │   ├─ connection_cache.hpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   ├─ 
                        │   │   └─ 
                      decoder/
                        │   ├─ decoder.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ implemented through frame_analyzer.cpp
                        │   │   ├─ essentiall is a wrapper for IBKR official decoder
                        │   │   └─ uses IBKR official decode to decode data
                        │   ├─ decoder.hpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   ├─ frame_analyzer.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ recieves tick data from connection.cpp
                        │   │   ├─ uses decoder.cpp to decode data
                        │   │   └─ sends completed data back to connection.cpp
                        │   ├─ frame_analyzer.hpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                    account_summary/    
                        │   ├─ account_summary.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   └─  account_summary.hpp
                        │       │   ├─ variables
                        │       │   └─ methods
                        │       ├─Roles:
                        │       ├─ 
                        │       └─ 
                       models/  
                        │   ├─ stock_data_tick.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   ├─ stock_data_tick.hpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   ├─ volume_profile_map.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   ├─  volume_profile_map.hpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   ├─ raw_data_model.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   └─  raw_data_model.hpp
                        │       │   ├─ variables
                        │       │   └─ methods
                        │       ├─Roles:
                        │       ├─ 
                        │       └─ 
                       utils/
                        │   ├─ app_state.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   ├─  app_state.hpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        time_ordered_tick_buffer/
                        │   ├─  time_ordered_tick_buffer.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   ├─  time_ordered_tick_buffer.hpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   ├─ ring_buffer_trade_handler.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   └─ ring_buffer_trade_handler.hpp
                        │       │   ├─ variables
                        │       │   └─ methods
                        │       ├─Roles:
                        │       ├─ 
                        │       └─ 
                        stk_q/
                        │   ├─ stk_q.cpp
                        │   │   │   ├─ variables
                        │   │   │   └─ methods
                        │   │   ├─Roles:
                        │   │   ├─ 
                        │   │   └─ 
                        │   └─ stk_q.hpp
                        │       │   ├─ variables
                        │       │   └─ methods
                        │       ├─Roles:
                        │       ├─ 
                        │       └─ 

🧵 MAIN-THREAD: Waits for user input (Enter to quit)
     ↓ (spawns but doesn't wait)
🧵 HTTP-SERVER: Runs independently, handles all requests
     ↓ (spawns worker threads)  
🧵 ModelManager(s): Process data independently per symbol

🧵 MAIN-THREAD 
├─ main.cpp/main()
├─ InputManager initialization
└─ LocalAPI::start()
    └── owns ──▶ 🧵 HTTP-SERVER  THREAD #1
        ├─ Local API (HTTP server)
        ├─ HTTP request processing  
        ├─ app_state.cpp (singleton created here!) ← CORRECT LOCATION
        └─ ModelManagerFactory operations
            ├─owns ──▶ 🧵 ModelManager  THREAD #2
            │    └─ ModelManager for Symbol
            └─owns ──▶ 🧵 ModelManager  THREAD #3
                 └─ ModelManager for Symbol

Legend:
“owns →” arrows denote “parent thread creates and manages the lifecycle of the child thread.”
Left‐pointing arrows ← denote data or control flowing from one component into another.
All components listed under a thread box execute on that thread ID.
🧵 MAIN-THREAD 
│
├─ main.cpp/main() ←──────────────────────── input_manager (program entry-point logic)
│
├─ InputManager (object creation) ←─────────── LocalAPI (object initialization)
│
└─ LocalAPI::start() (spawns HTTP thread)
        │
        └── owns ──▶ 🧵 THREAD #1 HTTP-SERVER/HTTP-WORKER
                │
                ├─ Local API (HTTP server) ←──────────────────────────────── External_clients
                │
                ├─ InputManager (request processing) ←──────────────────────── Local API callbacks
                │
                ├─ app_state.cpp (singleton created here) ←───HTTP thread (manages thread lifecycle)
                │
                └─ ModelManagerFactory (factory pattern) ←──────────────────── InputManager
                        │
                        ├── owns ──▶ 🧵 THREAD #2 - ModelManager #1 - QBTS
                        │         │
                        │         └─ ModelManager(QBTS) ←──────────────────────── time_ordered_tick_buffer, connection_manager, raw_data_model, volume_profile_map, ring_buffer_trade_handler
                        │                 │
                        │                 ├─ stock_data_tick ←────────────────────────  connection
                        │                 ├─ time_ordered_tick_buffer ←──────────────────────── stock_data_tick  
                        │                 ├─ time_ordered_tick_buffer (signal generation) ←────────── ring_buffer_trade_handler
                        │                 ├─ ring_buffer_trade_handler ←──────────────────────── stock_data_tick, time_ordered_tick_buffer
                        │                 ├─ connection
                        │                 │     └──connection.cpp ←──────────────────────── connection_cache, frame_analyzer, decoder, account_summary
                        │                 ├─ connection_manager  
                        │                 │     └──connection_manager.cpp ←──────────── connection
                        │                 ├─ connection_cache
                        │                 │       └──connection_cache.cpp ←──────────────────────── stock_data_tick
                        │                 ├─ decoder
                        │                 │     ├─ frame_analyzer.cpp ←──────────────────────── decoder
                        │                 │     └──decoder.cpp ←──────────────────────── frame_analyzer  
                        │                 ├─ models
                        │                 │     ├─ stock_data_tick
                        │                 │     ├─ volume_profile_map
                        │                 │     └─ raw_data_model ←──────────────────────── STK_Q, stock_data_tick
                        │                 ├─ STK_Q
                        │                 └─ position_handler (risk manager / P&L / Logs) ←──────────────────────── ring_buffer_trade_handler
                        │
                        ├── owns ──▶ 🧵 THREAD #3 - ModelManager #2 (future thread) - NVDA  
                        │         │
                        │         └─ ModelManager(NVDA) ←─ (same components as QBTS)
                        │
                        └── owns ──▶ 🧵 THREAD #4 - ModelManager #3 (future thread) - IONQ
                                  │
                                  └─ ModelManager(IONQ) ←─ (same components as QBTS)
                                                                
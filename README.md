// Readme.md

# IBKR Trading System

## System Overview

The IBKR Trading System is designed to collect, process, and analyze stock market data in real-time. It connects to Interactive Brokers' API to request and receive market data, which is then processed through a queue-based architecture with multiple threads for concurrent data handling.

### Key Components

1. **Stock Queue Manager** (`stk_queue_manager/`)
   - Manages multiple independent queue threads
   - Handles assignment of symbols to queues
   - Collects and stores tick-by-tick data
   - Purges old data based on retention policies

2. **Observer Pattern** (`observer/`)
   - Monitors queues and CSV files for changes
   - Triggers actions when events occur
   - Enables pub/sub communication between components

3. **IBKR Trader** (`config/`)
   - Connects to Interactive Brokers API
   - Requests various types of market data
   - Processes callbacks from the API

## Architecture Overview

This system consists of three core components that work together to create an automated trading pipeline:

1. **Market Monitor**
   - Ingests live ticker data via stock queues
   - Maintains data structures for analysis
   - Feeds information to charts and models

2. **Position Monitor**
   - Observes chart and model data
   - Identifies favorable position characteristics
   - Analyzes various trading setups

3. **Trader**
   - Identifies favorable position entry points
   - Generates entry and exit parameters
   - Executes trades with precise entries and exits

### Data Flow

`Market Data → Market Monitor → Position Monitor → Trader → Trade Execution`

### Architecture Diagram

```
          +----------------+        +-----------------+
          |     CLI Tool   |        |   .CSV File(s)  |
          +--------+-------+        +--------+--------+
                   |                         |
                   | (commands & symbols)    |
                   v                         |
          +----------------+  (user input)   |
          | Input Manager  | <---------------+
          +--------+-------+
                   |
                   | (Pub/Sub: config/symbol updates)
                   v
      +-----------------------+
      |   Raw_Data_q Manager    |
      +----------+------------+
                 |
                 | (market data events)
                 v
        +-------------------+
        | Metrics Calc.     |  <---- External Market Data API (feeds raw data)
        +--------+----------+
                 | (metrics)
                 v
        +-------------------+ (aggregates metrics)
        | Metrics.q        |
        |   Manager         |
        +--------+----------+
                 |
                 | (aggregated metrics, states)
                 v
        +-------------------+
        | Position Monitor  | (filters good positions)
        +--------+----------+
                 |
                 | (Map<Symbol,Set<Position>>)
                 v
        +-------------------+
        | Position Manager  |
        +--------+----------+
                 |
                 | (final positions, results)
                 v
           +--------------+
           |  Results/UI  |
           +--------------+

```

## Core Functionality

### Stock Queue Manager

The `STKQueueManager` class (in `src/stk_queue_manager/`) manages 10 independent stock queues, each running on its own thread:

- **Initialization**: `initializeAllStockQueues()` creates 10 empty queues, each with its own observer running on a separate thread
- **Symbol Assignment**: `assignSymbolToQueue(index, symbol, period)` assigns a symbol to a queue and begins collecting tick data
- **Data Retention**: `purgeOldDataFromQueue(index)` removes data older than the specified retention period
- **Queue Release**: `releaseQueue(index)` stops tracking a symbol and frees the queue for other uses
- **File Storage**: Tick data is written to symbol-specific text files (e.g., "AAPL.txt")

### CSV File Monitoring

The system can monitor a CSV file for changes:

- When a new symbol is detected, user is prompted to:
  1. Confirm whether to track the symbol
  2. Set a data retention period (1 minute, 5 minutes, or no limit)
- The symbol is then assigned to an available queue for data collection

### CLI Interface

The main application provides a CLI menu (`src/main.cpp`) with options to:

- Connect to IBKR and request various types of market data
- Add/remove symbols to the watch list
- View queue status
- Manually assign/release symbols to queues
- Purge old data from queues

## Key Methods and Locations

### STK Queue Manager (`src/stk_queue_manager/stk_queue_manager.hpp` & `.cpp`)

| Method | Purpose |
|--------|---------|
| `initializeAllStockQueues()` | Creates 10 empty queues on separate threads |
| `findAvailableQueueIndex()` | Finds an unused queue for symbol assignment |
| `assignSymbolToQueue(index, symbol, period)` | Starts tracking a symbol and collecting data |
| `releaseQueue(index)` | Stops tracking a symbol and frees the queue |
| `purgeOldDataFromQueue(index)` | Removes data older than retention period |
| `handleTickData(symbol, data)` | Processes incoming tick data for a symbol |
| `displayQueueStatus()` | Shows the current state of all queues |

### IBKR Trader (`src/config/config.hpp` & `.cpp`)

| Method | Purpose |
|--------|---------|
| `connect()` | Establishes connection to IBKR API |
| `requestTickByTickData()` | Requests real-time tick data for a symbol |
| `tickByTickAllLast()` | Callback that receives tick data from IBKR |

### Observer Implementation (`src/observer/observer.hpp` & `.cpp`)

| Method | Purpose |
|--------|---------|
| `createCSVObserver()` | Creates an observer for the CSV file |
| `startWatching()` | Begins monitoring in a separate thread |
| `addObserver()` | Registers a consumer to receive notifications |

## Threading Architecture

Each stock queue has its own dedicated thread:
- Thread is created when `observer->startWatching()` is called
- Queue observer continuously monitors for changes
- Independent, concurrent processing of multiple symbols

## Detailed Data Flow

1. IBKR API sends tick data via callbacks
2. Data is processed in `tickByTickAllLast()`
3. Formatted data is passed to `STKQueueManager::handleTickData()`
4. Data is written to files and stored in queues
5. Old data is automatically purged based on retention periods

## Usage

Run the program and use the CLI menu to:
1. Connect to IBKR (option 0)
2. Add symbols to watch via CSV or manual assignment (options 14, 19)
3. Interact with queue management (options 17, 18, 20)
4. Request various types of market data (options 1-13)


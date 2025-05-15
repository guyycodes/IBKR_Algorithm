# IBKR Trading System

## System Overview

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
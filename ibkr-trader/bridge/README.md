# Cross-Architecture Bridge
🚀 **M3 MacBook ↔ x86 Container Data Pipeline**

## Overview

This bridge solves the architecture compatibility issue between:
- **M3 MacBook (ARM64)** - For ML training and analysis 
- **x86 Container** - For IBKR TWS and C++ ring buffers

The bridge provides seamless access to both real-time and historical trading data across architectures.

## Architecture

```
┌─────────────────┐    TCP/IP     ┌──────────────────────┐
│   M3 MacBook    │◄─────────────►│   x86 Container      │
│   (ARM64)       │   JSON/HTTP   │   (Intel x86_64)     │
│                 │               │                      │
│  Python Clients │               │  C++ Ring Buffers    │
│  • Ring Buffer  │               │  • Minute Ring       │
│  • Training Data│               │  • Candle Ring       │
│  • ML Training  │               │  • Price Ring        │
│                 │               │                      │
│  Pandas/NumPy   │               │  STK_Q Database      │
│  Scikit-learn   │               │  IBKR TWS API        │
└─────────────────┘               └──────────────────────┘
```

## Components

### 1. C++ Network Server (`network_bridge.cpp`)
- **Purpose**: Expose ring buffer data via TCP/IP
- **Port 8080**: Real-time ring buffer access
- **Port 8081**: STK_Q training data access
- **Protocol**: JSON over TCP

### 2. Python Clients

#### `RingBufferClient` - Real-Time Analysis
```python
from ring_buffer_client import RingBufferClient

client = RingBufferClient("localhost", 8080)
client.connect()

# Get live market data
live_candles = client.get_live_minute_candles()
indicators = client.get_technical_indicators()
latest_price = client.get_latest_price()

# Real-time monitoring
client.monitor_real_time(duration_seconds=30)
```

#### `TrainingDataClient` - ML Training
```python
from training_data_client import TrainingDataClient

client = TrainingDataClient("localhost", 8081)
client.connect()

# Get training dataset info
info = client.get_dataset_info()
print(f"Total ticks: {info['total_ticks']:,}")

# Prepare ML dataset
X, y = client.prepare_ml_dataset(
    lookback_periods=50,
    prediction_horizon=1,
    sample_rate=5
)
```

## Data Access Patterns

### Real-Time Data (Ring Buffers)
- **Minute Ring**: Live OHLCV aggregation in progress
- **Candle Ring**: Completed 1-minute candles (sliding window)
- **Price Ring**: Recent price series for ALMA calculation
- **Indicators**: Live VWAP, RSI, EMA, ALMA, ATR values

### Training Data (STK_Q)
- **Historical Ticks**: Millions of tick-level data points
- **Time Range Queries**: Query specific date/time ranges
- **Sampling**: Intelligent sampling to manage memory usage
- **ML Features**: Auto-generated technical indicators and derived features

## Setup & Usage

### 1. Install Dependencies
```bash
cd ibkr-trader/bridge
pip install -r requirements.txt
```

### 2. Build C++ Network Server
```bash
cd ibkr-trader
make  # Builds the full trading system including bridge
```

### 3. Start the Bridge Server
```bash
# In x86 container
cd ibkr-trader
./ibkr_trader  # This starts the bridge server automatically
```

### 4. Use Python Clients
```bash
# On M3 MacBook
cd ibkr-trader/bridge
python cross_arch_demo.py  # Comprehensive demo
python ring_buffer_client.py  # Real-time data demo
python training_data_client.py  # Training data demo
```

## API Reference

### Ring Buffer Client Methods

| Method | Purpose | Returns |
|--------|---------|---------|
| `get_live_minute_candles()` | OHLCV being built from ticks | DataFrame |
| `get_completed_candles()` | Historical 1-min candles | DataFrame |
| `get_price_series()` | Recent prices for ALMA | NumPy array |
| `get_technical_indicators()` | Current indicator values | Dict |
| `get_market_snapshot()` | Complete market state | Dict |
| `monitor_real_time()` | Live monitoring session | None |

### Training Data Client Methods

| Method | Purpose | Returns |
|--------|---------|---------|
| `get_dataset_info()` | Dataset statistics | Dict |
| `get_training_sample()` | Sampled training data | DataFrame |
| `get_time_range_data()` | Data in time range | DataFrame |
| `prepare_ml_dataset()` | ML-ready X, y arrays | Tuple[np.array] |
| `cleanup_old_data()` | Remove old data | None |

## Performance Considerations

### Memory Management
- **Sampling**: Use `sample_every_n` to reduce memory usage
- **Time Windows**: Query specific time ranges instead of all data
- **Batch Processing**: Process data in chunks for large datasets

### Network Optimization
- **Connection Pooling**: Reuse connections for multiple requests
- **Compression**: JSON is text-based but efficient for structured data
- **Buffering**: Server buffers responses for large datasets

### Typical Performance
- **Real-time Data**: ~10-50 snapshots/second
- **Training Data**: ~1000-10000 ticks/second (depending on sampling)
- **Memory Usage**: ~200 bytes per tick
- **Network Latency**: <1ms localhost, ~10ms over LAN

## Use Cases

### 1. Real-Time Trading
```python
# Monitor live market for trading signals
client = RingBufferClient()
client.connect()

while trading_active:
    snapshot = client.get_market_snapshot()
    indicators = snapshot['indicators']
    
    if should_trade(indicators):
        execute_trade()
```

### 2. ML Model Training
```python
# Train price prediction model
client = TrainingDataClient()
client.connect()

X, y = client.prepare_ml_dataset(
    lookback_periods=100,
    prediction_horizon=5
)

model = train_model(X, y)
```

### 3. Backtesting
```python
# Test strategy on historical data
historical_data = training_client.get_time_range_data(
    start_time=datetime(2024, 1, 1),
    end_time=datetime(2024, 1, 31)
)

results = backtest_strategy(historical_data)
```

## Error Handling

### Connection Issues
- **Auto-retry**: Clients automatically attempt reconnection
- **Graceful Degradation**: Continue with cached data if connection fails
- **Timeout Handling**: Configurable timeouts for network requests

### Data Quality
- **Missing Data**: Forward-fill and interpolation strategies
- **Outlier Detection**: Automatic outlier filtering
- **Validation**: Data consistency checks before processing

## Security

### Network Security
- **Localhost Only**: Default configuration binds to localhost
- **No Authentication**: Assumes trusted internal network
- **Firewall**: Use firewall rules to restrict access if needed

### Data Privacy
- **No Persistence**: Bridge doesn't store data permanently
- **Memory Only**: All data handled in memory for performance
- **Clean Shutdown**: Proper cleanup on exit

## Troubleshooting

### Common Issues

1. **Connection Refused**
   - Ensure x86 container is running
   - Check if bridge server is started (`./ibkr_trader`)
   - Verify port availability (8080, 8081)

2. **Empty Data**
   - Check if IBKR TWS is connected
   - Verify market hours and data subscription
   - Look for errors in container logs

3. **Memory Issues**
   - Reduce sampling rate (`sample_every_n`)
   - Use smaller time windows
   - Monitor system memory usage

### Debugging
```bash
# Check if bridge server is running
netstat -ln | grep 8080
netstat -ln | grep 8081

# Test connection manually
telnet localhost 8080
```

## Future Enhancements

- **WebSocket Support**: Real-time streaming instead of polling
- **Compression**: Gzip compression for large datasets
- **Authentication**: Token-based authentication for security
- **Load Balancing**: Multiple server instances for high availability
- **Caching**: Redis caching layer for frequently accessed data

---

**Built for the IBKR Trader Project** 🚀  
*Bridging architectures for seamless trading data access* 
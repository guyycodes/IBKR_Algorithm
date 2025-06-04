# #!/usr/bin/env python3
# """
# Ring Buffer Client for Real-Time Analysis
# Connects to x86 Container for live trading data
# """

# import socket
# import json
# import time
# import numpy as np
# from typing import Dict, List, Any, Optional, Tuple
# import pandas as pd

# class RingBufferClient:
#     """
#     Real-Time Ring Buffer Data Client
    
#     Purpose: Access live trading data from 3 ring buffers for real-time analysis
#     - Minute Ring: Live tick aggregation (OHLCV building)
#     - Candle Ring: Completed 1-minute candles  
#     - Price Ring: ALMA calculation buffer
#     """
    
#     def __init__(self, host: str = "localhost", port: int = 8080):
#         self.host = host
#         self.port = port
#         self.socket: Optional[socket.socket] = None
        
#     def connect(self) -> bool:
#         """Connect to the ring buffer server"""
#         try:
#             self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
#             self.socket.connect((self.host, self.port))
#             print(f"🔗 [RingBuffer] Connected to {self.host}:{self.port}")
#             return True
#         except Exception as e:
#             print(f"❌ [RingBuffer] Connection failed: {e}")
#             return False
    
#     def disconnect(self):
#         """Disconnect from server"""
#         if self.socket:
#             self.socket.close()
#             self.socket = None
#             print("🔌 [RingBuffer] Disconnected")
    
#     def _send_request(self, command: str) -> Dict[str, Any]:
#         """Send request and receive response"""
#         if not self.socket:
#             raise RuntimeError("Not connected to server")
        
#         request = {"command": command}
#         request_str = json.dumps(request)
#         self.socket.send(request_str.encode('utf-8'))
        
#         response_data = self.socket.recv(65536).decode('utf-8').strip()
#         return json.loads(response_data)
    
#     # ═══════════════════════════════════════════════════════════════════════════════
#     # REAL-TIME DATA ACCESS METHODS
#     # ═══════════════════════════════════════════════════════════════════════════════
    
#     def get_live_minute_candles(self) -> pd.DataFrame:
#         """
#         Get live minute candles being built from tick aggregation
#         Returns: DataFrame with OHLCV data for active minute slots
#         """
#         response = self._send_request("minute_ring")
#         if not response["success"]:
#             print(f"❌ Error getting minute ring: {response['message']}")
#             return pd.DataFrame()
        
#         data = []
#         for slot in response["data"]:
#             candle = slot["candle"]
#             data.append({
#                 'slot': slot['slot'],
#                 'minute': slot['minute'],
#                 'open': candle['open'],
#                 'high': candle['high'],
#                 'low': candle['low'],
#                 'close': candle['close'],
#                 'volume': candle['volume'],
#                 'timestamp': slot['minute'] * 60 * 1000  # Convert to milliseconds
#             })
        
#         df = pd.DataFrame(data)
#         if not df.empty:
#             df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
        
#         return df
    
#     def get_completed_candles(self) -> pd.DataFrame:
#         """
#         Get completed 1-minute candles (historical data within ring buffer window)
#         Returns: DataFrame with completed OHLCV candles in chronological order
#         """
#         response = self._send_request("candle_ring")
#         if not response["success"]:
#             print(f"❌ Error getting candle ring: {response['message']}")
#             return pd.DataFrame()
        
#         data = []
#         for candle_entry in response["data"]:
#             candle = candle_entry["candle"]
#             data.append({
#                 'index': candle_entry['index'],
#                 'open': candle['open'],
#                 'high': candle['high'],
#                 'low': candle['low'],
#                 'close': candle['close'],
#                 'volume': candle['volume'],
#                 'timestamp': candle['timestamp']
#             })
        
#         df = pd.DataFrame(data)
#         if not df.empty:
#             df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
#             df = df.sort_values('timestamp')  # Ensure chronological order
        
#         return df
    
#     def get_price_series(self) -> np.ndarray:
#         """
#         Get recent price series used for ALMA calculation
#         Returns: NumPy array of recent closing prices
#         """
#         response = self._send_request("price_ring")
#         if not response["success"]:
#             print(f"❌ Error getting price ring: {response['message']}")
#             return np.array([])
        
#         return np.array(response["data"])
    
#     def get_technical_indicators(self) -> Dict[str, float]:
#         """
#         Get latest technical indicators (VWAP, RSI, EMA, ALMA, ATR)
#         Returns: Dictionary with current indicator values
#         """
#         response = self._send_request("indicators")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting indicators: {response['message']}")
#             return {}
    
#     def get_buffer_stats(self) -> Dict[str, Any]:
#         """
#         Get ring buffer statistics and metadata
#         Returns: Dictionary with buffer sizes, head pointers, etc.
#         """
#         response = self._send_request("stats")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting stats: {response['message']}")
#             return {}
    
#     # ═══════════════════════════════════════════════════════════════════════════════
#     # REAL-TIME ANALYSIS HELPERS
#     # ═══════════════════════════════════════════════════════════════════════════════
    
#     def get_market_snapshot(self) -> Dict[str, Any]:
#         """
#         Get complete market snapshot for real-time analysis
#         Returns: All ring buffer data + indicators in one call
#         """
#         response = self._send_request("all")
#         if response["success"]:
#             data = response["data"]
            
#             # Convert to structured format
#             snapshot = {
#                 'timestamp': time.time(),
#                 'minute_candles': self._format_minute_data(data.get('minute_ring', [])),
#                 'completed_candles': self._format_candle_data(data.get('candle_ring', [])),
#                 'price_series': np.array(data.get('price_ring', [])),
#                 'indicators': data.get('indicators', {}),
#                 'stats': data.get('stats', {})
#             }
            
#             return snapshot
#         else:
#             print(f"❌ Error getting market snapshot: {response['message']}")
#             return {}
    
#     def _format_minute_data(self, minute_data: List[Dict]) -> pd.DataFrame:
#         """Helper to format minute ring data"""
#         data = []
#         for slot in minute_data:
#             candle = slot["candle"]
#             data.append({
#                 'slot': slot['slot'],
#                 'minute': slot['minute'],
#                 'open': candle['open'],
#                 'high': candle['high'],
#                 'low': candle['low'],
#                 'close': candle['close'],
#                 'volume': candle['volume']
#             })
#         return pd.DataFrame(data)
    
#     def _format_candle_data(self, candle_data: List[Dict]) -> pd.DataFrame:
#         """Helper to format candle ring data"""
#         data = []
#         for candle_entry in candle_data:
#             candle = candle_entry["candle"]
#             data.append({
#                 'open': candle['open'],
#                 'high': candle['high'],
#                 'low': candle['low'],
#                 'close': candle['close'],
#                 'volume': candle['volume'],
#                 'timestamp': candle['timestamp']
#             })
        
#         df = pd.DataFrame(data)
#         if not df.empty:
#             df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
#             df = df.sort_values('timestamp')
#         return df
    
#     def is_market_active(self) -> bool:
#         """
#         Check if market data is actively flowing
#         Returns: True if recent data is available
#         """
#         stats = self.get_buffer_stats()
#         return (
#             stats.get('candle_ring_count', 0) > 0 or 
#             stats.get('price_ring_count', 0) > 0
#         )
    
#     def get_latest_price(self) -> Optional[float]:
#         """
#         Get the most recent price from the system
#         Returns: Latest price or None if unavailable
#         """
#         prices = self.get_price_series()
#         return float(prices[-1]) if len(prices) > 0 else None
    
#     def monitor_real_time(self, duration_seconds: int = 30, interval_seconds: int = 2):
#         """
#         Monitor ring buffer data in real-time
#         Args:
#             duration_seconds: How long to monitor
#             interval_seconds: Update frequency
#         """
#         print(f"🚀 [RingBuffer] Starting {duration_seconds}s real-time monitoring")
#         print("📊 Tracking: Live candles, completed candles, price series, indicators")
        
#         start_time = time.time()
#         iteration = 0
        
#         while time.time() - start_time < duration_seconds:
#             iteration += 1
            
#             try:
#                 snapshot = self.get_market_snapshot()
                
#                 print(f"\n📸 [SNAPSHOT #{iteration}] Real-Time Market Data:")
#                 print(f"   • Live Candles: {len(snapshot.get('minute_candles', []))} building")
#                 print(f"   • Completed Candles: {len(snapshot.get('completed_candles', []))} available")
#                 print(f"   • Price Series: {len(snapshot.get('price_series', []))} prices")
                
#                 indicators = snapshot.get('indicators', {})
#                 if indicators:
#                     print(f"   • Latest Price: ${self.get_latest_price():.2f}")
#                     print(f"   • VWAP: ${indicators.get('vwap', 0):.2f}")
#                     print(f"   • RSI: {indicators.get('rsi', 0):.1f}")
#                     print(f"   • ATR: {indicators.get('atr', 0):.2f}")
                
#             except Exception as e:
#                 print(f"❌ Monitoring error: {e}")
#                 break
            
#             time.sleep(interval_seconds)
        
#         print(f"\n✅ [RingBuffer] Monitoring completed ({iteration} snapshots)")

# def main():
#     """Demo the RingBufferClient"""
#     print("🔄 Ring Buffer Client for Real-Time Analysis")
    
#     client = RingBufferClient("localhost", 8080)
    
#     if not client.connect():
#         print("❌ Failed to connect. Start the server first.")
#         return
    
#     try:
#         # Test the real-time interface
#         print("\n📊 Testing real-time data access...")
        
#         stats = client.get_buffer_stats()
#         print(f"Buffer Stats: {stats}")
        
#         indicators = client.get_technical_indicators()
#         print(f"Indicators: {indicators}")
        
#         # Start real-time monitoring
#         client.monitor_real_time(duration_seconds=15, interval_seconds=3)
        
#     except KeyboardInterrupt:
#         print("\n⏹️  Interrupted by user")
#     finally:
#         client.disconnect()

# if __name__ == "__main__":
#     main()
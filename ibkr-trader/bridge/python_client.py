# #!/usr/bin/env python3
# """
# Ring Buffer Network Client for ARM MacBook
# Connects to x86 Docker Container TCP Server
# Usage: python python_client.py
# """

# import socket
# import json
# import time
# import sys
# from typing import Dict, List, Any, Optional

# class RingBufferClient:
#     """Client to access ring buffer data from x86 container"""
    
#     def __init__(self, host: str = "localhost", port: int = 8080):
#         self.host = host
#         self.port = port
#         self.socket: Optional[socket.socket] = None
        
#     def connect(self) -> bool:
#         """Connect to the ring buffer server"""
#         try:
#             self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
#             self.socket.connect((self.host, self.port))
#             print(f"🔗 Connected to ring buffer server at {self.host}:{self.port}")
#             return True
#         except Exception as e:
#             print(f"❌ Connection failed: {e}")
#             return False
    
#     def disconnect(self):
#         """Disconnect from server"""
#         if self.socket:
#             self.socket.close()
#             self.socket = None
#             print("🔌 Disconnected from server")
    
#     def _send_request(self, command: str) -> Dict[str, Any]:
#         """Send request and receive response"""
#         if not self.socket:
#             raise RuntimeError("Not connected to server")
        
#         # Send request
#         request = {"command": command}
#         request_str = json.dumps(request)
#         self.socket.send(request_str.encode('utf-8'))
        
#         # Receive response
#         response_data = self.socket.recv(65536).decode('utf-8').strip()
#         return json.loads(response_data)
    
#     def get_minute_ring(self) -> List[Dict[str, Any]]:
#         """Get minute ring data (live tick aggregation)"""
#         response = self._send_request("minute_ring")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting minute ring: {response['message']}")
#             return []
    
#     def get_candle_ring(self) -> List[Dict[str, Any]]:
#         """Get candle ring data (completed 1-minute candles)"""
#         response = self._send_request("candle_ring")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting candle ring: {response['message']}")
#             return []
    
#     def get_price_ring(self) -> List[float]:
#         """Get price ring data (ALMA calculation buffer)"""
#         response = self._send_request("price_ring")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting price ring: {response['message']}")
#             return []
    
#     def get_indicators(self) -> Dict[str, float]:
#         """Get latest technical indicators"""
#         response = self._send_request("indicators")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting indicators: {response['message']}")
#             return {}
    
#     def get_stats(self) -> Dict[str, Any]:
#         """Get ring buffer statistics"""
#         response = self._send_request("stats")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting stats: {response['message']}")
#             return {}
    
#     def get_all_data(self) -> Dict[str, Any]:
#         """Get all ring buffer data in one request"""
#         response = self._send_request("all")
#         if response["success"]:
#             return response["data"]
#         else:
#             print(f"❌ Error getting all data: {response['message']}")
#             return {}

# def print_ring_buffer_data(client: RingBufferClient):
#     """Pretty print ring buffer data"""
    
#     print("\n" + "="*80)
#     print("📊 RING BUFFER DATA FROM x86 CONTAINER")
#     print("="*80)
    
#     # Get statistics first
#     stats = client.get_stats()
#     if stats:
#         print(f"\n📈 BUFFER STATISTICS:")
#         print(f"   • Minute Ring Size: {stats.get('minute_ring_size', 0)}")
#         print(f"   • Window Minutes: {stats.get('window_minutes', 0)}")
#         print(f"   • Candle Ring: {stats.get('candle_ring_count', 0)}/{stats.get('candle_ring_size', 0)} (head: {stats.get('candle_ring_head', 0)})")
#         print(f"   • Price Ring: {stats.get('price_ring_count', 0)}/{stats.get('price_ring_size', 0)} (head: {stats.get('price_ring_head', 0)})")
    
#     # Get minute ring data
#     minute_data = client.get_minute_ring()
#     print(f"\n1️⃣  MINUTE RING ({len(minute_data)} active slots):")
#     for slot in minute_data[:5]:  # Show first 5
#         candle = slot["candle"]
#         print(f"   📦 Slot[{slot['slot']}] Minute:{slot['minute']} | "
#               f"OHLCV: {candle['open']:.2f}/{candle['high']:.2f}/"
#               f"{candle['low']:.2f}/{candle['close']:.2f} Vol:{candle['volume']:.0f}")
    
#     # Get candle ring data
#     candle_data = client.get_candle_ring()
#     print(f"\n2️⃣  CANDLE RING ({len(candle_data)} completed candles):")
#     for candle_entry in candle_data[-3:]:  # Show last 3
#         candle = candle_entry["candle"]
#         print(f"   🕯️  Candle[{candle_entry['index']}] @ {candle['timestamp']//1000} | "
#               f"OHLCV: {candle['open']:.2f}/{candle['high']:.2f}/"
#               f"{candle['low']:.2f}/{candle['close']:.2f} Vol:{candle['volume']:.0f}")
    
#     # Get price ring data
#     price_data = client.get_price_ring()
#     print(f"\n3️⃣  PRICE RING ({len(price_data)} prices):")
#     if price_data:
#         recent_prices = price_data[-5:]  # Last 5 prices
#         price_str = " → ".join([f"{p:.2f}" for p in recent_prices])
#         print(f"   💰 Recent Prices: {price_str}")
    
#     # Get technical indicators
#     indicators = client.get_indicators()
#     print(f"\n📈 TECHNICAL INDICATORS:")
#     if indicators:
#         print(f"   • VWAP: ${indicators.get('vwap', 0):.2f}")
#         print(f"   • RSI: {indicators.get('rsi', 0):.1f}")
#         print(f"   • EMA9: ${indicators.get('ema9', 0):.2f}")
#         print(f"   • EMA26: ${indicators.get('ema26', 0):.2f}")
#         print(f"   • ALMA: ${indicators.get('alma', 0):.2f}")
#         print(f"   • ATR: {indicators.get('atr', 0):.2f}")
#         print(f"   • Valid: {indicators.get('valid', False)}")

# def real_time_monitor(client: RingBufferClient, duration: int = 30):
#     """Monitor ring buffer data in real-time"""
    
#     print(f"\n🚀 Starting {duration}-second real-time monitoring...")
#     print("📡 ARM MacBook reading from x86 Container")
#     print("-" * 60)
    
#     start_time = time.time()
#     iteration = 0
    
#     while time.time() - start_time < duration:
#         iteration += 1
#         print(f"\n📸 [SNAPSHOT #{iteration}] Cross-Architecture Data Transfer:")
        
#         try:
#             print_ring_buffer_data(client)
#             print(f"\n✅ Successfully retrieved data from x86 container")
            
#         except Exception as e:
#             print(f"❌ Error during monitoring: {e}")
#             break
        
#         # Wait before next snapshot
#         time.sleep(2)  # 2-second intervals
    
#     print(f"\n🏁 Monitoring completed! Total snapshots: {iteration}")

# def main():
#     """Main function"""
    
#     print("🌉 Ring Buffer Network Client (ARM MacBook)")
#     print("Connecting to x86 Docker Container...")
    
#     # Connect to server
#     client = RingBufferClient("localhost", 8080)
    
#     if not client.connect():
#         print("❌ Failed to connect. Is the server running?")
#         print("💡 Start server in container with: ./ring_buffer_server")
#         sys.exit(1)
    
#     try:
#         # Test basic connectivity
#         stats = client.get_stats()
#         if stats:
#             print("✅ Successfully connected to ring buffer server!")
            
#             # Show initial data
#             print_ring_buffer_data(client)
            
#             # Start real-time monitoring
#             real_time_monitor(client, duration=15)
#         else:
#             print("❌ Server connected but no data available")
            
#     except KeyboardInterrupt:
#         print("\n⏹️  Interrupted by user")
#     except Exception as e:
#         print(f"❌ Error: {e}")
#     finally:
#         client.disconnect()

# if __name__ == "__main__":
#     main() 
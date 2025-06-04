# #!/usr/bin/env python3
# """
# Cross-Architecture Bridge Demo
# Demonstrates complete M3 MacBook -> x86 Container data bridge
# """

# import time
# import asyncio
# from datetime import datetime, timedelta
# from ring_buffer_client import RingBufferClient
# from training_data_client import TrainingDataClient

# def main():
#     print("🚀 Cross-Architecture Bridge Demo")
#     print("📊 M3 MacBook → x86 Container Data Pipeline")
#     print("=" * 70)
    
#     # Initialize both clients
#     ring_client = RingBufferClient("localhost", 8080)  # Real-time data
#     training_client = TrainingDataClient("localhost", 8081)  # Training data
    
#     # Test connections
#     print("\n🔗 Testing Connections...")
#     ring_connected = ring_client.connect()
#     training_connected = training_client.connect()
    
#     if not ring_connected:
#         print("❌ Real-time ring buffer connection failed")
#     if not training_connected:
#         print("❌ Training data connection failed")
    
#     if not (ring_connected or training_connected):
#         print("💀 Both connections failed. Start the servers first.")
#         return
    
#     try:
#         # ═══════════════════════════════════════════════════════════════════════
#         # REAL-TIME DATA DEMONSTRATION
#         # ═══════════════════════════════════════════════════════════════════════
        
#         if ring_connected:
#             print("\n" + "=" * 50)
#             print("📈 REAL-TIME DATA ACCESS")
#             print("=" * 50)
            
#             # Get current market status
#             if ring_client.is_market_active():
#                 print("✅ Market is ACTIVE - Live data flowing")
                
#                 # Get latest price
#                 latest_price = ring_client.get_latest_price()
#                 if latest_price:
#                     print(f"💰 Latest Price: ${latest_price:.2f}")
                
#                 # Get technical indicators
#                 indicators = ring_client.get_technical_indicators()
#                 if indicators:
#                     print("📊 Technical Indicators:")
#                     for name, value in indicators.items():
#                         if isinstance(value, float):
#                             print(f"   {name.upper()}: {value:.2f}")
                
#                 # Get live minute candles
#                 live_candles = ring_client.get_live_minute_candles()
#                 print(f"🕐 Live Minute Candles: {len(live_candles)} building")
                
#                 # Get completed candles
#                 completed_candles = ring_client.get_completed_candles()
#                 print(f"✅ Completed Candles: {len(completed_candles)} in buffer")
                
#                 # Brief real-time monitoring
#                 print(f"\n🔄 Starting 10-second real-time monitor...")
#                 ring_client.monitor_real_time(duration_seconds=10, interval_seconds=2)
                
#             else:
#                 print("😴 Market appears INACTIVE - No live data")
        
#         # ═══════════════════════════════════════════════════════════════════════
#         # TRAINING DATA DEMONSTRATION
#         # ═══════════════════════════════════════════════════════════════════════
        
#         if training_connected:
#             print("\n" + "=" * 50)
#             print("🧠 TRAINING DATA ACCESS")
#             print("=" * 50)
            
#             # Get dataset information
#             dataset_info = training_client.get_dataset_info()
#             print("📊 Dataset Information:")
#             for key, value in dataset_info.items():
#                 print(f"   {key}: {value}")
            
#             if dataset_info.get('total_ticks', 0) > 0:
#                 print(f"\n💾 Dataset has {dataset_info['total_ticks']:,} ticks")
#                 print(f"📅 Data freshness: {dataset_info.get('data_freshness', 'Unknown')}")
#                 print(f"💡 Recommended sampling: 1/{dataset_info.get('recommended_sampling', 'N/A')}")
                
#                 # Get sample training data
#                 print(f"\n🎯 Sampling training data...")
#                 sample_data = training_client.get_training_sample(sample_every_n=20)
#                 if not sample_data.empty:
#                     print(f"   Sample shape: {sample_data.shape}")
#                     print(f"   Columns: {list(sample_data.columns)}")
                    
#                     # Show data preview
#                     print(f"\n📋 Data Preview (first 3 rows):")
#                     print(sample_data.head(3).to_string())
                
#                 # Prepare ML dataset
#                 print(f"\n🤖 Preparing ML dataset...")
#                 X, y = training_client.prepare_ml_dataset(
#                     lookback_periods=10,  # Smaller for demo
#                     prediction_horizon=1,
#                     sample_rate=10
#                 )
                
#                 if X.size > 0:
#                     print(f"✅ ML dataset ready!")
#                     print(f"   Features (X): {X.shape}")
#                     print(f"   Labels (y): {y.shape}")
#                     print(f"   Memory usage: ~{X.nbytes / (1024*1024):.1f} MB")
#                 else:
#                     print("❌ Could not prepare ML dataset")
#             else:
#                 print("📭 No historical training data available yet")
        
#         # ═══════════════════════════════════════════════════════════════════════
#         # COMBINED USAGE DEMONSTRATION
#         # ═══════════════════════════════════════════════════════════════════════
        
#         if ring_connected and training_connected:
#             print("\n" + "=" * 50)
#             print("🔄 COMBINED REAL-TIME + TRAINING")
#             print("=" * 50)
            
#             # Compare real-time vs historical data
#             current_price = ring_client.get_latest_price()
#             recent_data = training_client.get_recent_training_data(num_ticks=100)
            
#             if current_price and not recent_data.empty:
#                 hist_avg = recent_data['last'].mean() if 'last' in recent_data.columns else 0
#                 print(f"💰 Current Price: ${current_price:.2f}")
#                 print(f"📊 Recent 100-tick Average: ${hist_avg:.2f}")
                
#                 if current_price > hist_avg:
#                     trend = "📈 ABOVE"
#                 elif current_price < hist_avg:
#                     trend = "📉 BELOW"
#                 else:
#                     trend = "➡️  AT"
                
#                 print(f"🎯 Trend: Current price is {trend} recent average")
            
#             # Show data pipeline summary
#             print(f"\n📋 Data Pipeline Summary:")
#             print(f"   Real-time: ✅ Connected to ring buffers")
#             print(f"   Training: ✅ Connected to STK_Q database")
#             print(f"   Architecture: M3 MacBook → x86 Container")
#             print(f"   Protocol: TCP/IP with JSON messages")
    
#     except KeyboardInterrupt:
#         print(f"\n⏹️  Demo interrupted by user")
    
#     except Exception as e:
#         print(f"\n💥 Demo error: {e}")
    
#     finally:
#         # Clean up connections
#         print(f"\n🧹 Cleaning up connections...")
#         if ring_connected:
#             ring_client.disconnect()
#         if training_connected:
#             training_client.disconnect()
        
#         print(f"✅ Cross-Architecture Bridge Demo Complete!")

# def benchmark_performance():
#     """Benchmark the cross-architecture data bridge performance"""
#     print("\n⚡ PERFORMANCE BENCHMARK")
#     print("=" * 40)
    
#     ring_client = RingBufferClient("localhost", 8080)
#     training_client = TrainingDataClient("localhost", 8081)
    
#     if not ring_client.connect():
#         print("❌ Cannot benchmark - ring buffer connection failed")
#         return
    
#     try:
#         # Test real-time data throughput
#         start_time = time.time()
#         snapshots = 0
        
#         for i in range(10):
#             snapshot = ring_client.get_market_snapshot()
#             if snapshot:
#                 snapshots += 1
#             time.sleep(0.1)  # 100ms between requests
        
#         duration = time.time() - start_time
#         throughput = snapshots / duration
        
#         print(f"📊 Real-time Data Throughput:")
#         print(f"   Snapshots: {snapshots}/10 successful")
#         print(f"   Duration: {duration:.2f}s")
#         print(f"   Throughput: {throughput:.1f} snapshots/sec")
        
#     finally:
#         ring_client.disconnect()
#         if training_client.socket:
#             training_client.disconnect()

# if __name__ == "__main__":
#     # Run main demo
#     main()
    
#     # Run performance benchmark
#     try:
#         benchmark_performance()
#     except Exception as e:
#         print(f"❌ Benchmark failed: {e}") 
# #!/usr/bin/env python3
# """
# STK_Q Training Data Client for ML Model Training
# Accesses vast historical datasets stored in STK_Q
# """

# import socket
# import json
# import time
# import numpy as np
# from typing import Dict, List, Any, Optional, Tuple
# import pandas as pd
# from datetime import datetime, timedelta

# class TrainingDataClient:
#     """
#     STK_Q Training Data Client for ML Training
    
#     Purpose: Access vast amounts of historical tick data from STK_Q for ML training
#     - Handles potentially massive datasets (millions of ticks)
#     - Provides sampling, time-range queries, and data preprocessing
#     - Optimized for ML feature extraction and model training
#     """
    
#     def __init__(self, host: str = "localhost", port: int = 8081):  # Different port for training data
#         self.host = host
#         self.port = port
#         self.socket: Optional[socket.socket] = None
        
#     def connect(self) -> bool:
#         """Connect to the STK_Q training data server"""
#         try:
#             self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
#             self.socket.connect((self.host, self.port))
#             print(f"🔗 [TrainingData] Connected to {self.host}:{self.port}")
#             return True
#         except Exception as e:
#             print(f"❌ [TrainingData] Connection failed: {e}")
#             return False
    
#     def disconnect(self):
#         """Disconnect from server"""
#         if self.socket:
#             self.socket.close()
#             self.socket = None
#             print("🔌 [TrainingData] Disconnected")
    
#     def _send_request(self, command: str, **params) -> Dict[str, Any]:
#         """Send request with parameters and receive response"""
#         if not self.socket:
#             raise RuntimeError("Not connected to server")
        
#         request = {"command": command, **params}
#         request_str = json.dumps(request)
#         self.socket.send(request_str.encode('utf-8'))
        
#         # Handle potentially large responses for training data
#         response_data = b""
#         while True:
#             chunk = self.socket.recv(65536)
#             if not chunk:
#                 break
#             response_data += chunk
#             try:
#                 # Try to parse - if successful, we have complete JSON
#                 response_str = response_data.decode('utf-8').strip()
#                 return json.loads(response_str)
#             except (json.JSONDecodeError, UnicodeDecodeError):
#                 # Continue receiving
#                 continue
    
#     # ═══════════════════════════════════════════════════════════════════════════════
#     # ML TRAINING DATA ACCESS METHODS
#     # ═══════════════════════════════════════════════════════════════════════════════
    
#     def get_dataset_info(self) -> Dict[str, Any]:
#         """
#         Get comprehensive dataset information for ML planning
#         Returns: Dataset size, time span, memory usage, etc.
#         """
#         response = self._send_request("training_stats")
#         if response.get("success", False):
#             stats = response["data"]
            
#             # Enhanced dataset info
#             dataset_info = {
#                 'total_ticks': stats.get('total_ticks', 0),
#                 'is_empty': stats.get('is_empty', True),
#                 'time_span_hours': stats.get('time_span_hours', 0),
#                 'estimated_memory_mb': self._estimate_memory_usage(stats.get('total_ticks', 0)),
#                 'oldest_timestamp': stats.get('oldest_timestamp'),
#                 'newest_timestamp': stats.get('newest_timestamp'),
#                 'data_freshness': self._calculate_freshness(stats.get('newest_timestamp')),
#                 'recommended_sampling': self._recommend_sampling_rate(stats.get('total_ticks', 0))
#             }
            
#             return dataset_info
#         else:
#             print(f"❌ Error getting dataset info: {response.get('message', 'Unknown error')}")
#             return {}
    
#     def get_training_sample(self, sample_every_n: int = 10) -> pd.DataFrame:
#         """
#         Get sampled training data (every Nth tick) to manage memory
#         Args:
#             sample_every_n: Take every Nth tick (10 = 10% of data)
#         Returns: DataFrame with sampled tick data
#         """
#         response = self._send_request("sample_training", sample_every_n=sample_every_n)
#         if not response.get("success", False):
#             print(f"❌ Error getting training sample: {response.get('message', 'Unknown error')}")
#             return pd.DataFrame()
        
#         return self._format_training_data(response["data"])
    
#     def get_time_range_data(self, start_time: datetime, end_time: datetime) -> pd.DataFrame:
#         """
#         Get training data within specific time range
#         Args:
#             start_time: Start of time range
#             end_time: End of time range
#         Returns: DataFrame with ticks in time range
#         """
#         start_ms = int(start_time.timestamp() * 1000)
#         end_ms = int(end_time.timestamp() * 1000)
        
#         response = self._send_request("time_range_data", start_time_ms=start_ms, end_time_ms=end_ms)
#         if not response.get("success", False):
#             print(f"❌ Error getting time range data: {response.get('message', 'Unknown error')}")
#             return pd.DataFrame()
        
#         return self._format_training_data(response["data"])
    
#     def get_recent_training_data(self, num_ticks: int = 1000) -> pd.DataFrame:
#         """
#         Get most recent N ticks for training/validation
#         Args:
#             num_ticks: Number of recent ticks to retrieve
#         Returns: DataFrame with recent tick data
#         """
#         response = self._send_request("recent_ticks", num_ticks=num_ticks)
#         if not response.get("success", False):
#             print(f"❌ Error getting recent data: {response.get('message', 'Unknown error')}")
#             return pd.DataFrame()
        
#         return self._format_training_data(response["data"])
    
#     def _format_training_data(self, raw_data: List[Dict]) -> pd.DataFrame:
#         """
#         Format raw STK_Q data into ML-ready DataFrame
#         Includes all technical indicators and derived features
#         """
#         if not raw_data:
#             return pd.DataFrame()
        
#         df = pd.DataFrame(raw_data)
        
#         # Convert timestamp to datetime
#         if 'time' in df.columns:
#             df['timestamp'] = pd.to_datetime(df['time'], unit='ms')
#             df = df.sort_values('timestamp')
        
#         # Add derived features for ML
#         if not df.empty:
#             df = self._add_ml_features(df)
        
#         return df
    
#     def _add_ml_features(self, df: pd.DataFrame) -> pd.DataFrame:
#         """
#         Add ML-ready features to the dataset
#         """
#         # Price-based features
#         if 'last' in df.columns:
#             df['price_change'] = df['last'].pct_change()
#             df['price_volatility'] = df['last'].rolling(window=20).std()
#             df['price_ma_5'] = df['last'].rolling(window=5).mean()
#             df['price_ma_20'] = df['last'].rolling(window=20).mean()
        
#         # Spread features
#         if 'bid' in df.columns and 'ask' in df.columns:
#             df['spread'] = df['ask'] - df['bid']
#             df['spread_pct'] = df['spread'] / df['last'] * 100
#             df['mid_price'] = (df['bid'] + df['ask']) / 2
        
#         # Volume features
#         if 'volume' in df.columns:
#             df['volume_ma'] = df['volume'].rolling(window=10).mean()
#             df['volume_ratio'] = df['volume'] / df['volume_ma']
        
#         # Order book imbalance
#         if 'bidSize' in df.columns and 'askSize' in df.columns:
#             df['order_imbalance'] = (df['bidSize'] - df['askSize']) / (df['bidSize'] + df['askSize'])
        
#         # Technical indicators (if available from STK_Q)
#         technical_cols = ['rsi', 'ema9', 'ema26', 'alma', 'atr']
#         for col in technical_cols:
#             if col in df.columns:
#                 df[f'{col}_change'] = df[col].pct_change()
        
#         return df
    
#     # ═══════════════════════════════════════════════════════════════════════════════
#     # ML DATASET PREPARATION HELPERS
#     # ═══════════════════════════════════════════════════════════════════════════════
    
#     def prepare_ml_dataset(self, 
#                           lookback_periods: int = 50,
#                           prediction_horizon: int = 1,
#                           sample_rate: int = 5) -> Tuple[np.ndarray, np.ndarray]:
#         """
#         Prepare ML dataset with features and labels
#         Args:
#             lookback_periods: Number of historical periods for features
#             prediction_horizon: Periods ahead to predict
#             sample_rate: Sampling rate to manage dataset size
#         Returns: (X, y) arrays for ML training
#         """
#         print(f"🧠 [TrainingData] Preparing ML dataset...")
#         print(f"   Lookback: {lookback_periods}, Horizon: {prediction_horizon}, Sample: 1/{sample_rate}")
        
#         # Get sampled data
#         df = self.get_training_sample(sample_every_n=sample_rate)
        
#         if df.empty:
#             print("❌ No training data available")
#             return np.array([]), np.array([])
        
#         print(f"   Dataset shape: {df.shape}")
        
#         # Select feature columns
#         feature_cols = self._select_ml_features(df)
#         print(f"   Feature columns: {len(feature_cols)}")
        
#         # Create sequences for time series ML
#         X, y = self._create_sequences(df[feature_cols], 
#                                     df['last'], 
#                                     lookback_periods, 
#                                     prediction_horizon)
        
#         print(f"   ML dataset prepared: X{X.shape}, y{y.shape}")
#         return X, y
    
#     def _select_ml_features(self, df: pd.DataFrame) -> List[str]:
#         """Select relevant features for ML training"""
#         base_features = ['last', 'bid', 'ask', 'volume', 'vwap']
#         technical_features = ['rsi', 'ema9', 'ema26', 'alma', 'atr']
#         derived_features = ['spread', 'spread_pct', 'price_change', 'volume_ratio', 'order_imbalance']
        
#         # Only include features that exist in the dataset
#         available_features = []
#         for feature_set in [base_features, technical_features, derived_features]:
#             for col in feature_set:
#                 if col in df.columns:
#                     available_features.append(col)
        
#         return available_features
    
#     def _create_sequences(self, features: pd.DataFrame, targets: pd.Series, 
#                          lookback: int, horizon: int) -> Tuple[np.ndarray, np.ndarray]:
#         """Create sequences for time series ML"""
#         X, y = [], []
        
#         # Ensure no NaN values
#         features = features.fillna(method='ffill').fillna(0)
#         targets = targets.fillna(method='ffill')
        
#         for i in range(lookback, len(features) - horizon + 1):
#             # Feature sequence
#             X.append(features.iloc[i-lookback:i].values)
            
#             # Target (future price)
#             y.append(targets.iloc[i+horizon-1])
        
#         return np.array(X), np.array(y)
    
#     def _estimate_memory_usage(self, num_ticks: int) -> float:
#         """Estimate memory usage in MB for given number of ticks"""
#         # Rough estimate: each STK_Q tick ~200 bytes
#         bytes_per_tick = 200
#         return (num_ticks * bytes_per_tick) / (1024 * 1024)
    
#     def _calculate_freshness(self, newest_timestamp_ms: Optional[int]) -> str:
#         """Calculate how fresh the data is"""
#         if not newest_timestamp_ms:
#             return "Unknown"
        
#         newest_time = datetime.fromtimestamp(newest_timestamp_ms / 1000)
#         age = datetime.now() - newest_time
        
#         if age.total_seconds() < 3600:  # Less than 1 hour
#             return f"{int(age.total_seconds() / 60)} minutes old"
#         elif age.days < 1:
#             return f"{int(age.total_seconds() / 3600)} hours old"
#         else:
#             return f"{age.days} days old"
    
#     def _recommend_sampling_rate(self, total_ticks: int) -> int:
#         """Recommend sampling rate based on dataset size"""
#         if total_ticks < 10000:
#             return 1  # Use all data
#         elif total_ticks < 100000:
#             return 2  # Use half
#         elif total_ticks < 1000000:
#             return 5  # Use 20%
#         else:
#             return 10  # Use 10% for very large datasets
    
#     # ═══════════════════════════════════════════════════════════════════════════════
#     # DATA MANAGEMENT
#     # ═══════════════════════════════════════════════════════════════════════════════
    
#     def cleanup_old_data(self, hours_to_keep: int = 72):
#         """
#         Remove old training data to manage storage
#         Args:
#             hours_to_keep: Keep only data from last N hours
#         """
#         cutoff_time = datetime.now() - timedelta(hours=hours_to_keep)
#         cutoff_ms = int(cutoff_time.timestamp() * 1000)
        
#         response = self._send_request("cleanup_data", cutoff_time_ms=cutoff_ms)
#         if response.get("success", False):
#             print(f"✅ [TrainingData] Cleaned up data older than {hours_to_keep} hours")
#         else:
#             print(f"❌ Error cleaning up data: {response.get('message', 'Unknown error')}")

# def main():
#     """Demo the TrainingDataClient"""
#     print("🧠 STK_Q Training Data Client for ML")
    
#     client = TrainingDataClient("localhost", 8081)
    
#     if not client.connect():
#         print("❌ Failed to connect. Start the training data server first.")
#         return
    
#     try:
#         # Get dataset info
#         info = client.get_dataset_info()
#         print(f"\n📊 Dataset Info:")
#         for key, value in info.items():
#             print(f"   {key}: {value}")
        
#         # Prepare ML dataset
#         if info.get('total_ticks', 0) > 0:
#             X, y = client.prepare_ml_dataset(
#                 lookback_periods=20, 
#                 prediction_horizon=1, 
#                 sample_rate=5
#             )
            
#             if X.size > 0:
#                 print(f"\n✅ ML dataset ready for training!")
#                 print(f"   Features shape: {X.shape}")
#                 print(f"   Labels shape: {y.shape}")
#             else:
#                 print("❌ Failed to prepare ML dataset")
        
#     except KeyboardInterrupt:
#         print("\n⏹️  Interrupted by user")
#     finally:
#         client.disconnect()

# if __name__ == "__main__":
#     main() 
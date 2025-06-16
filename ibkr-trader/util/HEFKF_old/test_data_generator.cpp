// Test Data Generator for HEFKF Pipeline Testing
// Generates controlled market scenarios with 1Hz sampling for validation

#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <random>
#include <chrono>
#include <sstream>
#include <algorithm>

class TestDataGenerator {
public:
    struct MarketScenario {
        std::string name;
        std::vector<double> prices;
        std::vector<double> volumes;
        std::vector<double> spreads;
        std::vector<long> timestamps;  // milliseconds since epoch
    };
    
    // Generate pure uptrend with controlled noise
    static MarketScenario generate_uptrend(int n_points = 3000, 
                                          double start_price = 100.0,
                                          double trend_per_second = 0.0002,  // 0.02% per second (compound)
                                          double noise_level = 0.0001) {     // 0.01% noise
        MarketScenario scenario;
        scenario.name = "uptrend";
        
        std::mt19937 rng(42);  // Fixed seed for reproducibility
        std::normal_distribution<double> noise(0.0, noise_level);
        std::normal_distribution<double> volume_dist(10000.0, 1000.0);
        
        auto start_time = std::chrono::milliseconds(1700000000000);  // Fixed start time
        
        for (int i = 0; i < n_points; ++i) {
            // Exponential uptrend with small noise (compound returns)
            double trend_component = start_price * std::pow(1.0 + trend_per_second, i);
            double noise_component = trend_component * noise(rng);
            double price = trend_component + noise_component;
            
            // Volume increases slightly with uptrend
            double volume = std::max(100.0, volume_dist(rng) * (1.0 + 0.0001 * i));
            
            // Spread tightens slightly in uptrend
            double spread = 0.01 * (1.0 - 0.00001 * i);
            spread = std::max(0.005, spread);  // Minimum spread
            
            scenario.prices.push_back(price);
            scenario.volumes.push_back(volume);
            scenario.spreads.push_back(spread);
            scenario.timestamps.push_back((start_time + std::chrono::seconds(i)).count());
        }
        
        return scenario;
    }
    
    // Generate pure downtrend
    static MarketScenario generate_downtrend(int n_points = 3000,
                                            double start_price = 100.0,
                                            double trend_per_second = -0.0002,  // -0.02% per second (compound)
                                            double noise_level = 0.0001) {
        MarketScenario scenario;
        scenario.name = "downtrend";
        
        std::mt19937 rng(43);  // Different seed
        std::normal_distribution<double> noise(0.0, noise_level);
        std::normal_distribution<double> volume_dist(10000.0, 1000.0);
        
        auto start_time = std::chrono::milliseconds(1700000000000);
        
        for (int i = 0; i < n_points; ++i) {
            // Exponential downtrend with small noise (compound returns)
            // This ensures price never goes negative
            double trend_component = start_price * std::pow(1.0 + trend_per_second, i);
            double noise_component = trend_component * noise(rng);
            double price = trend_component + noise_component;
            
            // Safety check: ensure price stays positive (minimum $1)
            price = std::max(1.0, price);
            
            // Volume increases in downtrend (panic selling)
            double volume = std::max(100.0, volume_dist(rng) * (1.0 + 0.0002 * i));
            
            // Spread widens in downtrend
            double spread = 0.01 * (1.0 + 0.00002 * i);
            spread = std::min(0.05, spread);  // Maximum spread
            
            scenario.prices.push_back(price);
            scenario.volumes.push_back(volume);
            scenario.spreads.push_back(spread);
            scenario.timestamps.push_back((start_time + std::chrono::seconds(i)).count());
        }
        
        return scenario;
    }
    
    // Generate sideways/ranging market
    static MarketScenario generate_sideways(int n_points = 3000,
                                           double center_price = 100.0,
                                           double range_percent = 0.02,  // 2% range
                                           double period_seconds = 300) { // 5-minute cycles
        MarketScenario scenario;
        scenario.name = "sideways";
        
        std::mt19937 rng(44);
        std::normal_distribution<double> noise(0.0, 0.0001);
        std::normal_distribution<double> volume_dist(10000.0, 1000.0);
        
        auto start_time = std::chrono::milliseconds(1700000000000);
        
        for (int i = 0; i < n_points; ++i) {
            // Sinusoidal price movement
            double phase = 2.0 * M_PI * i / period_seconds;
            double sine_component = center_price * (1.0 + range_percent * std::sin(phase));
            double noise_component = center_price * noise(rng);
            double price = sine_component + noise_component;
            
            // Volume peaks at extremes
            double volume_multiplier = 1.0 + 0.3 * std::abs(std::sin(phase));
            double volume = std::max(100.0, volume_dist(rng) * volume_multiplier);
            
            // Constant spread in sideways market
            double spread = 0.01;
            
            scenario.prices.push_back(price);
            scenario.volumes.push_back(volume);
            scenario.spreads.push_back(spread);
            scenario.timestamps.push_back((start_time + std::chrono::seconds(i)).count());
        }
        
        return scenario;
    }
    
    // Generate volatile/choppy market
    static MarketScenario generate_volatile(int n_points = 3000,
                                           double start_price = 100.0,
                                           double volatility = 0.005) {  // 0.5% std dev
        MarketScenario scenario;
        scenario.name = "volatile";
        
        std::mt19937 rng(45);
        std::normal_distribution<double> returns(0.0, volatility);
        std::normal_distribution<double> volume_dist(15000.0, 3000.0);
        std::uniform_real_distribution<double> spread_dist(0.01, 0.03);
        
        auto start_time = std::chrono::milliseconds(1700000000000);
        
        double price = start_price;
        for (int i = 0; i < n_points; ++i) {
            // Random walk with high volatility
            double return_pct = returns(rng);
            price *= (1.0 + return_pct);
            
            // High and variable volume
            double volume = std::max(100.0, volume_dist(rng));
            
            // Variable spread
            double spread = spread_dist(rng);
            
            scenario.prices.push_back(price);
            scenario.volumes.push_back(volume);
            scenario.spreads.push_back(spread);
            scenario.timestamps.push_back((start_time + std::chrono::seconds(i)).count());
        }
        
        return scenario;
    }
    
    // Generate step function (sudden jumps)
    static MarketScenario generate_steps(int n_points = 3000,
                                        double start_price = 100.0,
                                        int steps_per_scenario = 10,
                                        double step_size_percent = 0.01) {  // 1% jumps
        MarketScenario scenario;
        scenario.name = "steps";
        
        std::mt19937 rng(46);
        std::normal_distribution<double> noise(0.0, 0.00005);  // Very small noise
        std::normal_distribution<double> volume_dist(10000.0, 1000.0);
        std::uniform_int_distribution<int> step_direction(-1, 1);
        
        auto start_time = std::chrono::milliseconds(1700000000000);
        
        int points_per_step = n_points / steps_per_scenario;
        double price = start_price;
        
        for (int i = 0; i < n_points; ++i) {
            // Check if we should make a step
            if (i > 0 && i % points_per_step == 0) {
                int direction = step_direction(rng);
                if (direction != 0) {
                    price *= (1.0 + direction * step_size_percent);
                }
            }
            
            // Add tiny noise
            double noisy_price = price * (1.0 + noise(rng));
            
            // Volume spikes at steps
            double volume = volume_dist(rng);
            if (i > 0 && i % points_per_step < 10) {  // 10 seconds after step
                volume *= 3.0;  // Triple volume
            }
            
            // Spread widens at steps
            double spread = 0.01;
            if (i > 0 && i % points_per_step < 5) {  // 5 seconds after step
                spread = 0.03;
            }
            
            scenario.prices.push_back(noisy_price);
            scenario.volumes.push_back(volume);
            scenario.spreads.push_back(spread);
            scenario.timestamps.push_back((start_time + std::chrono::seconds(i)).count());
        }
        
        return scenario;
    }
    
    // Generate market with known frequency components for testing frequency analysis
    static MarketScenario generate_frequency_test(int n_points = 3000,
                                                 double start_price = 100.0) {
        MarketScenario scenario;
        scenario.name = "frequency_test";
        
        std::mt19937 rng(47);
        std::normal_distribution<double> noise(0.0, 0.00001);  // Very low noise
        
        auto start_time = std::chrono::milliseconds(1700000000000);
        
        for (int i = 0; i < n_points; ++i) {
            // Combine multiple known frequencies
            double t = static_cast<double>(i);
            
            // Microstructure: 30-second period (0.033 Hz)
            double micro = 0.001 * std::sin(2.0 * M_PI * t / 30.0);
            
            // Short-term: 5-minute period (0.0033 Hz)
            double short_term = 0.003 * std::sin(2.0 * M_PI * t / 300.0);
            
            // Medium-term: 30-minute period (0.00056 Hz)
            double medium = 0.005 * std::sin(2.0 * M_PI * t / 1800.0);
            
            // Trend: slight upward drift
            double trend = 0.00001 * t;
            
            double price = start_price * (1.0 + micro + short_term + medium + trend + noise(rng));
            
            // Volume correlated with price changes
            double price_change = (i > 0) ? std::abs(price - scenario.prices.back()) / scenario.prices.back() : 0.0;
            double volume = 10000.0 * (1.0 + 100.0 * price_change);
            
            // Constant spread for clean frequency analysis
            double spread = 0.01;
            
            scenario.prices.push_back(price);
            scenario.volumes.push_back(volume);
            scenario.spreads.push_back(spread);
            scenario.timestamps.push_back((start_time + std::chrono::seconds(i)).count());
        }
        
        return scenario;
    }
    
    // Save scenario to CSV file
    static void save_to_csv(const MarketScenario& scenario, const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        
        // Write header
        file << "Symbol,Timestamp,Last,Bid,Ask,BidSize,AskSize,Volume,Open,High,Low,Close,Spread,VWAP,RSI,EMA9,EMA26,ALMA,ATR,up_001_002,up_002_005,up_005_010,up_010_plus,dn_001_002,dn_002_005,dn_005_010,dn_010_plus\n";
        
        // Write data
        for (size_t i = 0; i < scenario.prices.size(); ++i) {
            double price = scenario.prices[i];
            double spread = scenario.spreads[i];
            double bid = price - spread / 2.0;
            double ask = price + spread / 2.0;
            
            file << std::fixed << std::setprecision(4);
            file << "TEST," << scenario.timestamps[i] << ",";
            file << price << "," << bid << "," << ask << ",";
            file << "1000,1000,";  // Dummy bid/ask sizes
            file << static_cast<int>(scenario.volumes[i]) << ",";
            file << price << "," << price << "," << price << "," << price << ",";  // OHLC
            file << spread << ",";
            file << price << ",";  // VWAP
            file << "50.0000,";    // RSI (neutral)
            file << "0.0000,0.0000,0.0000,0.0000,";  // EMA9, EMA26, ALMA, ATR
            file << "0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00\n";  // Bucket confidences
        }
        
        file.close();
        std::cout << "Saved " << scenario.name << " scenario to " << filename 
                  << " (" << scenario.prices.size() << " points)" << std::endl;
    }
    
    // Generate summary statistics for validation
    static void print_scenario_stats(const MarketScenario& scenario) {
        if (scenario.prices.empty()) return;
        
        double min_price = *std::min_element(scenario.prices.begin(), scenario.prices.end());
        double max_price = *std::max_element(scenario.prices.begin(), scenario.prices.end());
        double first_price = scenario.prices.front();
        double last_price = scenario.prices.back();
        double total_return = (last_price - first_price) / first_price * 100.0;
        
        // Calculate actual sampling rate
        if (scenario.timestamps.size() > 1) {
            double avg_interval = 0.0;
            for (size_t i = 1; i < scenario.timestamps.size(); ++i) {
                avg_interval += (scenario.timestamps[i] - scenario.timestamps[i-1]) / 1000.0;
            }
            avg_interval /= (scenario.timestamps.size() - 1);
            
            std::cout << "\nScenario: " << scenario.name << std::endl;
            std::cout << "Points: " << scenario.prices.size() << std::endl;
            std::cout << "Price range: " << min_price << " - " << max_price << std::endl;
            std::cout << "Total return: " << total_return << "%" << std::endl;
            std::cout << "Average sampling interval: " << avg_interval << " seconds" << std::endl;
            std::cout << "Expected: 1.0 seconds (1 Hz)" << std::endl;
        }
    }
};

int main() {
    std::cout << "Generating test data scenarios for HEFKF pipeline validation...\n" << std::endl;
    
    // Generate all scenarios
    auto uptrend = TestDataGenerator::generate_uptrend(3000);
    auto downtrend = TestDataGenerator::generate_downtrend(3000);
    auto sideways = TestDataGenerator::generate_sideways(3000);
    auto volatile_market = TestDataGenerator::generate_volatile(3000);
    auto steps = TestDataGenerator::generate_steps(3000);
    auto freq_test = TestDataGenerator::generate_frequency_test(3000);
    
    // Save to CSV files
    TestDataGenerator::save_to_csv(uptrend, "test_data_uptrend.csv");
    TestDataGenerator::save_to_csv(downtrend, "test_data_downtrend.csv");
    TestDataGenerator::save_to_csv(sideways, "test_data_sideways.csv");
    TestDataGenerator::save_to_csv(volatile_market, "test_data_volatile.csv");
    TestDataGenerator::save_to_csv(steps, "test_data_steps.csv");
    TestDataGenerator::save_to_csv(freq_test, "test_data_frequency.csv");
    
    // Print statistics
    TestDataGenerator::print_scenario_stats(uptrend);
    TestDataGenerator::print_scenario_stats(downtrend);
    TestDataGenerator::print_scenario_stats(sideways);
    TestDataGenerator::print_scenario_stats(volatile_market);
    TestDataGenerator::print_scenario_stats(steps);
    TestDataGenerator::print_scenario_stats(freq_test);
    
    std::cout << "\nTest data generation complete!" << std::endl;
    
    return 0;
} 
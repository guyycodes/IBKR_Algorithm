// volume_profile_map.cpp
//
// Purpose: Map of volume profiles for each price level
// Track accumulation/distribution patterns to detect liquidity extraction
//

#include "volume_profile_map.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <set>

namespace volume_profile_map {

VolumeProfileMap::VolumeProfileMap(double price_increment) 
    : price_increment_(price_increment) {
    if (price_increment_ <= 0.0) {
        price_increment_ = 0.05; // Default to 5-cent buckets
    }
    
    // Ensure price increment is reasonable for floating point precision
    // Round to nearest reasonable precision (e.g., 4 decimal places for cents)
    price_increment_ = std::round(price_increment_ * 10000.0) / 10000.0;
    
    // Convert price increment to cents for integer arithmetic
    tick_size_cents_ = static_cast<int>(std::round(price_increment_ * CENTS_PER_DOLLAR));
}

int VolumeProfileMap::price_to_bucket_index(double price) const {
    // Convert price to cents and use integer arithmetic for exact bucketing
    // Add small epsilon to handle floating point precision issues before floor
    int price_in_cents = static_cast<int>(std::floor(price * CENTS_PER_DOLLAR + 1e-6));
    return price_in_cents / tick_size_cents_;
}

double VolumeProfileMap::bucket_index_to_price(int bucket_index) const {
    // Convert bucket index back to dollar price
    return (bucket_index * tick_size_cents_) / static_cast<double>(CENTS_PER_DOLLAR);
}

void VolumeProfileMap::add_transaction(double price, int volume) {
    if (volume <= 0) {
        return; // Only add positive volumes
    }
    
    if (price < 0.0) {
        return; // Skip negative prices (not valid in most markets)
    }
    
    int bucket_index = price_to_bucket_index(price);
    volume_map_[bucket_index].insert(volume);  // O(log N) insertion
}

int VolumeProfileMap::remove_transaction(double price, int volume) {
    if (volume <= 0) {
        return volume; // Nothing to remove
    }
    
    if (price < 0.0) {
        return volume; // Skip negative prices, return unfulfilled volume
    }
    
    int bucket_index = price_to_bucket_index(price);
    auto it = volume_map_.find(bucket_index);
    
    if (it == volume_map_.end() || it->second.empty()) {
        return volume; // No volume at this price, return full unfulfilled amount
    }
    
    std::multiset<int>& volumes = it->second;
    
    // First, try to find an exact match for efficiency - O(log N)
    auto exact_match = volumes.find(volume);
    if (exact_match != volumes.end()) {
        volumes.erase(exact_match);  // O(log N)
        
        // Clean up empty bucket
        if (volumes.empty()) {
            volume_map_.erase(it);
        }
        return 0; // Exact match found and removed, fully fulfilled
    }
    
    // No exact match, remove from largest elements first
    int remaining_to_remove = volume;
    
    while (remaining_to_remove > 0 && !volumes.empty()) {
        auto max_it = std::prev(volumes.end());  // Iterator to largest element - O(1)
        int top = *max_it;                       // O(1)
        
        if (top <= remaining_to_remove) {
            // Remove this entire element
            remaining_to_remove -= top;
            volumes.erase(max_it);               // O(log N)
        } else {
            // Partially reduce this element
            volumes.insert(top - remaining_to_remove);  // O(log N) - insert new reduced value first
            volumes.erase(max_it);                       // O(log N) - remove old value after successful insert
            remaining_to_remove = 0;
        }
    }
    
    // Clean up empty bucket
    if (volumes.empty()) {
        volume_map_.erase(it);
    }
    
    return remaining_to_remove; // Return any unfulfilled remainder
}

int VolumeProfileMap::get_net_volume(double price) const {
    if (price < 0.0) {
        return 0; // No volume for negative prices
    }
    
    int bucket_index = price_to_bucket_index(price);
    auto it = volume_map_.find(bucket_index);
    
    if (it == volume_map_.end()) {
        return 0;
    }
    
    // Sum all volumes in this bucket
    return std::accumulate(it->second.begin(), it->second.end(), 0);
}

int VolumeProfileMap::get_total_inventory(double low_price, double high_price) const {
    if (low_price > high_price) {
        return 0;
    }
    
    // Clamp to non-negative range for typical market scenarios
    if (high_price < 0.0) {
        return 0; // Entire range is negative
    }
    if (low_price < 0.0) {
        low_price = 0.0; // Clamp lower bound to zero
    }
    
    int low_bucket = price_to_bucket_index(low_price);
    int high_bucket = price_to_bucket_index(high_price);
    
    int total = 0;
    auto lo = volume_map_.lower_bound(low_bucket);
    for (auto it = lo; it != volume_map_.end() && it->first <= high_bucket; ++it) {
        total += std::accumulate(it->second.begin(), it->second.end(), 0);
    }
    
    return total;
}

const std::map<int, std::multiset<int>>& VolumeProfileMap::get_volume_map() const {
    return volume_map_;
}

void VolumeProfileMap::clear() {
    volume_map_.clear();
}

std::string VolumeProfileMap::get_summary() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "=== VOLUME PROFILE SUMMARY ===" << std::endl;
    oss << "Price Increment: $" << price_increment_ << std::endl;
    oss << "Total Price Levels: " << volume_map_.size() << std::endl;
    
    if (volume_map_.empty()) {
        oss << "No volume data available." << std::endl;
        return oss.str();
    }
    
    // Calculate total volume across all levels
    int total_volume = 0;
    double min_price = bucket_index_to_price(volume_map_.begin()->first);
    double max_price = bucket_index_to_price(volume_map_.rbegin()->first);
    
    for (const auto& [bucket_index, volumes] : volume_map_) {
        int level_volume = std::accumulate(volumes.begin(), volumes.end(), 0);
        total_volume += level_volume;
    }
    
    oss << "Price Range: $" << min_price << " - $" << max_price << std::endl;
    oss << "Total Volume: " << total_volume << " shares" << std::endl;
    oss << "Average Volume per Level: " << (total_volume / volume_map_.size()) << " shares" << std::endl;
    
    // Show top 5 volume levels
    std::vector<std::pair<double, int>> volume_levels;
    for (const auto& [bucket_index, volumes] : volume_map_) {
        int level_volume = std::accumulate(volumes.begin(), volumes.end(), 0);
        double price = bucket_index_to_price(bucket_index);
        volume_levels.emplace_back(price, level_volume);
    }
    
    std::sort(volume_levels.begin(), volume_levels.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    oss << std::endl << "Top Volume Levels:" << std::endl;
    for (size_t i = 0; i < std::min(volume_levels.size(), size_t(5)); ++i) {
        oss << "  $" << volume_levels[i].first << ": " << volume_levels[i].second << " shares" << std::endl;
    }
    
    return oss.str();
}

std::string VolumeProfileMap::detect_liquidity_patterns(int min_large_volume) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "=== LIQUIDITY PATTERN ANALYSIS ===" << std::endl;
    oss << "Large Volume Threshold: " << min_large_volume << " shares" << std::endl;
    
    if (volume_map_.empty()) {
        oss << "No volume data available for pattern analysis." << std::endl;
        return oss.str();
    }
    
    int institutional_levels = 0;
    int mixed_levels = 0;
    int retail_levels = 0;
    std::vector<double> distribution_candidates;
    
    for (const auto& [bucket_index, volumes] : volume_map_) {
        if (volumes.empty()) continue;
        
        double price = bucket_index_to_price(bucket_index);
        
        // Analyze composition of this price level
        int large_blocks = 0;
        int small_blocks = 0;
        int total_large_volume = 0;
        int total_small_volume = 0;
        
        for (int volume : volumes) {
            if (volume >= min_large_volume) {
                large_blocks++;
                total_large_volume += volume;
            } else {
                small_blocks++;
                total_small_volume += volume;
            }
        }
        
        // Classify the level
        if (large_blocks > 0 && small_blocks == 0) {
            institutional_levels++;
        } else if (large_blocks > 0 && small_blocks > 0) {
            mixed_levels++;
            
            // Check for potential distribution pattern
            // Large volume + many small volumes could indicate distribution
            if (large_blocks >= 1 && small_blocks >= 3) {
                distribution_candidates.push_back(price);
            }
        } else {
            retail_levels++;
        }
    }
    
    oss << std::endl << "Level Classification:" << std::endl;
    oss << "  Institutional Only: " << institutional_levels << " levels" << std::endl;
    oss << "  Mixed (Inst + Retail): " << mixed_levels << " levels" << std::endl;
    oss << "  Retail Only: " << retail_levels << " levels" << std::endl;
    
    if (!distribution_candidates.empty()) {
        oss << std::endl << "Potential Distribution Patterns Detected:" << std::endl;
        for (double price : distribution_candidates) {
            int bucket_index = price_to_bucket_index(price);
            const auto& volumes = volume_map_.at(bucket_index);
            int net_volume = std::accumulate(volumes.begin(), volumes.end(), 0);
            
            oss << "  $" << price << " (" << volumes.size() << " transactions, " 
                << net_volume << " total shares)" << std::endl;
            
            // Show the volume breakdown
            // Since multiset is already sorted, use reverse iterators to get largest first
            oss << "    Volumes: ";
            auto it = volumes.rbegin();
            for (size_t i = 0; i < std::min(volumes.size(), size_t(5)) && it != volumes.rend(); ++i, ++it) {
                oss << *it;
                if (i < std::min(volumes.size(), size_t(5)) - 1) oss << ", ";
            }
            if (volumes.size() > 5) {
                oss << " (+" << (volumes.size() - 5) << " more)";
            }
            oss << std::endl;
        }
    } else {
        oss << std::endl << "No clear distribution patterns detected." << std::endl;
    }
    
    // Calculate concentration metrics
    double min_price = bucket_index_to_price(volume_map_.begin()->first);
    double max_price = bucket_index_to_price(volume_map_.rbegin()->first);
    double price_range = max_price - min_price;
    double avg_spacing = price_range / volume_map_.size();
    
    oss << std::endl << "Concentration Metrics:" << std::endl;
    oss << "  Price Range: $" << price_range << std::endl;
    oss << "  Average Level Spacing: $" << avg_spacing << std::endl;
    oss << "  Concentration Ratio: " << (mixed_levels + institutional_levels) 
        << "/" << volume_map_.size() << " ("
        << (100.0 * (mixed_levels + institutional_levels) / volume_map_.size()) << "%)" << std::endl;
    
    return oss.str();
}

std::vector<double> VolumeProfileMap::find_high_volume_nodes(double threshold_percentile) const {
    if (volume_map_.empty()) return {};
    
    // Calculate volume at each price level
    std::vector<std::pair<double, int>> price_volumes;
    for (const auto& [bucket_index, volumes] : volume_map_) {
        int level_volume = std::accumulate(volumes.begin(), volumes.end(), 0);
        double price = bucket_index_to_price(bucket_index);
        price_volumes.emplace_back(price, level_volume);
    }
    
    // Sort by volume to find threshold
    std::vector<int> volumes_only;
    for (const auto& [price, volume] : price_volumes) {
        volumes_only.push_back(volume);
    }
    std::sort(volumes_only.begin(), volumes_only.end());
    
    int threshold_index = static_cast<int>(volumes_only.size() * threshold_percentile);
    int volume_threshold = volumes_only[std::max(0, threshold_index - 1)];
    
    // Find prices that meet the threshold
    std::vector<double> high_volume_prices;
    for (const auto& [price, volume] : price_volumes) {
        if (volume >= volume_threshold) {
            high_volume_prices.push_back(price);
        }
    }
    
    return high_volume_prices;
}

std::pair<double, double> VolumeProfileMap::find_value_area(double percentage) const {
    if (volume_map_.empty()) return {0.0, 0.0};
    
    // Calculate total volume and find POC
    int total_volume = 0;
    int max_volume = 0;
    int poc_bucket = 0;
    
    for (const auto& [bucket_index, volumes] : volume_map_) {
        int level_volume = std::accumulate(volumes.begin(), volumes.end(), 0);
        total_volume += level_volume;
        if (level_volume > max_volume) {
            max_volume = level_volume;
            poc_bucket = bucket_index;
        }
    }
    
    // Find value area by expanding from POC
    int target_volume = static_cast<int>(total_volume * percentage);
    int accumulated_volume = max_volume;
    
    int lower_bucket = poc_bucket;
    int upper_bucket = poc_bucket;
    
    while (accumulated_volume < target_volume) {
        int lower_volume = 0;
        int upper_volume = 0;
        
        // Find next populated bucket below current range
        auto lower_it = volume_map_.lower_bound(lower_bucket);
        if (lower_it != volume_map_.begin()) {
            --lower_it;  // Move to previous populated bucket
            lower_volume = std::accumulate(lower_it->second.begin(), lower_it->second.end(), 0);
        }
        
        // Find next populated bucket above current range
        auto upper_it = volume_map_.upper_bound(upper_bucket);
        if (upper_it != volume_map_.end()) {
            upper_volume = std::accumulate(upper_it->second.begin(), upper_it->second.end(), 0);
        }
        
        // Expand in direction with higher volume
        if (lower_volume >= upper_volume && lower_volume > 0) {
            lower_bucket = lower_it->first;
            accumulated_volume += lower_volume;
        } else if (upper_volume > 0) {
            upper_bucket = upper_it->first;
            accumulated_volume += upper_volume;
        } else {
            break; // No more data to add
        }
    }
    
    return {bucket_index_to_price(lower_bucket), bucket_index_to_price(upper_bucket)};
}

} // namespace volume_profile_map

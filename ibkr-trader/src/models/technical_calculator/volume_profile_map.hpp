// volume_profile_map.hpp
//
// Purpose: Map of volume profiles for each price level
// Track accumulation/distribution patterns to detect liquidity extraction
//

#ifndef VOLUME_PROFILE_MAP_HPP
#define VOLUME_PROFILE_MAP_HPP

#include <map>
#include <vector>
#include <set>
#include <string>
#include <utility>
#include <iostream>

namespace volume_profile_map {

/**
 * VolumeProfileMap - Track order flow at different price levels
 * 
 * Purpose: Track accumulation/distribution patterns with an aim to detect liquidity extraction
 * 
 * Data Structure: map<int, multiset<int>> volume_map - bucket_index -> multiset of transaction volumes
 * 
 * Example Usage: (0.05 cent incremental buckets)
 * {820: {75000, 500, 200},    // Institution + retail bought bucket 820 ($41.00-41.04 range)
 *  821: {75000, 500, 200},    // Institution + retail bought bucket 821 ($41.05-41.09 range)
 *  822: {75000, 500, 200},    // Institution + retail bought bucket 822 ($41.10-41.14 range)
 *  ...
 *  855: {60000, 300, 1000},   // Institution + retail bought bucket 855 ($42.75-42.79 range)
 *  ...
 *  879: {40000, 800}}         // Institution bought bucket 879 ($43.95-43.99 range)
 * 
 * Goal: Track when large positions get distributed through smaller retail-sized sales,
 *       helping tip off liquidity generation patterns.
 */
class VolumeProfileMap {
public:
    /**
     * Constructor
     * @param price_increment The price increment for bucketing (e.g., 0.05 for 5-cent buckets)
     */
    explicit VolumeProfileMap(double price_increment = 0.05);
    
    /**
     * Add a transaction to the volume profile
     * @param price The exact price of the transaction
     * @param volume The volume of the transaction (positive integer)
     */
    void add_transaction(double price, int volume);
    
    /**
     * Remove volume from a price bucket
     * If the bucket's multiset contains an element equal to volume, remove it and return 0.
     * Otherwise, subtract volume from the largest elements in that bucket until fulfilled.
     * @param price The price bucket to remove from
     * @param volume The volume to remove
     * @return Any unfulfilled remainder volume
     */
    int remove_transaction(double price, int volume);
    
    /**
     * Get the net volume at a specific price level
     * @param price The price level to query
     * @return Sum of all volume entries in the bucket at price
     */
    int get_net_volume(double price) const;
    
    /**
     * Get total inventory across a price range
     * @param low_price The lower bound of the price range (inclusive)
     * @param high_price The upper bound of the price range (inclusive)
     * @return Sum of get_net_volume(p) for every bucket p in [low_price, high_price]
     */
    int get_total_inventory(double low_price, double high_price) const;
    
    /**
     * Get the volume profile data for analysis
     * @return Reference to the internal volume map (bucket_index -> volumes)
     */
    const std::map<int, std::multiset<int>>& get_volume_map() const;
    
    /**
     * Clear all volume data
     */
    void clear();
    
    /**
     * Get summary statistics for analysis
     * @return String containing volume profile summary
     */
    std::string get_summary() const;
    
    /**
     * Detect potential liquidity extraction patterns
     * @param min_large_volume Minimum volume to consider "large" (default 10000)
     * @return String describing detected patterns
     */
    std::string detect_liquidity_patterns(int min_large_volume = 10000) const;
    
    /**
     * Find high volume nodes above a percentile threshold
     * @param threshold_percentile Percentile threshold (0.0 to 1.0)
     * @return Vector of prices with high volume
     */
    std::vector<double> find_high_volume_nodes(double threshold_percentile) const;
    
    /**
     * Find value area containing specified percentage of volume
     * @param percentage Percentage of total volume to include (0.0 to 1.0)
     * @return Pair of (low_price, high_price) defining the value area
     */
    std::pair<double, double> find_value_area(double percentage) const;
    
    /**
     * Convert bucket index back to price for display
     * @param bucket_index The bucket index
     * @return The bucket price in dollars
     */
    double bucket_index_to_price(int bucket_index) const;
    
    /**
     * Print the current volume map for debugging/inspection
     * Shows only buckets that contain volumes
     */
    void print_volume_map() const;

private:
    static constexpr int CENTS_PER_DOLLAR = 100;
    
    std::map<int, std::multiset<int>> volume_map_;
    double price_increment_;
    int tick_size_cents_;  // price_increment converted to cents
    
    /**
     * Convert price to bucket index using integer arithmetic
     * @param price The exact price in dollars
     * @return The bucket index (integer)
     */
    int price_to_bucket_index(double price) const;
};

} // namespace volume_profile_map

#endif // VOLUME_PROFILE_MAP_HPP
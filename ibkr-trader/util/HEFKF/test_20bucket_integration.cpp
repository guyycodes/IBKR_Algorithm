// Test file to verify 20-bucket integration for both 1min and 5min HEFKF
#include "1min_HEFKF.hpp"
#include "5min_HEFKF.hpp"
#include <iostream>

int main() {
    std::cout << "Testing 20-bucket integration..." << std::endl;
    
    // Test 1min HEFKF
    {
        hefkf_1min::BucketConfidence20 bc1;
        bc1.up_020 = 0.3;  // 0.50% bucket
        bc1.up_040 = 0.5;  // 1.00% bucket  
        bc1.dn_010 = 0.2;  // -0.25% bucket
        bc1.normalize();
        
        auto stats1 = hefkf_1min::BucketExpectation20::compute_expectation(bc1);
        std::cout << "1min - Expected return: " << stats1.mean_return * 100 << "%" << std::endl;
        std::cout << "1min - Variance: " << stats1.variance << std::endl;
    }
    
    // Test 5min HEFKF
    {
        hefkf_5min::BucketConfidence20 bc5;
        bc5.up_020 = 0.3;  // 0.50% bucket
        bc5.up_040 = 0.5;  // 1.00% bucket
        bc5.dn_010 = 0.2;  // -0.25% bucket
        bc5.normalize();
        
        auto stats5 = hefkf_5min::BucketExpectation20::compute_expectation(bc5);
        std::cout << "5min - Expected return: " << stats5.mean_return * 100 << "%" << std::endl;
        std::cout << "5min - Variance: " << stats5.variance << std::endl;
    }
    
    std::cout << "✓ 20-bucket integration test complete!" << std::endl;
    return 0;
} 
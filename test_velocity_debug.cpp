#include <iostream>
#include <Eigen/Dense>

int main() {
    // Simulate the issue
    std::cout << "=== Velocity Tracking Debug ===" << std::endl;
    
    // Current noise configuration
    double R_velocity = 0.0001;  // Measurement noise
    double Q_velocity = 0.002;   // Process noise (10x base)
    double ratio = R_velocity / Q_velocity;
    
    std::cout << "R/Q ratio: " << ratio << " (filter trusts measurements " << 1/ratio << "x more)" << std::endl;
    
    // Simulate Kalman gain calculation for velocity
    double P_velocity = 0.1;  // Typical covariance
    double S = P_velocity + R_velocity;  // Innovation covariance
    double K_velocity = P_velocity / S;  // Kalman gain
    
    std::cout << "Kalman gain for velocity: " << K_velocity << std::endl;
    std::cout << "This means " << K_velocity * 100 << "% weight on new measurement" << std::endl;
    
    // The issue: Control input affects price but NOT velocity
    std::cout << "\n=== FOUND THE ISSUE ===" << std::endl;
    std::cout << "1. Control matrix B only affects price: B = [1, 0, 0, 0]" << std::endl;
    std::cout << "2. When bucket predictions add control_input to price," << std::endl;
    std::cout << "   velocity state is NOT updated accordingly" << std::endl;
    std::cout << "3. This creates inconsistency: price jumps but velocity doesn't" << std::endl;
    
    // Additionally, frequency nudging only affects price
    std::cout << "\n4. Frequency nudging also only modifies price state x[0]" << std::endl;
    std::cout << "5. This further disconnects price and velocity evolution" << std::endl;
    
    // The result
    std::cout << "\n=== RESULT ===" << std::endl;
    std::cout << "Even though the filter trusts velocity measurements," << std::endl;
    std::cout << "the control inputs and frequency nudging bypass velocity," << std::endl;
    std::cout << "causing the velocity to converge to average of measurements" << std::endl;
    std::cout << "rather than tracking the actual trend." << std::endl;
    
    return 0;
} 
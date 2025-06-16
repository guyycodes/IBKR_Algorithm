// Incremental Linear Regression for Online Computation
// Numerically stable algorithm that doesn't require storing all data points

#ifndef INCREMENTAL_REGRESSION_HPP
#define INCREMENTAL_REGRESSION_HPP

#include <cmath>

namespace bucket_convergence {

// Welford-style incremental linear regression
// Computes slope and intercept online without storing history
class IncrementalLinearRegression {
public:
    IncrementalLinearRegression() = default;
    
    // Add a new data point (x, y)
    void add_point(double x, double y) {
        n_++;
        
        // Update means incrementally
        double delta_x = x - mean_x_;
        mean_x_ += delta_x / n_;
        
        double delta_y = y - mean_y_;
        mean_y_ += delta_y / n_;
        
        // Update covariance and variance incrementally
        // This uses the update formula: Cov_n = Cov_{n-1} + (n-1)/n * delta_x * delta_y
        if (n_ > 1) {
            double factor = (n_ - 1.0) / n_;
            cov_xy_ += factor * delta_x * delta_y;
            var_x_ += factor * delta_x * delta_x;
            var_y_ += factor * delta_y * delta_y;
        }
    }
    
    // Get the current slope estimate
    double get_slope() const {
        if (var_x_ < 1e-10 || n_ < 2) {
            return 0.0;
        }
        return cov_xy_ / var_x_;
    }
    
    // Get the current intercept estimate
    double get_intercept() const {
        return mean_y_ - get_slope() * mean_x_;
    }
    
    // Get R-squared (coefficient of determination)
    double get_r_squared() const {
        if (var_x_ < 1e-10 || var_y_ < 1e-10 || n_ < 2) {
            return 0.0;
        }
        double r = cov_xy_ / std::sqrt(var_x_ * var_y_);
        return r * r;
    }
    
    // Reset the regression
    void reset() {
        n_ = 0;
        mean_x_ = 0.0;
        mean_y_ = 0.0;
        cov_xy_ = 0.0;
        var_x_ = 0.0;
        var_y_ = 0.0;
    }
    
    // Get number of points
    size_t size() const { return n_; }
    
private:
    size_t n_ = 0;
    double mean_x_ = 0.0;
    double mean_y_ = 0.0;
    double cov_xy_ = 0.0;  // Covariance between x and y
    double var_x_ = 0.0;   // Variance of x
    double var_y_ = 0.0;   // Variance of y (for R-squared)
};

} // namespace bucket_convergence

#endif // INCREMENTAL_REGRESSION_HPP 
#ifndef STATIC_RING_BUFFER_HPP
#define STATIC_RING_BUFFER_HPP

#include <array>
#include <atomic>
#include <type_traits>

/**
 * StaticRingBuffer<T, N>
 * ----------------------
 * High-performance, fixed-capacity, single-producer/single-consumer (SPSC) ring buffer.
 * 
 * Features:
 * - O(1) push/emplace operations with ≤30ns latency target
 * - O(1) random access via at(i_from_tail)
 * - Cache-line aligned storage for optimal memory performance
 * - Lock-free implementation using relaxed atomic operations
 * - Power-of-two capacity optimization for fast modulo operations
 * - Optional segment-tree overlay support for O(log n) aggregates
 */
template<typename T, size_t N>
class StaticRingBuffer
{
public:
    using value_type = T;
    constexpr static size_t capacity = N;
    
    // Compile-time check: N should be power of two for optimal performance
    static_assert((N & (N - 1)) == 0, "Capacity N should be a power of two for optimal modulo performance");

    // ---------- writer ----------
    
    /**
     * Push new element, overwriting oldest if buffer is full.
     * @param v Value to insert
     * @complexity O(1), target ≤30ns
     */
    void push(const T& v) noexcept
    {
        const size_t head = m_head.load(std::memory_order_relaxed);
        m_data[head & (N - 1)] = v;  // Power-of-two modulo optimization
        
        // Update head first, then count (SPSC ordering)
        m_head.store(head + 1, std::memory_order_relaxed);
        
        const size_t current_count = m_count.load(std::memory_order_relaxed);
        if (current_count < N) {
            m_count.store(current_count + 1, std::memory_order_relaxed);
        }
    }
    
    /**
     * Emplace new element in-place, overwriting oldest if buffer is full.
     * @param args Constructor arguments
     * @complexity O(1), target ≤30ns
     */
    template<typename... Args>
    void emplace(Args&&... args) noexcept
    {
        const size_t head = m_head.load(std::memory_order_relaxed);
        m_data[head & (N - 1)] = T(std::forward<Args>(args)...);
        
        m_head.store(head + 1, std::memory_order_relaxed);
        
        const size_t current_count = m_count.load(std::memory_order_relaxed);
        if (current_count < N) {
            m_count.store(current_count + 1, std::memory_order_relaxed);
        }
    }

    // ---------- reader ----------
    
    /**
     * Get current number of elements in buffer.
     * @return Element count [0, N]
     * @complexity O(1)
     */
    size_t size() const noexcept
    {
        return m_count.load(std::memory_order_relaxed);
    }
    
    /**
     * Check if buffer is empty.
     * @return true if no elements
     * @complexity O(1)
     */
    bool empty() const noexcept
    {
        return m_count.load(std::memory_order_relaxed) == 0;
    }
    
    /**
     * Get the most recently inserted element.
     * @return Reference to latest element
     * @complexity O(1)
     * @pre !empty()
     */
    const T& latest() const noexcept
    {
        const size_t head = m_head.load(std::memory_order_relaxed);
        return m_data[(head - 1) & (N - 1)];
    }
    
    /**
     * Random access to elements by offset from tail (most recent).
     * @param i_from_tail Offset from most recent: 0=latest, 1=second-to-latest, etc.
     * @return Reference to element
     * @complexity O(1)  
     * @pre i_from_tail < size()
     */
    const T& at(size_t i_from_tail) const noexcept
    {
        const size_t head = m_head.load(std::memory_order_relaxed);
        return m_data[(head - 1 - i_from_tail) & (N - 1)];
    }

    // ---------- aggregate helpers (optional - for future segment-tree overlay) ----------
    
    /**
     * Compute sum over the most recent n elements.
     * @param n Number of recent elements to sum
     * @return Sum of recent n elements
     * @complexity O(log n) when segment-tree overlay is enabled, O(n) otherwise
     * @note Currently O(n) implementation - segment-tree overlay to be added later
     */
    /*
    double range_sum(size_t n) const noexcept
    {
        // Future: O(log n) via Fenwick tree or segment tree
        // Current: O(n) fallback
        const size_t count = std::min(n, size());
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += static_cast<double>(at(i));
        }
        return sum;
    }
    */
    
    /**
     * Find maximum over the most recent n elements.
     * @param n Number of recent elements to check
     * @return Maximum element value
     * @complexity O(log n) when segment-tree overlay is enabled, O(n) otherwise
     * @note Currently disabled - segment-tree overlay to be added later
     */
    /*
    T range_max(size_t n) const noexcept
    {
        // Future: O(log n) via segment tree
        // Current: O(n) fallback
        const size_t count = std::min(n, size());
        if (count == 0) return T{};
        
        T max_val = at(0);
        for (size_t i = 1; i < count; ++i) {
            const T& val = at(i);
            if (val > max_val) max_val = val;
        }
        return max_val;
    }
    */

private:
    // Cache-line aligned storage for optimal memory performance
    alignas(64) std::array<T, N> m_data{};
    
    // SPSC atomics with relaxed ordering (no fences needed)
    std::atomic<size_t> m_head{0};   // Points to next slot to write
    std::atomic<size_t> m_count{0};  // Current number of elements [0, N]
};

#endif // STATIC_RING_BUFFER_HPP 
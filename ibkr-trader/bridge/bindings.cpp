// ═══════════════════════════════════════════════════════════════════════════════
// SIMPLIFIED BINDINGS: Candle Ring + STK_Q Only for now
// bindings.cpp is used to bind the C++ code to Python
// ═══════════════════════════════════════════════════════════════════════════════

#include "bindings.hpp"

PYBIND11_MODULE(pybridge, m) {
    m.doc() = "Simplified Bridge: Candle Ring + STK_Q Only";
    
    // Bind only what we need
    bind_stk_q_data(m);              // STK_Q data structure
    bind_candle(m);                  // Candle data structure
    
    // Bind simplified wrappers
    bind_candle_ring_wrapper(m);     // Candle Ring access
    bind_stk_q_wrapper(m);           // STK_Q access
    
    m.attr("__version__") = "2.0.0-simplified";
} 
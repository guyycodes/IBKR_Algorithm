// #include "bindings.hpp"

// PYBIND11_MODULE(pybridge, m) {
//     m.doc() = "Ultra-Fast Ring Buffer Bridge: C++ ➜ Python";
    
//     // Bind core data structures
//     bind_stockdata(m);
//     bind_temporary_candle(m);
//     bind_candle(m);
//     bind_technical_indicators(m);
    
//     // Bind the ultra-fast ring buffer wrapper
//     bind_ring_buffer_wrapper(m);
    
//     m.attr("__version__") = "1.0.0";
// } 
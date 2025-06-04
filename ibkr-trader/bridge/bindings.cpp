// #include "bindings.hpp"

// PYBIND11_MODULE(pybridge, m) {
//     m.doc() = "Ultra-Fast Bridge: C++ ➜ Python (Ring Buffer + ML Training Data)";
    
//     // Bind core data structures
//     bind_stockdata(m);
//     bind_stk_q_data(m);           // New: STK_Q data for training
//     bind_temporary_candle(m);
//     bind_candle(m);
//     bind_technical_indicators(m);
    
//     // Bind wrappers
//     bind_ring_buffer_wrapper(m);     // Real-time analysis
//     bind_training_data_wrapper(m);   // ML training data
    
//     m.attr("__version__") = "2.0.0";
// } 
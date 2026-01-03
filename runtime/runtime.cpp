#include "runtime.h"
#include <iostream>
#include <cstdlib>

/**
 * Implementation of zero_print function
 * 
 * This function is called from compiled Zero code via LLVM.
 * It provides basic stdout functionality with automatic newline appending.
 */
extern "C" void zero_print(const char* message) {
    // Null pointer safety check
    if (message == nullptr) {
        std::cerr << "[RUNTIME ERROR] Attempted to print null pointer" << std::endl;
        return;
    }
    
    // Output to stdout with automatic newline
    std::cout << message << std::endl;
}

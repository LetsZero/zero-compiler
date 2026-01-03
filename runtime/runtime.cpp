#include "runtime.h"
#include <iostream>
#include <cstdlib>
#include <cstring>

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

/**
 * Get ANSI color code from color name
 * Returns the ANSI escape sequence for the given color name
 */
static const char* get_ansi_color(const char* color) {
    if (color == nullptr) return nullptr;
    
    // Color name to ANSI code mapping
    if (strcmp(color, "red") == 0)     return "\033[31m";
    if (strcmp(color, "green") == 0)   return "\033[32m";
    if (strcmp(color, "yellow") == 0)  return "\033[33m";
    if (strcmp(color, "blue") == 0)    return "\033[34m";
    if (strcmp(color, "magenta") == 0) return "\033[35m";
    if (strcmp(color, "cyan") == 0)    return "\033[36m";
    if (strcmp(color, "white") == 0)   return "\033[37m";
    if (strcmp(color, "reset") == 0)   return "\033[0m";
    
    // Unknown color name
    return nullptr;
}

/**
 * Implementation of zero_log function
 * 
 * Provides enhanced logging with color support using ANSI escape codes.
 * Supports both named colors and direct ANSI code injection.
 */
extern "C" void zero_log(const char* message, const char* color, const char* ansi) {
    // Null pointer safety check for message
    if (message == nullptr) {
        std::cerr << "[RUNTIME ERROR] Attempted to log null pointer" << std::endl;
        return;
    }
    
    const char* color_code = nullptr;
    
    // Priority: direct ANSI code > named color
    if (ansi != nullptr) {
        color_code = ansi;
    } else if (color != nullptr) {
        color_code = get_ansi_color(color);
        if (color_code == nullptr) {
            std::cerr << "[RUNTIME WARNING] Unknown color name: " << color << std::endl;
        }
    }
    
    // Output with color if available
    if (color_code != nullptr) {
        std::cout << color_code << message << "\033[0m" << std::endl;
    } else {
        // No color - just print normally
        std::cout << message << std::endl;
    }
}

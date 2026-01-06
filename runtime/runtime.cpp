#include "runtime.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sstream>

/**
 * Implementation of zero_print function
 * 
 * This function is called from compiled Zero code.
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

// ============================================================================
// ENHANCED PRINT FUNCTIONS (F-String, Trace, Pipe)
// ============================================================================

/**
 * Print with Trace Support
 * 
 * Usage in Zero: print("message", trace=true)
 * When trace=true, outputs: [TRACE] message
 * When trace=false, outputs: message (normal print)
 */
extern "C" void zero_print_traced(const char* message, bool trace) {
    if (message == nullptr) {
        std::cerr << "[RUNTIME ERROR] Attempted to print null pointer" << std::endl;
        return;
    }
    
    if (trace) {
        // Trace mode: add [TRACE] prefix with cyan color
        std::cout << "\033[36m[TRACE]\033[0m " << message << std::endl;
    } else {
        // Normal print
        std::cout << message << std::endl;
    }
}

/**
 * Print Piped Value with Label
 * 
 * Usage in Zero: print(|>func(), msg="result")
 * Outputs: result: <value>
 * 
 * This is for printing the result of a piped expression with an optional label.
 * If label is null/empty, just prints the value.
 */
extern "C" void zero_print_piped(const char* value, const char* label) {
    if (value == nullptr) {
        std::cerr << "[RUNTIME ERROR] Attempted to print null piped value" << std::endl;
        return;
    }
    
    if (label != nullptr && strlen(label) > 0) {
        // Print with label: "label: value"
        std::cout << "\033[33m" << label << ":\033[0m " << value << std::endl;
    } else {
        // No label, just print value
        std::cout << value << std::endl;
    }
}

/**
 * Print with F-String Interpolation Support (Multiple Parts)
 * 
 * This function handles F-string output where parts have been pre-computed
 * by the compiler. It concatenates and prints all parts.
 * 
 * Usage: Called by compiled Zero code with pre-interpolated string parts
 */
extern "C" void zero_print_fstring(const char** parts, int count) {
    if (parts == nullptr || count <= 0) {
        std::cerr << "[RUNTIME ERROR] Invalid f-string parts" << std::endl;
        return;
    }
    
    std::ostringstream oss;
    for (int i = 0; i < count; ++i) {
        if (parts[i] != nullptr) {
            oss << parts[i];
        }
    }
    
    std::cout << oss.str() << std::endl;
}

/**
 * Combined Print with All Features
 * 
 * Usage in Zero:
 *   print("hello")                     -> normal
 *   print(f"val: {x}")                 -> f-string (pre-interpolated)
 *   print("debug", trace=true)         -> traced
 *   print(|>func(), msg="result")      -> piped with label
 * 
 * This is a unified print that handles all modes.
 * Mode: 0=normal, 1=trace, 2=piped
 */
extern "C" void zero_print_ex(const char* message, int mode, const char* extra) {
    if (message == nullptr) {
        std::cerr << "[RUNTIME ERROR] Attempted to print null pointer" << std::endl;
        return;
    }
    
    switch (mode) {
        case 0: // Normal print
            std::cout << message << std::endl;
            break;
            
        case 1: // Trace mode
            std::cout << "\033[36m[TRACE]\033[0m " << message << std::endl;
            break;
            
        case 2: // Piped mode (extra = label)
            if (extra != nullptr && strlen(extra) > 0) {
                std::cout << "\033[33m" << extra << ":\033[0m " << message << std::endl;
            } else {
                std::cout << message << std::endl;
            }
            break;
            
        default:
            std::cout << message << std::endl;
    }
}

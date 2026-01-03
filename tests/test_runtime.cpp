#include "runtime.h"

int main() {
    // Test basic print
    zero_print("=== Testing Print ===");
    zero_print("Hello from Zero runtime!");
    zero_print("");
    
    // Test log with named colors
    zero_print("=== Testing Log with Named Colors ===");
    zero_log("Success message", "green", nullptr);
    zero_log("Warning message", "yellow", nullptr);
    zero_log("Error message", "red", nullptr);
    zero_log("Info message", "blue", nullptr);
    zero_log("Debug message", "cyan", nullptr);
    zero_log("Important message", "magenta", nullptr);
    zero_print("");
    
    // Test log without color
    zero_print("=== Testing Log without Color ===");
    zero_log("Plain message", nullptr, nullptr);
    zero_print("");
    
    // Test log with direct ANSI codes
    zero_print("=== Testing Log with Direct ANSI ===");
    zero_log("Bold red text", nullptr, "\033[1;31m");
    zero_log("Underlined green text", nullptr, "\033[4;32m");
    zero_print("");
    
    // Test invalid color name
    zero_print("=== Testing Invalid Color ===");
    zero_log("Unknown color", "purple", nullptr);
    zero_print("");
    
    // Test null safety
    zero_print("=== Testing Null Safety ===");
    zero_log(nullptr, "red", nullptr);
    
    zero_print("");
    zero_print("=== All Tests Complete ===");
    
    return 0;
}
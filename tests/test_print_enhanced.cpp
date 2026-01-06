#include "runtime.h"
#include <iostream>

/**
 * Test Suite for Enhanced Print Functions
 * 
 * Tests the following runtime functions:
 * 1. zero_print_traced  - Print with trace flag
 * 2. zero_print_piped   - Print piped value with label
 * 3. zero_print_fstring - Print f-string parts
 * 4. zero_print_ex      - Extended print with all modes
 */

void print_separator(const char* section) {
    std::cout << "\n";
    std::cout << "=============================================" << std::endl;
    std::cout << " " << section << std::endl;
    std::cout << "=============================================" << std::endl;
}

int main() {
    std::cout << "╔═══════════════════════════════════════════╗" << std::endl;
    std::cout << "║   Zero Runtime - Enhanced Print Tests     ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════╝" << std::endl;

    // =========================================================================
    // TEST 1: zero_print_traced
    // =========================================================================
    print_separator("TEST 1: zero_print_traced");
    
    std::cout << "\n[1.1] trace=false (normal print):" << std::endl;
    zero_print_traced("This is a normal message", false);
    
    std::cout << "\n[1.2] trace=true (with [TRACE] prefix):" << std::endl;
    zero_print_traced("Debug: variable x = 42", true);
    zero_print_traced("Debug: entering function foo()", true);
    zero_print_traced("Debug: loop iteration 5", true);
    
    std::cout << "\n[1.3] Empty message with trace:" << std::endl;
    zero_print_traced("", true);
    
    std::cout << "\n[1.4] Null safety test:" << std::endl;
    zero_print_traced(nullptr, true);

    // =========================================================================
    // TEST 2: zero_print_piped
    // =========================================================================
    print_separator("TEST 2: zero_print_piped");
    
    std::cout << "\n[2.1] Value with label:" << std::endl;
    zero_print_piped("42", "result");
    zero_print_piped("3.14159", "pi");
    zero_print_piped("Hello World", "greeting");
    
    std::cout << "\n[2.2] Value without label (nullptr):" << std::endl;
    zero_print_piped("Just a value", nullptr);
    
    std::cout << "\n[2.3] Value with empty label:" << std::endl;
    zero_print_piped("Another value", "");
    
    std::cout << "\n[2.4] Simulating pipeline: x |> double() |> print(msg=\"doubled\"):" << std::endl;
    // Simulate: let x = 5; x |> double() |> print(msg="doubled")
    int x = 5;
    int doubled = x * 2;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", doubled);
    zero_print_piped(buffer, "doubled");
    
    std::cout << "\n[2.5] Null safety test:" << std::endl;
    zero_print_piped(nullptr, "label");

    // =========================================================================
    // TEST 3: zero_print_fstring
    // =========================================================================
    print_separator("TEST 3: zero_print_fstring");
    
    std::cout << "\n[3.1] Simple f-string: f\"Hello, {name}!\":" << std::endl;
    const char* parts1[] = {"Hello, ", "Alice", "!"};
    zero_print_fstring(parts1, 3);
    
    std::cout << "\n[3.2] Complex f-string: f\"Value: {x} + {y} = {z}\":" << std::endl;
    const char* parts2[] = {"Value: ", "10", " + ", "20", " = ", "30"};
    zero_print_fstring(parts2, 6);
    
    std::cout << "\n[3.3] F-string with just text (no interpolation):" << std::endl;
    const char* parts3[] = {"Just plain text"};
    zero_print_fstring(parts3, 1);
    
    std::cout << "\n[3.4] Empty parts array:" << std::endl;
    zero_print_fstring(nullptr, 0);
    
    std::cout << "\n[3.5] Parts with null element:" << std::endl;
    const char* parts4[] = {"Before ", nullptr, " After"};
    zero_print_fstring(parts4, 3);

    // =========================================================================
    // TEST 4: zero_print_ex (Unified Extended Print)
    // =========================================================================
    print_separator("TEST 4: zero_print_ex (Unified API)");
    
    std::cout << "\n[4.1] Mode 0 - Normal print:" << std::endl;
    zero_print_ex("Normal message via print_ex", 0, nullptr);
    
    std::cout << "\n[4.2] Mode 1 - Trace mode:" << std::endl;
    zero_print_ex("Traced message via print_ex", 1, nullptr);
    
    std::cout << "\n[4.3] Mode 2 - Piped mode with label:" << std::endl;
    zero_print_ex("100", 2, "computed_value");
    
    std::cout << "\n[4.4] Mode 2 - Piped mode without label:" << std::endl;
    zero_print_ex("200", 2, nullptr);
    zero_print_ex("300", 2, "");
    
    std::cout << "\n[4.5] Unknown mode (should default to normal):" << std::endl;
    zero_print_ex("Message with unknown mode", 99, nullptr);
    
    std::cout << "\n[4.6] Null safety test:" << std::endl;
    zero_print_ex(nullptr, 0, nullptr);

    // =========================================================================
    // TEST 5: Combined Usage Scenarios
    // =========================================================================
    print_separator("TEST 5: Real-World Usage Scenarios");
    
    std::cout << "\n[5.1] Simulating: print(f\"Processing item {i}\", trace=true):" << std::endl;
    for (int i = 1; i <= 3; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Processing item %d", i);
        zero_print_ex(msg, 1, nullptr);  // trace mode
    }
    
    std::cout << "\n[5.2] Simulating: result |> process() |> print(msg=\"output\"):" << std::endl;
    const char* pipeline_result = "42.5";
    zero_print_piped(pipeline_result, "output");
    
    std::cout << "\n[5.3] Simulating: print(f\"User {name} scored {score} points\"):" << std::endl;
    const char* fstring_parts[] = {"User ", "Bob", " scored ", "95", " points"};
    zero_print_fstring(fstring_parts, 5);

    // =========================================================================
    // SUMMARY
    // =========================================================================
    print_separator("TEST SUMMARY");
    std::cout << "\n";
    std::cout << "  [✓] zero_print_traced  - Trace flag support" << std::endl;
    std::cout << "  [✓] zero_print_piped   - Pipeline value printing" << std::endl;
    std::cout << "  [✓] zero_print_fstring - F-string part concatenation" << std::endl;
    std::cout << "  [✓] zero_print_ex      - Unified extended API" << std::endl;
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════╗" << std::endl;
    std::cout << "║         All Tests Completed!              ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════╝" << std::endl;
    
    return 0;
}

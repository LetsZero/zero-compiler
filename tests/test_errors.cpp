#include "diagnostics/reporter.hpp"
#include <iostream>

using namespace zero::diagnostics;

int main() {
    std::cout << "=== Testing Error Reporter ===" << std::endl;
    std::cout << std::endl;
    
    // Test 1: Syntax Error
    std::cout << "Test 1: Syntax Error" << std::endl;
    Reporter::reportError(
        ErrorType::SYNTAX,
        SourceLocation("examples/hello_world.zero", 5, 8),
        "Identifier 'statux' not found in this scope.",
        "Did you mean 'status'?"
    );
    
    // Test 2: Type Error
    std::cout << "Test 2: Type Error" << std::endl;
    Reporter::reportError(
        ErrorType::TYPE,
        SourceLocation("examples/hello_world.zero", 3, 10),
        "Cannot add 'string' and 'int'",
        "Convert one operand to match the other's type"
    );
    
    // Test 3: Runtime Error with trace
    std::cout << "Test 3: Runtime Error with Trace" << std::endl;
    std::vector<std::string> trace = {
        "at main() in examples/hello_world.zero:4",
        "at print() in stdlib/display.zero:10"
    };
    Reporter::reportError(
        ErrorType::RUNTIME,
        SourceLocation("examples/hello_world.zero", 4, 4),
        "Division by zero",
        "Ensure divisor is not zero before division",
        trace
    );
    
    // Test 4: Lexical Error
    std::cout << "Test 4: Lexical Error" << std::endl;
    Reporter::reportError(
        ErrorType::LEXICAL,
        SourceLocation("examples/hello_world.zero", 2, 0),
        "Unexpected character '@'",
        "Remove or escape the invalid character"
    );
    
    std::cout << "=== All Error Reporter Tests Complete ===" << std::endl;
    
    return 0;
}

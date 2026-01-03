#include "diagnostics/reporter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace zero {
namespace diagnostics {

std::string Reporter::getErrorTypeName(ErrorType type) {
    switch (type) {
        case ErrorType::LEXICAL:  return "LexicalError";
        case ErrorType::SYNTAX:   return "SyntaxError";
        case ErrorType::TYPE:     return "TypeError";
        case ErrorType::RUNTIME:  return "RuntimeError";
        default:                  return "Error";
    }
}

std::vector<std::string> Reporter::readSourceFile(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        return lines; // Return empty vector if file can't be opened
    }
    
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    
    file.close();
    return lines;
}

std::string Reporter::generatePointer(int column, int length) {
    std::string pointer;
    
    // Add spaces before the pointer
    for (int i = 0; i < column; ++i) {
        pointer += " ";
    }
    
    // Add the pointer characters
    for (int i = 0; i < length; ++i) {
        pointer += "^";
    }
    
    return pointer;
}

std::string Reporter::formatLineNumber(int lineNum, int maxWidth) {
    std::ostringstream oss;
    oss << std::setw(maxWidth) << lineNum;
    return oss.str();
}

void Reporter::reportError(
    ErrorType type,
    const SourceLocation& location,
    const std::string& message,
    const std::string& help,
    const std::vector<std::string>& trace
) {
    // ANSI color codes
    const char* RED = "\033[31m";
    const char* YELLOW = "\033[33m";
    const char* CYAN = "\033[36m";
    const char* RESET = "\033[0m";
    const char* BOLD = "\033[1m";
    
    // Print error header
    std::cerr << RED << BOLD << "[ ERROR ] " << RESET
              << RED << getErrorTypeName(type) << RESET
              << " in '" << location.filename << "'\n";
    
    std::cerr << "  " << CYAN << "-->" << RESET
              << " Line " << location.line
              << ", Col " << location.column << "\n\n";
    
    // Read source file
    std::vector<std::string> sourceLines = readSourceFile(location.filename);
    
    if (!sourceLines.empty() && location.line > 0 && location.line <= static_cast<int>(sourceLines.size())) {
        int errorLine = location.line - 1; // Convert to 0-indexed
        int maxLineNumWidth = std::to_string(location.line).length();
        
        // Print previous line for context (if available)
        if (errorLine > 0) {
            std::cerr << "   " << formatLineNumber(errorLine, maxLineNumWidth)
                      << " | " << sourceLines[errorLine - 1] << "\n";
        }
        
        // Print error line
        std::cerr << "   " << formatLineNumber(location.line, maxLineNumWidth)
                  << " | " << RED << sourceLines[errorLine] << RESET << "\n";
        
        // Print pointer
        std::string padding(maxLineNumWidth + 5, ' '); // "   " + lineNum + " | "
        std::cerr << padding << RED << generatePointer(location.column, 1) << RESET << "\n";
        
        // Print focus message
        std::cerr << padding << YELLOW << "[ Focus ]: " << RESET << message << "\n";
        
        // Print help message if provided
        if (!help.empty()) {
            std::cerr << padding << CYAN << "[ Help ]: " << RESET << help << "\n";
        }
    } else {
        // File couldn't be read or line is out of range
        std::cerr << "   " << YELLOW << "[ Focus ]: " << RESET << message << "\n";
        if (!help.empty()) {
            std::cerr << "   " << CYAN << "[ Help ]: " << RESET << help << "\n";
        }
    }
    
    // Print trace if provided
    if (!trace.empty()) {
        std::cerr << "\n  (Trace):\n";
        for (const auto& frame : trace) {
            std::cerr << "    " << frame << "\n";
        }
    }
    
    std::cerr << "\n";
}

} // namespace diagnostics
} // namespace zero

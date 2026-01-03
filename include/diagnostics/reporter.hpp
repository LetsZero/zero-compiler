#ifndef ZERO_DIAGNOSTICS_REPORTER_HPP
#define ZERO_DIAGNOSTICS_REPORTER_HPP

#include <string>
#include <vector>

namespace zero {
namespace diagnostics {

/**
 * Error severity levels
 */
enum class ErrorType {
    LEXICAL,    // Lexer/tokenization errors
    SYNTAX,     // Parser/syntax errors
    TYPE,       // Type checking errors
    RUNTIME     // Runtime errors
};

/**
 * Source location information
 */
struct SourceLocation {
    std::string filename;
    int line;
    int column;
    
    SourceLocation(const std::string& file, int ln, int col)
        : filename(file), line(ln), column(col) {}
};

/**
 * Diagnostic reporter using "Frame & Focus" format
 * 
 * Format:
 * [ ERROR ] <ErrorType> in '<file_name>'
 *   --> Line <num>, Col <num>
 * 
 *    <line_prev> |  <code_context>
 *    <line_err>  |  <offending_code>
 *                |  ^^^^
 *                |  [ Focus ]: <specific_reason>
 *                |  [ Help ]: <suggested_fix>
 */
class Reporter {
public:
    /**
     * Report an error with Frame & Focus format
     * 
     * @param type Error type (LEXICAL, SYNTAX, TYPE, RUNTIME)
     * @param location Source location of the error
     * @param message Main error message
     * @param help Optional help/suggestion message
     * @param trace Optional call stack trace
     */
    static void reportError(
        ErrorType type,
        const SourceLocation& location,
        const std::string& message,
        const std::string& help = "",
        const std::vector<std::string>& trace = {}
    );
    
private:
    /**
     * Get error type name as string
     */
    static std::string getErrorTypeName(ErrorType type);
    
    /**
     * Read lines from source file
     * Returns empty vector if file cannot be read
     */
    static std::vector<std::string> readSourceFile(const std::string& filename);
    
    /**
     * Generate visual pointer (^^^) for error location
     */
    static std::string generatePointer(int column, int length = 1);
    
    /**
     * Format line number with padding
     */
    static std::string formatLineNumber(int lineNum, int maxWidth = 4);
};

} // namespace diagnostics
} // namespace zero

#endif // ZERO_DIAGNOSTICS_REPORTER_HPP

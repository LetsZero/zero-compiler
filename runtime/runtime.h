#ifndef ZERO_RUNTIME_H
#define ZERO_RUNTIME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Basic print function for Zero language
 * @param message The string to print to stdout
 * 
 * Outputs the message followed by a newline character.
 * This function provides no color support - plain text only.
 */
void zero_print(const char* message);

/**
 * @brief Enhanced logging function with color support
 * @param message The string to print to stdout
 * @param color Named color (e.g., "red", "green") or NULL for no color
 * @param ansi Direct ANSI escape code or NULL
 * 
 * Outputs the message with optional color formatting.
 * If both color and ansi are provided, ansi takes precedence.
 */
void zero_log(const char* message, const char* color, const char* ansi);

/**
 * @brief Print with trace flag
 * @param message The message to print
 * @param trace If true, adds [TRACE] prefix
 * 
 * Usage: print("msg", trace=true)
 */
void zero_print_traced(const char* message, bool trace);

/**
 * @brief Print piped value with optional label
 * @param value The value to print (result of piped expression)
 * @param label Optional label to prefix the output
 * 
 * Usage: print(|>func(), msg="label")
 */
void zero_print_piped(const char* value, const char* label);

/**
 * @brief Print f-string with multiple parts
 * @param parts Array of string parts
 * @param count Number of parts
 */
void zero_print_fstring(const char** parts, int count);

/**
 * @brief Extended print with all features
 * @param message The message to print
 * @param mode 0=normal, 1=trace, 2=piped
 * @param extra Extra data (label for piped mode)
 */
void zero_print_ex(const char* message, int mode, const char* extra);

#ifdef __cplusplus
}
#endif

#endif // ZERO_RUNTIME_H

#ifndef ZERO_RUNTIME_H
#define ZERO_RUNTIME_H

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

#ifdef __cplusplus
}
#endif

#endif // ZERO_RUNTIME_H

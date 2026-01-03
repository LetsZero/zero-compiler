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

#ifdef __cplusplus
}
#endif

#endif // ZERO_RUNTIME_H

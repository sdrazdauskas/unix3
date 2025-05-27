// utils.h - Utility functions for string manipulation and more
#ifndef UTILS_H
#define UTILS_H

// Trims leading and trailing whitespace in-place
void trim_whitespace(char *str);

// Case-insensitive string search
char *strcasestr(const char *haystack, const char *needle);

// Log message with variable arguments
void log_message(const char *fmt, ...);

#endif // UTILS_H

#pragma once
#include <iostream>
#include <string>

// ANSI Escape Codes for colors (inline: one instance program-wide, no
// per-TU copies from this widely-included header).
inline const std::string RED = "\033[31m";    // Red text
inline const std::string GREEN = "\033[32m";  // Green text
inline const std::string YELLOW = "\033[33m"; // Yellow text
inline const std::string BLUE = "\033[34m";   // Blue text
inline const std::string CYAN = "\033[36m";   // Cyan text
inline const std::string RESET = "\033[0m";   // Reset to default color
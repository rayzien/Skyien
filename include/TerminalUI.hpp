#pragma once

#include <string>
#include <iostream>

namespace Skyien {
namespace UI {

// ANSI Colors
inline constexpr const char* RESET       = "\033[0m";
inline constexpr const char* BOLD        = "\033[1m";
inline constexpr const char* DIM         = "\033[2m";
inline constexpr const char* RED         = "\033[31m";
inline constexpr const char* GREEN       = "\033[32m";
inline constexpr const char* YELLOW      = "\033[33m";
inline constexpr const char* BLUE        = "\033[34m";
inline constexpr const char* MAGENTA     = "\033[35m";
inline constexpr const char* CYAN        = "\033[36m";
inline constexpr const char* WHITE       = "\033[37m";
inline constexpr const char* BG_BLUE     = "\033[44m";
inline constexpr const char* BG_CYAN     = "\033[46m";

void printBanner();
void printSuccess(const std::string& msg);
void printInfo(const std::string& msg);
void printWarning(const std::string& msg);
void printError(const std::string& msg);
void printAutomation(const std::string& rule, const std::string& action);

std::string prompt(const std::string& message, bool isSecret = false);
std::string formatDuration(int64_t seconds);

} // namespace UI
} // namespace Skyien

#include "TerminalUI.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <termios.h>
#include <unistd.h>

namespace Skyien {
namespace UI {

void printBanner() {
    std::cout << CYAN << BOLD
              << "  ███████╗██╗  ██╗██╗   ██╗██╗███████╗███╗   ██╗\n"
              << "  ██╔════╝██║ ██╔╝╚██╗ ██╔╝██║██╔════╝████╗  ██║\n"
              << "  ███████╗█████═╝  ╚████╔╝ ██║█████╗  ██╔██╗ ██║\n"
              << "  ╚════██║██╔═██╗   ╚██╔╝  ██║██╔══╝  ██║╚██╗██║\n"
              << "  ███████║██║ ╚██╗   ██║   ██║███████╗██║ ╚████║\n"
              << "  ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═╝╚══════╝╚═╝  ╚═══╝\n"
              << RESET
              << DIM << "    Ultra-Fast C++ Telegram Account Automation CLI\n"
              << "    Powered by TDLib & Clang++ | Termux\n\n"
              << RESET;
}

void printSuccess(const std::string& msg) {
    std::cout << GREEN << BOLD << "[+] " << RESET << msg << "\n";
}

void printInfo(const std::string& msg) {
    std::cout << CYAN << "[*] " << RESET << msg << "\n";
}

void printWarning(const std::string& msg) {
    std::cout << YELLOW << BOLD << "[!] " << RESET << msg << "\n";
}

void printError(const std::string& msg) {
    std::cerr << RED << BOLD << "[-] " << RESET << msg << "\n";
}

void printAutomation(const std::string& rule, const std::string& action) {
    std::cout << MAGENTA << BOLD << "[AUTO] " << RESET 
              << YELLOW << rule << RESET << " -> " << GREEN << action << RESET << "\n";
}

std::string prompt(const std::string& message, bool isSecret) {
    std::cout << BOLD << message << ": " << RESET << std::flush;
    std::string input;
    if (isSecret) {
        termios oldt;
        tcgetattr(STDIN_FILENO, &oldt);
        termios newt = oldt;
        newt.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        std::getline(std::cin, input);
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cout << "\n";
    } else {
        std::getline(std::cin, input);
    }
    return input;
}

std::string formatDuration(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    int64_t days = seconds / 86400;
    int64_t rem = seconds % 86400;
    int64_t hours = rem / 3600;
    rem = rem % 3600;
    int64_t minutes = rem / 60;
    int64_t secs = rem % 60;

    std::ostringstream oss;
    if (days > 0) {
        oss << days << "d " << std::setw(2) << std::setfill('0') << hours << "h "
            << std::setw(2) << std::setfill('0') << minutes << "m "
            << std::setw(2) << std::setfill('0') << secs << "s";
    } else if (hours > 0) {
        oss << std::setw(2) << std::setfill('0') << hours << "h "
            << std::setw(2) << std::setfill('0') << minutes << "m "
            << std::setw(2) << std::setfill('0') << secs << "s";
    } else {
        oss << std::setw(2) << std::setfill('0') << minutes << "m "
            << std::setw(2) << std::setfill('0') << secs << "s";
    }
    return oss.str();
}

} // namespace UI
} // namespace Skyien

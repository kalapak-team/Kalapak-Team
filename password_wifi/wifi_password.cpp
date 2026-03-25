#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <windows.h>

// Run a command and capture its output
std::string runCommand(const std::string& cmd) {
    std::string result;
    char buffer[256];
    // Use _popen to capture stdout
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    _pclose(pipe);
    return result;
}

// Trim whitespace from both ends of a string
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// Extract all saved WiFi profile names
std::vector<std::string> getProfiles() {
    std::vector<std::string> profiles;
    std::string output = runCommand("netsh wlan show profiles");

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        // Line format (English):  "    All User Profile     : ProfileName"
        // Line format (Thai/other locales may differ, but colon separator remains)
        size_t colonPos = line.rfind(':');
        if (colonPos == std::string::npos) continue;

        // Check if the line contains "Profile" (works for most Windows locales)
        std::string left = line.substr(0, colonPos);
        if (left.find("Profile") == std::string::npos &&
            left.find("profile") == std::string::npos) continue;

        std::string name = trim(line.substr(colonPos + 1));
        if (!name.empty()) {
            profiles.push_back(name);
        }
    }
    return profiles;
}

// Extract the WiFi password (Key Content) for a given profile
std::string getPassword(const std::string& profileName) {
    // Escape double quotes in profile name for safety
    std::string safeName;
    for (char c : profileName) {
        if (c == '"') safeName += "\\\"";  // escape embedded quotes
        else          safeName += c;
    }

    std::string cmd = "netsh wlan show profile name=\"" + safeName + "\" key=clear";
    std::string output = runCommand(cmd);

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        // Look for "Key Content" line
        if (line.find("Key Content") != std::string::npos ||
            line.find("key content") != std::string::npos ||
            line.find("Key content") != std::string::npos) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                return trim(line.substr(colonPos + 1));
            }
        }
    }
    return "(no password / open network)";
}

int main() {
    // Set console code page to UTF-8 for better character support
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "============================================\n";
    std::cout << "   Saved WiFi Passwords on This Computer   \n";
    std::cout << "============================================\n\n";

    std::vector<std::string> profiles = getProfiles();

    if (profiles.empty()) {
        std::cout << "No saved WiFi profiles found.\n";
        std::cout << "(Try running as Administrator)\n";
    } else {
        std::cout << "Found " << profiles.size() << " saved profile(s):\n\n";

        // Print header
        std::cout << std::left;
        std::cout.width(35); std::cout << "WiFi Name (SSID)";
        std::cout << "Password\n";
        std::cout << std::string(35, '-') << std::string(35, '-') << "\n";

        for (const std::string& name : profiles) {
            std::string password = getPassword(name);

            // Print row
            std::string displayName = name;
            if (displayName.size() > 33) displayName = displayName.substr(0, 30) + "...";

            std::cout.width(35);
            std::cout << std::left << displayName;
            std::cout << password << "\n";
        }
    }

    std::cout << "\n============================================\n";
    std::cout << "Press Enter to exit...";
    std::cin.get();
    return 0;
}

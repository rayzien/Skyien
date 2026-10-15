#pragma once

#include <string>
#include <cstdint>
#include <json/json.h>

namespace Skyien {

class BotBridge {
public:
    BotBridge() = default;

    // Verify a bot token and retrieve bot details
    static bool verifyToken(const std::string& token, std::string& outUsername, int64_t& outId);

    // Send a message using the Telegram Bot API
    static bool getUpdates(const std::string& token, int64_t offset, Json::Value& outUpdates);
    static bool sendMessage(const std::string& token, int64_t chatId, const std::string& text);

    // Helper to send HTTP GET/POST via libcurl
    static bool httpGet(const std::string& url, std::string& response);
    static bool httpPostJson(const std::string& url, const std::string& jsonBody, std::string& response);
};

} // namespace Skyien

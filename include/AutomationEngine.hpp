#pragma once

#include "Config.hpp"
#include "TelegramClient.hpp"
#include "BotBridge.hpp"
#include "UserCommandHandler.hpp"
#include <atomic>
#include <chrono>
#include <unordered_set>

namespace Skyien {

struct AutomationStats {
    uint64_t messagesReceived{0};
    uint64_t repliesSent{0};
    uint64_t forwardsCompleted{0};
    uint64_t relaysSent{0};
    std::chrono::system_clock::time_point startTime;
};

class AutomationEngine {
public:
    AutomationEngine(TelegramClient& client, const Config& config);

    void start();
    void stop();
    bool isRunning() const;

    void onUpdate(const Json::Value& update);

    const AutomationStats& getStats() const;
    void printStatus() const;

    void reloadConfig(const Config& config);

    // Relay helpers
    void broadcast(const std::string& message);

private:
    void botPollingTask();
    std::thread m_botThread;
    void executeBroadcast(const std::string& message);
    void handleBotCommand(int64_t chatId, const std::string& cmd);

    void handleNewMessage(const Json::Value& message);
    bool checkAutoReply(int64_t chatId, int64_t senderId, const std::string& text, int64_t messageId);
    void checkForward(int64_t chatId, int64_t messageId);
    void checkRelay(int64_t chatId, int64_t senderId, const std::string& text);

    TelegramClient& m_client;
    Config m_config;
    std::atomic<bool> m_active{false};
    AutomationStats m_stats;

    std::unordered_set<int64_t> m_processedMessages;
    std::unordered_set<int64_t> m_afkNotifiedUsers;
    std::mutex m_mutex;
    int64_t m_myUserId{0};
    std::string m_myUsername;
    UserCommandHandler m_cmdHandler;
};

} // namespace Skyien

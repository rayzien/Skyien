#pragma once

#include "Config.hpp"
#include <json/json.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <future>

namespace Skyien {

struct UserProfile {
    int64_t id{0};
    std::string firstName;
    std::string lastName;
    std::string username;
    std::string phoneNumber;
};

struct ChatInfo {
    int64_t id{0};
    std::string title;
    int32_t unreadCount{0};
    std::string type;
};

class TelegramClient {
public:
    using UpdateHandler = std::function<void(const Json::Value&)>;

    TelegramClient(const Config& config);
    ~TelegramClient();

    bool start();
    void stop();

    // Authentication (accepts optional phone number to log in directly)
    bool authenticate(const std::string& explicitPhoneNumber = "");
    bool isAuthenticated() const;

    // Core Request API
    void sendAsync(const Json::Value& request);
    Json::Value sendSync(Json::Value request, double timeoutSec = 10.0);

    // High level Telegram API
    UserProfile getMe();
    int64_t getMyUserId();
    std::vector<ChatInfo> getChats(int limit = 25);
    bool sendMessage(int64_t chatId, const std::string& text);
    bool viewMessages(int64_t chatId, const std::vector<int64_t>& messageIds);
    bool joinChat(int64_t chatId);
    bool isChatJoined(int64_t chatId);

    // Numbered interactive chat selection helper
    int64_t selectChatInteractive(const std::string& promptText = "Select chat by number or enter Chat ID");

    // Event listeners
    void setUpdateHandler(UpdateHandler handler);

private:
    void workerLoop();
    void handleIncoming(const Json::Value& update);

    Config m_config;
    void* m_tdClient{nullptr};
    std::thread m_workerThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_authenticated{false};

    std::atomic<uint64_t> m_nextExtraId{1};
    std::mutex m_promisesMutex;
    std::unordered_map<uint64_t, std::promise<Json::Value>> m_pendingPromises;

    std::string m_currentAuthState;
    std::mutex m_authMutex;
    std::condition_variable m_authCv;

    UpdateHandler m_updateHandler;
    int64_t m_myUserId{0};

    // Chat cache & order
    std::mutex m_chatsMutex;
    std::vector<int64_t> m_chatOrder;
    std::unordered_map<int64_t, ChatInfo> m_chatsCache;
};

} // namespace Skyien

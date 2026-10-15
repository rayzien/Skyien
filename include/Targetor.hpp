#pragma once

#include "Config.hpp"
#include "TelegramClient.hpp"
#include <string>
#include <vector>
#include <deque>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <chrono>

namespace Skyien {

struct TargetCandidate {
    int64_t userId{0};
    int64_t fromChatId{0};
    std::string matchedKeyword;
    std::string userFirstName;
    std::chrono::system_clock::time_point detectedAt;
};

class Targetor {
public:
    Targetor(TelegramClient& client, const Config& config);

    bool loadConfig(const std::string& path = "");
    bool saveConfig(const std::string& path = "") const;

    void start();
    void stop();
    bool isRunning() const;

    void onUpdate(const Json::Value& update);

    // Interactive Terminal Configurator
    void runInteractiveConfig();

    // Direct command execution
    void addMonitoredChannel(int64_t chatId);
    void addKeyword(const std::string& keyword);
    void printStatus() const;

    // Config getters/setters
    int64_t getMinGapSeconds() const { return m_minGapSeconds; }
    int64_t getMaxGapSeconds() const { return m_maxGapSeconds; }
    void setGaps(int64_t minSec, int64_t maxSec);
    void setCustomTemplate(const std::string& text);

private:
    void loopWorker();
    void processCandidate(const TargetCandidate& cand);
    int64_t computeNextRandomGap();
    static std::string formatDuration(int64_t seconds);

    TelegramClient& m_client;
    Config m_baseConfig;
    std::string m_configPath;

    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
    std::mutex m_mutex;

    // Monitored channels and conditions
    std::vector<int64_t> m_monitoredChannels;
    std::vector<std::string> m_keywords;
    std::string m_customMessageTemplate;

    // Interval settings (default 2 hours to 4 days)
    int64_t m_minGapSeconds{7200};      // 2 hours
    int64_t m_maxGapSeconds{345600};    // 4 days (4 * 24 * 3600)
    int64_t m_currentBaseGap{7200};     // starts at 2 hours
    int64_t m_escalationStep{3600};     // +1 hour step per sent DM
    double m_escalationMultiplier{1.25};// multiplier factor

    int64_t m_nextScheduledDispatchTime{0};
    int32_t m_dailyLimit{10};
    int32_t m_dmsSentToday{0};
    std::chrono::system_clock::time_point m_lastSentTime;

    std::unordered_set<int64_t> m_contactedUserIds;
    std::deque<TargetCandidate> m_pendingQueue;
    int64_t m_myUserId{0};
};

} // namespace Skyien

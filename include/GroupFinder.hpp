#pragma once

#include "TelegramClient.hpp"
#include <json/json.h>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Skyien {

enum class PacingMode {
    MIN_10_MINUTES = 1,   // Minimum 10 minutes (600s)
    RANDOM_4M_TO_1H = 2,  // Random delay between 4 minutes (240s) and 1 hour (3600s)
    CUSTOM_FIXED = 3,     // Custom fixed delay in seconds
    CUSTOM_RANDOM = 4     // Custom random delay between customMinSec and customMaxSec
};

struct PacingConfig {
    PacingMode mode{PacingMode::MIN_10_MINUTES};
    int32_t customFixedSec{600};
    int32_t customMinSec{240};
    int32_t customMaxSec{3600};

    int32_t getNextDelaySeconds() const;
    std::string getModeDescription() const;
    Json::Value toJson() const;
    static PacingConfig fromJson(const Json::Value& val);
    void configureInteractive();
};

struct GroupFilterCriteria {
    std::string query;
    int32_t minMembers{0};       // Minimum members (default 0)
    int32_t maxMembers{0};       // Maximum members (e.g. 10000 for "10k" aim: 0 to 10k)
    int32_t targetOnline{0};     // e.g. 1000 for "1k"
    bool requireCanSend{false};
    bool includeChannels{true};  // Search both groups and channels
    bool includeGroups{true};
    bool excludeJoined{true};    // Default true: never show already joined ones
};

struct GroupSearchResult {
    int64_t chatId{0};
    std::string title;
    std::string username;
    int32_t memberCount{0};
    int32_t onlineCount{0};
    bool canSendMessages{true};
    bool isSupergroup{true};
    bool isChannel{false};
    std::string typeLabel{"Group"}; // "Channel", "Supergroup", "Group"
    bool alreadyJoined{false};
    double proximityScore{0.0};

    Json::Value toJson() const;
    static GroupSearchResult fromJson(const Json::Value& val);
};

struct JoinedChatRecord {
    int64_t chatId{0};
    std::string title;
    std::string username;
    std::string typeLabel;
    int64_t joinedAt{0};

    Json::Value toJson() const;
    static JoinedChatRecord fromJson(const Json::Value& val);
};

struct JoinQueueState {
    bool inProgress{false};
    std::vector<GroupSearchResult> pending;
    std::vector<JoinedChatRecord> completed;
    int32_t currentIndex{0};

    Json::Value toJson() const;
    static JoinQueueState fromJson(const Json::Value& val);
};

class GroupFinder {
public:
    GroupFinder(TelegramClient& client, const std::string& stateFilePath = "");

    std::vector<GroupSearchResult> search(const GroupFilterCriteria& criteria);
    bool joinAll(const std::vector<GroupSearchResult>& groups);
    bool resumeJoinQueue();

    void runInteractiveFinder();
    static void printResults(const std::vector<GroupSearchResult>& list);
    static int32_t parseQuantity(const std::string& input);
    static bool countdownTimer(int32_t seconds, const std::string& nextTitle);

    PacingConfig& getPacingConfig() { return m_pacing; }
    void setPacingConfig(const PacingConfig& cfg) { m_pacing = cfg; saveState(); }

    const std::unordered_map<int64_t, JoinedChatRecord>& getJoinedChats() const { return m_joinedChats; }
    bool isChatInJoinedCache(int64_t chatId) const;
    void markChatJoined(int64_t chatId, const std::string& title, const std::string& username, const std::string& typeLabel);

    void loadState();
    void saveState();

    const std::string& getStateFilePath() const { return m_stateFilePath; }

private:
    TelegramClient& m_client;
    std::string m_stateFilePath;

    PacingConfig m_pacing;
    std::unordered_map<int64_t, JoinedChatRecord> m_joinedChats;
    std::vector<GroupSearchResult> m_lastResults;
    std::string m_lastQuery;
    int32_t m_lastAimMembers{0};
    JoinQueueState m_queueState;
};

} // namespace Skyien

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Skyien {

inline constexpr int32_t DEFAULT_API_ID = 94575;
inline const std::string DEFAULT_API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2";

struct AutoReplyRule {
    std::string trigger;
    std::string matchType; // "contains", "exact", "starts_with"
    std::string response;
    std::vector<int64_t> allowedChats; // empty = all
};

struct ForwardRule {
    int64_t fromChatId{0};
    int64_t toChatId{0};
};

class Config {
public:
    int32_t apiId{0};
    std::string apiHash;
    std::string phoneNumber;
    std::string sessionDirectory;
    
    // Telegram Bot Bridge
    std::string botToken;
    std::string botUsername;
    int64_t botId{0};

    // Connected Channel & Group
    int64_t connectedChannelId{0};
    std::string connectedChannelTitle;
    int64_t connectedGroupId{0};
    std::string connectedGroupTitle;

    // Relay / Automation settings
    bool autoReplyEnabled{false};
    bool afkMode{false};
    std::string afkMessage{"[Skyien Auto-Responder] I am currently away and will reply shortly."};
    bool autoReadMessages{false};
    bool relayToChannel{false};
    bool relayToGroup{false};
    bool relayToBot{false};
    
    std::vector<AutoReplyRule> autoReplyRules;
    std::vector<ForwardRule> forwardRules;
    // Broadcast Extensions
    std::vector<int64_t> broadcastChannels;
    std::vector<int64_t> exceptionChannels;
    std::string broadcastGapMode{"default"}; // default, random, custom, custom_random
    int32_t broadcastGapMin{10};
    int32_t broadcastGapMax{10};

    // Welcome Feature
    bool welcomeEnabled{false};
    std::string welcomeMessage{"Welcome to the group!"};


    Config();

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;
    static std::string getDefaultConfigPath();
    static std::string getDefaultSessionPath();
    static std::string getDefaultFinderStatePath();
    
    int32_t getEffectiveApiId() const {
        return apiId != 0 ? apiId : DEFAULT_API_ID;
    }
    
    std::string getEffectiveApiHash() const {
        return !apiHash.empty() ? apiHash : DEFAULT_API_HASH;
    }

    bool isValid() const;
    void ensureDirectories() const;
};

} // namespace Skyien

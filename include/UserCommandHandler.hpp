#pragma once

#include "TelegramClient.hpp"
#include "Config.hpp"
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <atomic>
#include <json/json.h>
#include <curl/curl.h>

namespace Skyien {

struct ProxyEntry {
    std::string type;   // "http" or "socks5"
    std::string host;
    int         port = 0;
    std::string user;
    std::string pass;
    int32_t     tdlibProxyId = 0;
};

struct FoundItem {
    int index = 0;
    std::string type; // "link", "document", "video", "audio"
    std::string title;
    std::string url;
    int32_t fileId = 0;
    int64_t messageId = 0;
};

class UserCommandHandler {
public:
    UserCommandHandler(TelegramClient& client, const Config& config);
    ~UserCommandHandler() = default;

    // Call this for every outgoing message YOU send
    // Returns true if the message was consumed as a command or reel download
    bool handleOutgoing(const Json::Value& message);

    // Call this for every incoming message (for sticker hunter and reel link detection)
    void handleIncoming(const Json::Value& message);

    // Accessors
    const std::string& prefix() const { return m_prefix; }
    void setPrefix(const std::string& p);
    bool isRayzEnabled() const { return m_rayzEnabled; }
    const std::string& getRayzMode() const { return m_rayzMode; }
    bool isRayzExceptionChat(int64_t chatId);
    void handleRayzIncoming(int64_t chatId, int64_t msgId, const std::string& text);
    void recordUserOutgoing(int64_t chatId, const std::string& text);

private:
    TelegramClient& m_client;
    Config          m_config;

    // Command prefix (default "/", also accepts ".", "+", and configured prefix)
    std::string     m_prefix = "/";

    // Rayz Auto-Responder
    bool            m_rayzEnabled = false;
    std::string     m_rayzMode = "always"; // "always" or "offline"
    std::string     m_hfToken = "";

    struct RayzChatMessage {
        std::string role;
        std::string text;
    };
    std::map<int64_t, std::vector<RayzChatMessage>> m_rayzChatHistory;
    std::mutex      m_rayzHistoryMutex;
    std::set<int64_t> m_rayzExceptionChats;
    std::mutex      m_rayzExceptionMutex;

    void loadChatHistory();
    void saveChatHistory();

    // Sticker Hunter
    bool            m_stickerHunter   = false;
    bool            m_stickerMuted    = true; // Muted by default to prevent spam
    int             m_stickersAddedCount = 0;
    int             m_stickersIgnoredCount = 0;
    std::string     m_stickerPackName = "rayzien's collection";
    std::set<std::string> m_seenStickers;
    std::mutex      m_stickerMutex;

    // Proxy
    bool                    m_proxyEnabled = false;
    std::string             m_proxyMode    = "http";
    std::vector<ProxyEntry> m_proxyList;
    int                     m_activeProxy  = -1;
    std::mutex              m_proxyMutex;

    // Helpers
    void reply(int64_t chatId, const std::string& text);
    int64_t replySync(int64_t chatId, const std::string& text);
    void replyAndPurge(int64_t chatId, const std::string& text, int delaySeconds = 5);
    void editMessage(int64_t chatId, int64_t messageId, const std::string& text);
    void deleteMsg(int64_t chatId, int64_t msgId);
    void loadState();
    void saveState();

    // Command parsing
    bool matchPrefix(const std::string& text, std::string& cmd, std::string& args);

    // Commands
    void cmdHelp(int64_t chatId);
    void cmdSettings(int64_t chatId);
    void cmdRayz(int64_t chatId, int64_t cmdMsgId, const std::string& args);
    void cmdHeyRayz(int64_t chatId, int64_t msgId, const std::string& prompt);
    void cmdRayimage(int64_t chatId, int64_t cmdMsgId, const std::string& prompt, bool showStatus = false);
    void cmdSticker(int64_t chatId, const std::string& args);
    void cmdAddSticker(int64_t chatId, int64_t msgId, const Json::Value& fullMsg);
    void cmdProxy(int64_t chatId, const std::string& args);
    void cmdDownload(int64_t chatId, const std::string& url);
    void cmdFind(int64_t chatId, const std::string& query);
    void cmdDlItem(int64_t chatId, const std::string& arg);
    void cmdSaveItem(int64_t chatId, const std::string& arg);
    bool processFoundItem(int64_t chatId, const FoundItem& item, bool sendToChat);

    // Found items cache for /find
    std::map<int64_t, std::vector<FoundItem>> m_chatFoundItems;
    std::mutex m_foundMutex;

    // Sticker Hunter helpers
    void addStickerToPack(int64_t chatId, const std::string& fileId,
                          const std::string& emoji, const std::string& setName);

    // Real-time TDLib Proxy helpers
    bool applyTdlibProxy(ProxyEntry& p);
    void disableTdlibProxy();
    bool testProxyCurl(const ProxyEntry& p) const;
    std::string proxyStatusStr();
    void pickBestProxy(int64_t notifyChatId = 0);

    // HTTP helpers
    bool httpGet(const std::string& url, std::string& out) const;
    bool httpPostJson(const std::string& url, const std::string& body, std::string& out) const;

    // Bot API for Sticker Set Management
    bool botAddStickerToSet(const std::string& token, int64_t userId,
                            const std::string& name, const std::string& fileId,
                            const std::string& emoji);
    bool botCreateStickerSet(const std::string& token, int64_t userId,
                             const std::string& name, const std::string& title,
                             const std::string& fileId, const std::string& emoji);

    static std::string shellEscape(const std::string& s);
    static bool extractUrl(const std::string& text, std::string& outUrl);
};

} // namespace Skyien

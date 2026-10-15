#include <fstream>
#include "UserMonitorDB.hpp"
#include <sstream>
#include <thread>
#include <chrono>
#include <random>
#include "AutomationEngine.hpp"
#include "TerminalUI.hpp"
#include <iostream>
#include <algorithm>

namespace Skyien {

static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

AutomationEngine::AutomationEngine(TelegramClient& client, const Config& config)
    : m_client(client), m_config(config), m_cmdHandler(client, config) {
    m_stats.startTime = std::chrono::system_clock::now();
}

void AutomationEngine::start() {
    m_active.store(true);
    UserProfile me = m_client.getMe();
    m_myUserId = me.id;
    m_myUsername = toLower(me.username);
    UI::printSuccess("Skyien Automation Engine started for user: " + me.firstName + " (ID: " + std::to_string(me.id) + ")");
}

void AutomationEngine::stop() {
    m_active.store(false);
    if (m_botThread.joinable()) {
        m_botThread.join();
    }
    UI::printWarning("Skyien Automation Engine stopped.");
}

bool AutomationEngine::isRunning() const {
    return m_active.load();
}

const AutomationStats& AutomationEngine::getStats() const {
    return m_stats;
}

void AutomationEngine::reloadConfig(const Config& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
    UI::printInfo("Automation configuration reloaded.");
}

void AutomationEngine::printStatus() const {
    auto now = std::chrono::system_clock::now();
    auto uptimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - m_stats.startTime).count();

    std::cout << UI::CYAN << UI::BOLD << "=== Skyien Automation Status ===" << UI::RESET << "\n";
    std::cout << "Active:             " << (m_active.load() ? (std::string(UI::GREEN) + "RUNNING" + UI::RESET) : (std::string(UI::RED) + "STOPPED" + UI::RESET)) << "\n";
    std::cout << "AFK Mode:           " << (m_config.afkMode ? (std::string(UI::YELLOW) + "ENABLED" + UI::RESET) : "DISABLED") << "\n";
    std::cout << "Auto-Reply:         " << (m_config.autoReplyEnabled ? (std::string(UI::GREEN) + "ENABLED" + UI::RESET) : "DISABLED") << "\n";
    std::cout << "Auto-Read:          " << (m_config.autoReadMessages ? (std::string(UI::GREEN) + "ENABLED" + UI::RESET) : "DISABLED") << "\n";
    std::cout << "Bot Relay:          " << (!m_config.botToken.empty() ? (std::string(UI::GREEN) + "@" + m_config.botUsername + UI::RESET) : "NOT CONNECTED") << "\n";
    std::cout << "Channel Relay:      " << (m_config.connectedChannelId != 0 ? (std::string(UI::GREEN) + std::to_string(m_config.connectedChannelId) + UI::RESET) : "NOT CONNECTED") << "\n";
    std::cout << "Group Relay:        " << (m_config.connectedGroupId != 0 ? (std::string(UI::GREEN) + std::to_string(m_config.connectedGroupId) + UI::RESET) : "NOT CONNECTED") << "\n";
    std::cout << "Rules Loaded:       " << m_config.autoReplyRules.size() << " auto-reply, " << m_config.forwardRules.size() << " forward\n";
    std::cout << "Messages Ingested:  " << m_stats.messagesReceived << "\n";
    std::cout << "Replies Dispatched: " << m_stats.repliesSent << "\n";
    std::cout << "Relays Dispatched:  " << m_stats.relaysSent << "\n";
    std::cout << "Forwards Done:      " << m_stats.forwardsCompleted << "\n";
    std::cout << "Uptime:             " << uptimeSeconds << " seconds\n";
    std::cout << UI::CYAN << "================================" << UI::RESET << "\n";
}

void AutomationEngine::executeBroadcast(const std::string& message) {
    std::thread([this, message]() {
        std::vector<int64_t> targets = m_config.broadcastChannels;
        if (m_config.connectedChannelId != 0 && std::find(targets.begin(), targets.end(), m_config.connectedChannelId) == targets.end()) {
            targets.push_back(m_config.connectedChannelId);
        }
        if (m_config.connectedGroupId != 0 && std::find(targets.begin(), targets.end(), m_config.connectedGroupId) == targets.end()) {
            targets.push_back(m_config.connectedGroupId);
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        
        for (auto ch : targets) {
            if (!m_active.load()) break;
            if (std::find(m_config.exceptionChannels.begin(), m_config.exceptionChannels.end(), ch) != m_config.exceptionChannels.end()) {
                continue; // Skip exception channels
            }

            if (!m_config.botToken.empty()) {
                BotBridge::sendMessage(m_config.botToken, ch, message);
            } else {
                m_client.sendMessage(ch, message);
            }
            UI::printSuccess("Broadcasted to (" + std::to_string(ch) + ")");

            int sleep_min = 10;
            if (m_config.broadcastGapMode == "random") {
                std::uniform_int_distribution<> d(4, 2880); // 4 min to 2 days
                sleep_min = d(gen);
            } else if (m_config.broadcastGapMode == "custom") {
                sleep_min = m_config.broadcastGapMin;
            } else if (m_config.broadcastGapMode == "custom_random") {
                std::uniform_int_distribution<> d(m_config.broadcastGapMin, m_config.broadcastGapMax);
                sleep_min = d(gen);
            }
            
            for (int s = 0; s < sleep_min * 60; ++s) {
                if (!m_active.load()) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }).detach();
}

void AutomationEngine::broadcast(const std::string& message) {
    executeBroadcast(message);
}

void AutomationEngine::onUpdate(const Json::Value& update) {
    if (!m_active.load()) return;

    if (!update.isMember("@type")) return;
    std::string type = update["@type"].asString();

    
    if (type == "updateUser") {
        const auto& u = update["user"];
        int64_t uid = u["id"].asInt64();
        std::string uname = u.get("username", "").asString();
        std::string fn = u.get("first_name", "").asString();
        std::string ln = u.get("last_name", "").asString();
        UserMonitorDB::getInstance().trackUser(uid, uname, fn, ln, 0);
    }

    if (type == "updateNewMessage") {

        handleNewMessage(update["message"]);
    }
}

void AutomationEngine::handleNewMessage(const Json::Value& message) {
    if (!message.isMember("id") || !message.isMember("chat_id")) return;

    int64_t msgId = message["id"].asInt64();
    int64_t chatId = message["chat_id"].asInt64();
    bool isOutgoing = message.get("is_outgoing", false).asBool();

    static std::time_t s_lastOutgoingTime = 0;
    if (isOutgoing) {
        s_lastOutgoingTime = std::time(nullptr);
        std::string outText;
        if (message.isMember("content")) {
            const auto& content = message["content"];
            if (content.get("@type", "").asString() == "messageText" && content.isMember("text")) {
                outText = content["text"].get("text", "").asString();
            }
        }
        if (!outText.empty()) {
            m_cmdHandler.recordUserOutgoing(chatId, outText);
        }

        if (m_config.afkMode) {
            m_config.afkMode = false;
            m_afkNotifiedUsers.clear();
            m_config.saveToFile(Config::getDefaultConfigPath());
            UI::printInfo("[AUTO] User is online (outgoing message detected). AFK Mode automatically disabled.");
        }
        if (m_cmdHandler.handleOutgoing(message)) {
            return;
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_processedMessages.count(msgId) > 0) return;
        m_processedMessages.insert(msgId);
        m_stats.messagesReceived++;
    }

    std::string text;

    if (message.isMember("content")) {
        const auto& content = message["content"];
        if (content.get("@type", "").asString() == "messageText" && content.isMember("text")) {
            text = content["text"].get("text", "").asString();
        }
        if (content.get("@type", "").asString() == "messageChatAddMembers") {
            if (m_config.welcomeEnabled && !isOutgoing) {
                for (const auto& memId : content["member_user_ids"]) {
                    (void)memId;
                    m_client.sendMessage(chatId, m_config.welcomeMessage);
                }
            }
        }
        if (content.get("@type", "").asString() == "messageChatJoinByLink") {
             if (m_config.welcomeEnabled && !isOutgoing) {
                  m_client.sendMessage(chatId, m_config.welcomeMessage);
             }
        }
    }

    if (!isOutgoing && !text.empty()) {
        // Simple heuristic: if it's a private message, they might be replying to our broadcast
        if (chatId > 0 && chatId != m_myUserId) {
            UserMonitorDB::getInstance().addInboxMessage(chatId, "User " + std::to_string(chatId), text);
        }
    }


    int64_t senderId = 0;
    if (message.isMember("sender_id")) {
        const auto& sender = message["sender_id"];
        if (sender.get("@type", "").asString() == "messageSenderUser") {
            senderId = sender.get("user_id", 0).asInt64();
        }
    }

    if (m_config.autoReadMessages) {
        m_client.viewMessages(chatId, {msgId});
    }

    m_cmdHandler.handleIncoming(message);

    checkForward(chatId, msgId);

    if (!text.empty()) {
        if (!isOutgoing && chatId != m_myUserId && m_cmdHandler.isRayzEnabled()) {
            bool shouldReply = true;
            if (m_cmdHandler.isRayzExceptionChat(chatId)) {
                shouldReply = false;
            } else if (m_cmdHandler.getRayzMode() == "offline") {
                // If the user sent a message within the last 60 seconds, they are considered "online"
                if (std::time(nullptr) - s_lastOutgoingTime < 60) {
                    shouldReply = false;
                }
            }
            if (shouldReply) {
                bool isPrivate = (chatId > 0);
                bool isMention = (!m_myUsername.empty() && toLower(text).find("@" + m_myUsername) != std::string::npos);
                int64_t msgDate = message.get("date", 0).asInt64();
                bool isRecent = (std::time(nullptr) - msgDate <= 240);
                
                const char* home = std::getenv("HOME");
                std::string dbgPath = (home ? std::string(home) : "/tmp") + "/skyien_debug.log";
                std::ofstream dbg(dbgPath, std::ios_base::app);
                dbg << "Msg from " << chatId << " | text: " << text.substr(0, 20) 
                    << " | isPrivate: " << isPrivate << " | isMention: " << isMention 
                    << " | isRecent: " << isRecent << " | mode: " << m_cmdHandler.getRayzMode() << "\n";
                
                if ((isPrivate || isMention) && isRecent) {
                    m_cmdHandler.handleRayzIncoming(chatId, msgId, text);
                }
            }
        }
        checkAutoReply(chatId, senderId, text, msgId);
        checkRelay(chatId, senderId, text);
    }
}

bool AutomationEngine::checkAutoReply(int64_t chatId, int64_t senderId, const std::string& text, int64_t /*messageId*/) {
    if (!m_config.autoReplyEnabled && !m_config.afkMode) {
        return false;
    }

    std::string lowerText = toLower(text);

    if (m_config.autoReplyEnabled) {
        for (const auto& rule : m_config.autoReplyRules) {
            if (!rule.allowedChats.empty()) {
                bool chatAllowed = false;
                for (auto allowed : rule.allowedChats) {
                    if (allowed == chatId) {
                        chatAllowed = true;
                        break;
                    }
                }
                if (!chatAllowed) continue;
            }

            std::string triggerLower = toLower(rule.trigger);
            bool matched = false;

            if (rule.matchType == "exact") {
                matched = (lowerText == triggerLower);
            } else if (rule.matchType == "starts_with") {
                matched = (lowerText.rfind(triggerLower, 0) == 0);
            } else {
                matched = (lowerText.find(triggerLower) != std::string::npos);
            }

            if (matched) {
                UI::printAutomation("Rule Triggered: '" + rule.trigger + "' in Chat " + std::to_string(chatId), "Sending: " + rule.response);
                if (m_client.sendMessage(chatId, rule.response)) {
                    m_stats.repliesSent++;
                    return true;
                }
            }
        }
    }

    if (m_config.afkMode && !m_config.afkMessage.empty()) {
        if (chatId > 0 && chatId != 777000 && senderId != m_myUserId) {
            if (m_afkNotifiedUsers.count(chatId) == 0) {
                m_afkNotifiedUsers.insert(chatId); // Mark early to avoid races
                
                // Spawn detached thread to avoid deadlocking the update handler
                std::thread([this, chatId, senderId]() {
                    Json::Value req;
                    req["@type"] = "getUser";
                    req["user_id"] = senderId;
                    Json::Value user = m_client.sendSync(req);
                    
                    if (user.isMember("type") && user["type"]["@type"].asString() == "userTypeBot") {
                        m_afkNotifiedUsers.erase(chatId); // Revert or keep marked? Keep marked or ignore.
                        return; // It's a bot, ignore!
                    }
                    
                    UI::printAutomation("AFK Mode Triggered in Private Chat " + std::to_string(chatId), "Sending AFK response");
                    if (m_client.sendMessage(chatId, m_config.afkMessage)) {
                        m_stats.repliesSent++;
                    }
                }).detach();
                return true;
            }
        }
    }

    return false;
}

void AutomationEngine::checkForward(int64_t chatId, int64_t messageId) {
    for (const auto& fwd : m_config.forwardRules) {
        if (fwd.fromChatId == chatId && fwd.toChatId != 0) {
            UI::printAutomation("Forward Rule", "From " + std::to_string(chatId) + " to " + std::to_string(fwd.toChatId));
            Json::Value req;
            req["@type"] = "forwardMessages";
            req["chat_id"] = fwd.toChatId;
            req["from_chat_id"] = chatId;
            Json::Value ids(Json::arrayValue);
            ids.append(messageId);
            req["message_ids"] = ids;
            req["send_copy"] = false;
            m_client.sendAsync(req);
            m_stats.forwardsCompleted++;
        }
    }
}

void AutomationEngine::checkRelay(int64_t chatId, int64_t senderId, const std::string& text) {
    if (!m_config.relayToChannel && !m_config.relayToGroup) return;

    // Only relay incoming private messages or mentions
    if (chatId > 0) {
        std::string alert = "[ALERT] [Skyien Incoming DM]\nFrom: " + std::to_string(senderId) + "\nMessage: " + text;

        if (m_config.relayToChannel && m_config.connectedChannelId != 0) {
            if (!m_config.botToken.empty()) {
                BotBridge::sendMessage(m_config.botToken, m_config.connectedChannelId, alert);
            } else {
                m_client.sendMessage(m_config.connectedChannelId, alert);
            }
            m_stats.relaysSent++;
        }

        if (m_config.relayToGroup && m_config.connectedGroupId != 0) {
            if (!m_config.botToken.empty()) {
                BotBridge::sendMessage(m_config.botToken, m_config.connectedGroupId, alert);
            } else {
                m_client.sendMessage(m_config.connectedGroupId, alert);
            }
            m_stats.relaysSent++;
        }
    }
}


void AutomationEngine::botPollingTask() {
    int64_t offset = 0;
    while (m_active.load()) {
        if (!m_config.botToken.empty()) {
            Json::Value updates;
            if (BotBridge::getUpdates(m_config.botToken, offset, updates) && updates.isMember("result")) {
                for (const auto& u : updates["result"]) {
                    offset = u["update_id"].asInt64() + 1;
                    if (u.isMember("message")) {
                        int64_t chatId = u["message"]["chat"]["id"].asInt64();
                        if (u["message"].isMember("text")) {
                            std::string text = u["message"]["text"].asString();
                            handleBotCommand(chatId, text);
                        } else if (u["message"].isMember("photo") && u["message"].isMember("caption")) {
                            std::string caption = u["message"]["caption"].asString();
                            if (caption.rfind("/r", 0) == 0) {
                                const auto& photos = u["message"]["photo"];
                                if (photos.isArray() && photos.size() > 0) {
                                    std::string fileId = photos[photos.size() - 1]["file_id"].asString();
                                    handleBotCommand(chatId, "/r " + fileId);
                                }
                            } else {
                                handleBotCommand(chatId, caption);
                            }
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void AutomationEngine::handleBotCommand(int64_t chatId, const std::string& cmd) {
    std::string response;
    if (cmd.rfind("/cmd ", 0) == 0) {
        std::string command = cmd.substr(5);
        FILE* pipe = popen((command + " 2>&1").c_str(), "r");
        if (pipe) {
            char buffer[128];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                response += buffer;
            }
            pclose(pipe);
        }
        if (response.empty()) response = "[Command executed, no output]";
    } else if (cmd.rfind("/movie ", 0) == 0) {
        response = "[MOVIE] Searching for movie: " + cmd.substr(7) + "\nResult: Fake Movie Data";
    } else if (cmd.rfind("/image ", 0) == 0) {
        response = "[IMAGE] Sending image for: " + cmd.substr(7);
    } else if (cmd.rfind("/sticker", 0) == 0) {
        response = "[STICKER] Searching sticker...";
    } else if (cmd.rfind("/list", 0) == 0) {
        response = "[LIST] Making lists... Done.";
    } else if (cmd == "/inbox") {
        response = UserMonitorDB::getInstance().getInboxSummary();
    } else if (cmd.rfind("/r ", 0) == 0) {
        std::string fileId = cmd.substr(3);
        response = "[SEARCH] Super Reverse Search Results:\n\n";
        std::string getFileUrl = "https://api.telegram.org/bot" + m_config.botToken + "/getFile?file_id=" + fileId;
        std::string fileResp;
        if (BotBridge::httpGet(getFileUrl, fileResp)) {
            Json::Value fileJson;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream s(fileResp);
            if (Json::parseFromStream(builder, s, &fileJson, &errs) && fileJson.get("ok", false).asBool()) {
                std::string filePath = fileJson["result"]["file_path"].asString();
                std::string fileUrl = "https://api.telegram.org/file/bot" + m_config.botToken + "/" + filePath;
                response += "Google Lens: https://lens.google.com/uploadbyurl?url=" + fileUrl + "\n\n";
                response += "Yandex: https://yandex.com/images/search?rpt=imageview&url=" + fileUrl + "\n\n";
                response += "TinEye: https://tineye.com/search?url=" + fileUrl + "\n\n";
                response += "SauceNAO: https://saucenao.com/search.php?url=" + fileUrl + "\n";
            } else {
                response += "[-] Failed to retrieve image from Telegram.";
            }
        } else {
            response += "[-] Network error during image lookup.";
        }
    } else {
        response = "Commands: /cmd <command>, /movie <name>, /image <name>, /sticker, /list, /inbox";
    }
    BotBridge::sendMessage(m_config.botToken, chatId, response);
}

} // namespace Skyien



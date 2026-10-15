/*
 * UserCommandHandler.cpp — Skyien Userbot Command Engine
 * 
 * Zero emojis. Self-reply userbot commands for Telegram sessions.
 * Real-time proxy, sticker hunter, reel downloader, custom prefixes.
 */

#include "UserCommandHandler.hpp"
#include "TerminalUI.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <algorithm>

namespace Skyien {

static size_t curlWriteCb(void* ptr, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(ptr), total);
    return total;
}

static std::string getUserbotConfigPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "/data/data/com.termux/files/home/.skyien/userbot_config.json";
    return std::string(home) + "/.skyien/userbot_config.json";
}

static std::string getChatHistoryPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "/data/data/com.termux/files/home/.skyien/chat_history.json";
    return std::string(home) + "/.skyien/chat_history.json";
}

UserCommandHandler::UserCommandHandler(TelegramClient& client, const Config& config)
    : m_client(client), m_config(config) {
    loadState();
    loadChatHistory();
}

void UserCommandHandler::loadState() {
    std::string path = getUserbotConfigPath();
    std::ifstream f(path);
    if (!f.is_open()) return;

    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    if (Json::parseFromStream(b, f, &root, &errs)) {
        m_prefix = root.get("prefix", "/").asString();
        m_rayzEnabled = root.get("rayz_enabled", false).asBool();
        m_rayzMode = root.get("rayz_mode", "always").asString();
        m_stickerHunter = root.get("sticker_hunter", false).asBool();
        m_stickerMuted = root.get("sticker_muted", true).asBool();
        m_stickersAddedCount = root.get("stickers_added_count", 0).asInt();
        m_stickersIgnoredCount = root.get("stickers_ignored_count", 0).asInt();
        m_stickerPackName = root.get("sticker_pack_name", "rayzien's collection").asString();
        m_proxyEnabled = root.get("proxy_enabled", false).asBool();
        m_proxyMode = root.get("proxy_mode", "http").asString();
        m_activeProxy = root.get("active_proxy", -1).asInt();

        if (root.isMember("proxies") && root["proxies"].isArray()) {
            m_proxyList.clear();
            for (const auto& item : root["proxies"]) {
                ProxyEntry pe;
                pe.type = item.get("type", "http").asString();
                pe.host = item.get("host", "").asString();
                pe.port = item.get("port", 0).asInt();
                pe.user = item.get("user", "").asString();
                pe.pass = item.get("pass", "").asString();
                pe.tdlibProxyId = item.get("tdlib_proxy_id", 0).asInt();
                if (!pe.host.empty() && pe.port > 0) {
                    m_proxyList.push_back(pe);
                }
            }
        }

        if (root.isMember("seen_stickers") && root["seen_stickers"].isArray()) {
            for (const auto& s : root["seen_stickers"]) {
                m_seenStickers.insert(s.asString());
            }
        }

        if (root.isMember("rayz_exception_chats") && root["rayz_exception_chats"].isArray()) {
            std::lock_guard<std::mutex> lk(m_rayzExceptionMutex);
            m_rayzExceptionChats.clear();
            for (const auto& ec : root["rayz_exception_chats"]) {
                m_rayzExceptionChats.insert(ec.asInt64());
            }
        }

        m_hfToken = root.get("hf_token", "").asString();
        if (m_hfToken.empty()) {
            const char* envToken = std::getenv("HF_TOKEN");
            if (envToken) m_hfToken = envToken;
        }
    }
}

void UserCommandHandler::saveState() {
    std::string path = getUserbotConfigPath();
    Json::Value root;
    root["prefix"] = m_prefix;
    root["rayz_enabled"] = m_rayzEnabled;
    root["rayz_mode"] = m_rayzMode;
    root["sticker_hunter"] = m_stickerHunter;
    root["sticker_muted"] = m_stickerMuted;
    root["stickers_added_count"] = m_stickersAddedCount;
    root["stickers_ignored_count"] = m_stickersIgnoredCount;
    root["sticker_pack_name"] = m_stickerPackName;
    root["proxy_enabled"] = m_proxyEnabled;
    root["proxy_mode"] = m_proxyMode;
    root["active_proxy"] = m_activeProxy;

    Json::Value proxies(Json::arrayValue);
    for (const auto& pe : m_proxyList) {
        Json::Value p;
        p["type"] = pe.type;
        p["host"] = pe.host;
        p["port"] = pe.port;
        p["user"] = pe.user;
        p["pass"] = pe.pass;
        p["tdlib_proxy_id"] = pe.tdlibProxyId;
        proxies.append(p);
    }
    root["proxies"] = proxies;

    Json::Value stickers(Json::arrayValue);
    for (const auto& s : m_seenStickers) {
        stickers.append(s);
    }
    root["seen_stickers"] = stickers;

    Json::Value excList(Json::arrayValue);
    {
        std::lock_guard<std::mutex> lk(m_rayzExceptionMutex);
        for (auto cid : m_rayzExceptionChats) {
            excList.append(static_cast<Json::Int64>(cid));
        }
    }
    root["rayz_exception_chats"] = excList;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(wb.newStreamWriter());
    std::ofstream out(path);
    if (out.is_open()) {
        writer->write(root, &out);
    }
}

bool UserCommandHandler::isRayzExceptionChat(int64_t chatId) {
    std::lock_guard<std::mutex> lk(m_rayzExceptionMutex);
    return m_rayzExceptionChats.count(chatId) > 0;
}

void UserCommandHandler::loadChatHistory() {
    std::string path = getChatHistoryPath();
    std::ifstream f(path);
    if (!f.is_open()) return;
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    if (Json::parseFromStream(b, f, &root, &errs) && root.isObject()) {
        std::lock_guard<std::mutex> lk(m_rayzHistoryMutex);
        m_rayzChatHistory.clear();
        for (const auto& key : root.getMemberNames()) {
            try {
                int64_t cid = std::stoll(key);
                const auto& arr = root[key];
                if (arr.isArray()) {
                    for (const auto& item : arr) {
                        m_rayzChatHistory[cid].push_back({
                            item.get("role", "user").asString(),
                            item.get("text", "").asString()
                        });
                    }
                }
            } catch (...) {}
        }
    }
}

void UserCommandHandler::saveChatHistory() {
    std::string path = getChatHistoryPath();
    Json::Value root(Json::objectValue);
    {
        std::lock_guard<std::mutex> lk(m_rayzHistoryMutex);
        for (const auto& [cid, hist] : m_rayzChatHistory) {
            Json::Value arr(Json::arrayValue);
            for (const auto& item : hist) {
                Json::Value m;
                m["role"] = item.role;
                m["text"] = item.text;
                arr.append(m);
            }
            root[std::to_string(cid)] = arr;
        }
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(wb.newStreamWriter());
    std::ofstream out(path);
    if (out.is_open()) {
        writer->write(root, &out);
    }
}

void UserCommandHandler::recordUserOutgoing(int64_t chatId, const std::string& text) {
    if (text.empty()) return;
    if (text[0] == '/' || text[0] == '.') return;
    {
        std::lock_guard<std::mutex> lk(m_rayzHistoryMutex);
        auto& hist = m_rayzChatHistory[chatId];
        hist.push_back({ "assistant", text });
        if (hist.size() > 10) {
            hist.erase(hist.begin(), hist.begin() + (hist.size() - 10));
        }
    }
    saveChatHistory();
}

void UserCommandHandler::setPrefix(const std::string& p) {
    if (!p.empty()) {
        m_prefix = p;
        saveState();
    }
}

void UserCommandHandler::reply(int64_t chatId, const std::string& text) {
    m_client.sendMessage(chatId, text);
}

void UserCommandHandler::replyAndPurge(int64_t chatId, const std::string& text, int delaySeconds) {
    int64_t msgId = replySync(chatId, text);
    if (msgId == 0) return;
    std::thread([this, chatId, msgId, delaySeconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
        deleteMsg(chatId, msgId);
    }).detach();
}

int64_t UserCommandHandler::replySync(int64_t chatId, const std::string& text) {
    Json::Value req;
    req["@type"] = "sendMessage";
    req["chat_id"] = chatId;
    Json::Value content;
    content["@type"] = "inputMessageText";
    Json::Value txt;
    txt["@type"] = "formattedText";
    txt["text"] = text;
    content["text"] = txt;
    req["input_message_content"] = content;
    Json::Value res = m_client.sendSync(req, 4.0);
    return res.get("id", 0).asInt64();
}

void UserCommandHandler::editMessage(int64_t chatId, int64_t messageId, const std::string& text) {
    if (messageId == 0) return;
    Json::Value req;
    req["@type"] = "editMessageText";
    req["chat_id"] = chatId;
    req["message_id"] = messageId;
    Json::Value content;
    content["@type"] = "inputMessageText";
    Json::Value txt;
    txt["@type"] = "formattedText";
    txt["text"] = text;
    content["text"] = txt;
    req["input_message_content"] = content;
    m_client.sendAsync(req);
}

void UserCommandHandler::deleteMsg(int64_t chatId, int64_t msgId) {
    Json::Value req;
    req["@type"] = "deleteMessages";
    req["chat_id"] = chatId;
    req["revoke"] = true;
    Json::Value ids(Json::arrayValue);
    ids.append(msgId);
    req["message_ids"] = ids;
    m_client.sendAsync(req);
}

std::string UserCommandHandler::shellEscape(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

bool UserCommandHandler::extractUrl(const std::string& text, std::string& outUrl) {
    std::istringstream iss(text);
    std::string tok;
    while (iss >> tok) {
        if (tok.find("http://") == 0 || tok.find("https://") == 0) {
            outUrl = tok;
            return true;
        }
        if (tok.find("instagram.com") != std::string::npos || 
            tok.find("youtube.com") != std::string::npos || 
            tok.find("youtu.be") != std::string::npos) {
            outUrl = "https://" + tok;
            return true;
        }
    }
    return false;
}

bool UserCommandHandler::matchPrefix(const std::string& text, std::string& cmd, std::string& args) {
    if (text.empty()) return false;

    // Check single '+' character shortcut for proxy toggle
    if (text == "+") {
        cmd = "proxy";
        args = "";
        return true;
    }

    std::string matchedPrefix = "";
    if (!m_prefix.empty() && text.rfind(m_prefix, 0) == 0) {
        matchedPrefix = m_prefix;
    } else if (text.rfind("/", 0) == 0) {
        matchedPrefix = "/";
    } else if (text.rfind(".", 0) == 0) {
        matchedPrefix = ".";
    } else if (text.rfind("+", 0) == 0) {
        matchedPrefix = "+";
    }

    if (matchedPrefix.empty()) return false;

    std::string rest = text.substr(matchedPrefix.size());
    if (rest.empty()) return false;

    auto sp = rest.find(' ');
    if (sp == std::string::npos) {
        cmd = rest;
        args = "";
    } else {
        cmd = rest.substr(0, sp);
        args = rest.substr(sp + 1);
    }

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    return true;
}

bool UserCommandHandler::handleOutgoing(const Json::Value& message) {
    if (!message.isMember("content")) return false;

    int64_t chatId = message.get("chat_id", 0).asInt64();
    int64_t msgId = message.get("id", 0).asInt64();

    const auto& content = message["content"];
    std::string contentType = content.get("@type", "").asString();

    std::string text;
    if (contentType == "messageText" && content.isMember("text")) {
        text = content["text"].get("text", "").asString();
    }

    if (text.empty()) return false;

    // Ignore bot-generated messages to prevent infinite loops
    if (text.rfind("[*]", 0) == 0 || text.rfind("[!]", 0) == 0) return false;

    // 1. Direct Reel/Video Link Detection
    if (text.find("instagram.com/") != std::string::npos ||
        text.find("youtube.com/shorts/") != std::string::npos ||
        text.find("youtu.be/") != std::string::npos ||
        text.find("youtube.com/watch") != std::string::npos) {
        std::string rawUrl;
        if (extractUrl(text, rawUrl)) {
            UI::printAutomation("Userbot Reel Downloader Triggered", rawUrl);
            deleteMsg(chatId, msgId);
            std::thread([this, chatId, rawUrl]() {
                cmdDownload(chatId, rawUrl);
            }).detach();
            return true;
        }
    }

    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

    // 1.4 Rayz Stop Trigger (Silence and stop all actions)
    if (lowerText == "rayz stop" || lowerText == "/rayz stop" || lowerText == ".rayz stop") {
        m_rayzEnabled = false;
        saveState();
        deleteMsg(chatId, msgId);
        UI::printInfo("[RAYZ] Stopped and silenced completely by user command.");
        return true;
    }

    // 1.5 Hey Rayz query command (Ask Rayz and have it reply hooked to the message)
    if (lowerText.rfind("hey rayz ", 0) == 0 || lowerText == "hey rayz") {
        std::string prompt = (lowerText.size() > 9) ? text.substr(9) : "";
        cmdHeyRayz(chatId, msgId, prompt);
        return true;
    }

    // 1.6 Natural Rayz Image Generation (Directly send image output without placeholder)
    if (lowerText.rfind("rayz ", 0) == 0) {
        std::string prompt = text.substr(5);
        deleteMsg(chatId, msgId);
        cmdRayimage(chatId, 0, prompt, false);
        return true;
    }

    // 2. Command matching
    std::string cmd, args;
    if (!matchPrefix(text, cmd, args)) {
        return false;
    }

    // Delete outgoing command message for a clean userbot experience
    deleteMsg(chatId, msgId);
    UI::printAutomation("Userbot Command Received", cmd + " " + args);

    if (cmd == "help") {
        cmdHelp(chatId);
        return true;
    } else if (cmd == "settings") {
        cmdSettings(chatId);
        return true;
    } else if (cmd == "sticker") {
        cmdSticker(chatId, args);
        return true;
    } else if (cmd == "addsticker") {
        cmdAddSticker(chatId, msgId, message);
        return true;
    } else if (cmd == "proxy") {
        cmdProxy(chatId, args);
        return true;
    } else if (cmd == "find") {
        std::thread([this, chatId, args]() {
            cmdFind(chatId, args);
        }).detach();
        return true;
    } else if (cmd == "dl" || cmd == "reel" || cmd == "download") {
        std::string target = args;
        if (target.empty()) {
            extractUrl(text, target);
        }
        
        int64_t replyToId = message.get("reply_to_message_id", 0).asInt64();
        if (replyToId != 0 && target.find("http") == std::string::npos) {
            Json::Value req;
            req["@type"] = "getMessage";
            req["chat_id"] = chatId;
            req["message_id"] = replyToId;
            Json::Value res = m_client.sendSync(req, 4.0);
            
            if (res.isMember("content")) {
                const auto& repContent = res["content"];
                std::string repType = repContent.get("@type", "").asString();
                std::string repText = "";
                if (repType == "messageText" && repContent.isMember("text")) {
                    repText = repContent["text"].get("text", "").asString();
                } else if (repContent.isMember("caption")) {
                    repText = repContent["caption"].get("text", "").asString();
                }
                std::string targetUrl;
                extractUrl(repText, targetUrl);
                if (!targetUrl.empty()) {
                    if (!target.empty()) target += " " + targetUrl;
                    else target = targetUrl;
                }
            }
        }

        if (!target.empty()) {
            bool isIndexOrAll = (target == "all");
            if (!isIndexOrAll) {
                try {
                    int n = std::stoi(target);
                    if (n > 0) isIndexOrAll = true;
                } catch (...) {}
            }
            if (isIndexOrAll) {
                std::thread([this, chatId, target]() {
                    cmdDlItem(chatId, target);
                }).detach();
            } else {
                std::thread([this, chatId, target]() {
                    cmdDownload(chatId, target);
                }).detach();
            }
        } else {
            reply(chatId, "[!] Usage: " + m_prefix + "dl <url> or " + m_prefix + "dl <number from /find>");
        }
        return true;
    } else if (cmd == "save") {
        std::string target = args;
        if (!target.empty()) {
            std::thread([this, chatId, target]() {
                cmdSaveItem(chatId, target);
            }).detach();
        } else {
            reply(chatId, "[!] Usage: " + m_prefix + "save <number from /find> or " + m_prefix + "save all");
        }
        return true;
    } else if (cmd == "prefix") {
        if (!args.empty()) {
            setPrefix(args);
            reply(chatId, "[*] Userbot command prefix changed to: " + m_prefix);
        } else {
            reply(chatId, "[*] Current prefix: " + m_prefix + "\nTo change: " + m_prefix + "prefix <new_prefix>");
        }
        return true;
    } else if (cmd == "fs" || cmd == "forcestop") {
        reply(chatId, "[!] Force stopping Skyien...");
        std::exit(0);
        return true;
    } else if (cmd == "rayz") {
        cmdRayz(chatId, msgId, args);
        return true;
    } else if (cmd == "rayimage") {
        cmdRayimage(chatId, msgId, args);
        return true;
    }

    return false;
}

void UserCommandHandler::handleIncoming(const Json::Value& message) {
    if (!message.isMember("content")) return;
    int64_t chatId = message.get("chat_id", 0).asInt64();

    const auto& content = message["content"];
    std::string contentType = content.get("@type", "").asString();

    // Check for sticker hunter
    if (m_stickerHunter && contentType == "messageSticker" && content.isMember("sticker")) {
        const auto& sticker = content["sticker"];
        std::string uid = "";
        std::string fileId = "";
        std::string emoji = sticker.get("emoji", "").asString();

        if (sticker.isMember("sticker") && sticker["sticker"].isMember("id")) {
            uid = std::to_string(sticker["sticker"]["id"].asInt64());
        }
        if (sticker.isMember("sticker") && sticker["sticker"].isMember("remote")) {
            fileId = sticker["sticker"]["remote"].get("id", "").asString();
        }

        if (uid.empty() && sticker.isMember("set_id")) {
            uid = std::to_string(sticker["set_id"].asInt64());
        }

        if (!fileId.empty()) {
            {
                std::lock_guard<std::mutex> lk(m_stickerMutex);
                if (m_seenStickers.count(fileId) > 0 || (!uid.empty() && m_seenStickers.count(uid) > 0)) {
                    m_stickersIgnoredCount++;
                    saveState();
                    return; // Already seen
                }
                if (!uid.empty()) m_seenStickers.insert(uid);
                m_seenStickers.insert(fileId);
                saveState();
            }

            if (!m_stickerMuted) {
                replyAndPurge(chatId, "+sticker", 3);
            }

            std::string safeName;
            for (char c : m_stickerPackName) {
                if (std::isalnum((unsigned char)c)) safeName += std::tolower((unsigned char)c);
                else if (c == ' ' || c == '\'' || c == '-') safeName += '_';
            }
            if (safeName.empty()) safeName = "rayziens_collection";

            std::string botUsername = m_config.botUsername.empty() ? "Rayzzistbot" : m_config.botUsername;
            std::string setName = safeName + "_by_" + botUsername;

            addStickerToPack(chatId, fileId, emoji.empty() ? "X" : emoji, setName);
        }
    }
}

void UserCommandHandler::cmdHelp(int64_t chatId) {
    std::string out =
        "[AZZIZTANT + SKYIEN USERBOT]\n"
        "Commands execute directly using your account in this chat.\n"
        "Active prefix: " + m_prefix + "\n\n"
        "--- STICKER HUNTER ---\n"
        + m_prefix + "sticker             Toggle auto sticker hunter ON/OFF\n"
        + m_prefix + "sticker mute        Toggle sticker hunter spam output\n"
        + m_prefix + "sticker status      Show hunter status and captured count\n"
        + m_prefix + "sticker name <name> Change custom sticker pack name\n"
        + m_prefix + "addsticker          Reply to a sticker to manually add it\n\n"
        "--- PROXY MANAGER ---\n"
        + m_prefix + "proxy               Toggle real-time proxy ON/OFF\n"
        "+                   Shortcut: type '+' alone to toggle proxy\n"
        + m_prefix + "proxy list          List all configured proxies\n"
        + m_prefix + "proxy status        Show real-time proxy connection status\n"
        + m_prefix + "proxy mode <h/s>    Set proxy mode (http or socks5)\n"
        + m_prefix + "proxy add <t> <ip> <port> [user] [pass]\n"
        + m_prefix + "proxy fetch         Download fresh free SOCKS5 proxies\n"
        + m_prefix + "proxy test          Test all proxies & auto-select best\n\n"
        "--- RAYZ AI SUITE ---\n"
        + m_prefix + "rayz                Toggle Qwen 2.5 Auto-Responder ON/OFF\n"
        + m_prefix + "rayimage <prompt>   Generate an image using SDXL\n\n"
        "--- MEDIA DOWNLOADER ---\n"
        + m_prefix + "dl <url>            Download Instagram/YouTube reel & send video\n"
        "Or just paste any Instagram/YouTube reel link directly in any chat\n\n"
        "--- CHAT SEARCH & DOWNLOAD ---\n"
        + m_prefix + "find \"<query>\"      Search chat for links, documents, or files\n"
        + m_prefix + "dl <number>         Download found item & send here in chat\n"
        + m_prefix + "save <number>       Save found item locally to ~/downloads\n"
        + m_prefix + "dl all              Download and send all found items\n"
        + m_prefix + "save all            Save all found items to ~/downloads\n\n"
        "--- CONFIGURATION ---\n"
        + m_prefix + "settings            Show current userbot configuration\n"
        + m_prefix + "prefix <p>          Change command prefix (e.g. " + m_prefix + "prefix +)\n"
        + m_prefix + "help                Show this command manual\n";

    replyAndPurge(chatId, out, 30);
}

void UserCommandHandler::cmdSettings(int64_t chatId) {
    std::string hunterStr = m_stickerHunter ? "ENABLED (hunting active)" : "DISABLED";
    std::string out =
        "[SKYIEN USERBOT SETTINGS]\n"
        "-------------------------------------\n"
        "Command Prefix:    " + m_prefix + "\n"
        "Sticker Hunter:    " + hunterStr + "\n"
        "Sticker Pack Name: " + m_stickerPackName + "\n"
        "Stickers Captured: " + std::to_string(m_seenStickers.size()) + "\n"
        "Bot Token Bridge:  " + (!m_config.botToken.empty() ? ("@" + m_config.botUsername) : "NOT SET") + "\n"
        "Proxy Mode:        " + m_proxyMode + "\n"
        "Real-Time Proxy:   " + proxyStatusStr() + "\n"
        "Total Proxies:     " + std::to_string(m_proxyList.size()) + "\n"
        "-------------------------------------\n";

    replyAndPurge(chatId, out, 15);
}

void UserCommandHandler::cmdSticker(int64_t chatId, const std::string& args) {
    if (args.empty()) {
        m_stickerHunter = !m_stickerHunter;
        saveState();
        std::string st = m_stickerHunter ? "ENABLED" : "DISABLED";
        replyAndPurge(chatId, "[*] Sticker Hunter is now " + st + ".\nPack: " + m_stickerPackName, 5);
        return;
    }

    if (args == "mute") {
        m_stickerMuted = !m_stickerMuted;
        saveState();
        std::string mt = m_stickerMuted ? "MUTED" : "UNMUTED";
        replyAndPurge(chatId, "[*] Sticker Hunter output is now " + mt + ".", 5);
        return;
    }

    if (args == "status") {
        std::lock_guard<std::mutex> lk(m_stickerMutex);
        std::string st = m_stickerHunter ? "ONLINE" : "OFFLINE";
        std::string mt = m_stickerMuted ? "YES" : "NO";
        replyAndPurge(chatId,
              "--- Sticker Hunter Status ---\n"
              "Status: " + st + "\n"
              "Muted: " + mt + "\n"
              "Stickers Added: " + std::to_string(m_stickersAddedCount) + "\n"
              "Duplicates Ignored: " + std::to_string(m_stickersIgnoredCount) + "\n"
              "Memory Cache Size: " + std::to_string(m_seenStickers.size()) + "\n"
              "Pack Name: " + m_stickerPackName + "\n"
              "-----------------------------", 10);
        return;
    }

    if (args.rfind("name ", 0) == 0) {
        std::string newName = args.substr(5);
        if (!newName.empty()) {
            m_stickerPackName = newName;
            saveState();
            replyAndPurge(chatId, "[*] Sticker pack name updated to: '" + m_stickerPackName + "'", 5);
            return;
        }
    }

    replyAndPurge(chatId, "[!] Usage:\n"
                  + m_prefix + "sticker (toggle)\n"
                  + m_prefix + "sticker mute (toggle output)\n"
                  + m_prefix + "sticker status\n"
                  + m_prefix + "sticker name <new pack name>", 10);
}

void UserCommandHandler::cmdAddSticker(int64_t chatId, int64_t /*msgId*/, const Json::Value& fullMsg) {
    int64_t replyToId = fullMsg.get("reply_to_message_id", 0).asInt64();
    if (replyToId == 0) {
        reply(chatId, "[!] Please reply to a sticker message with " + m_prefix + "addsticker");
        return;
    }

    Json::Value req;
    req["@type"] = "getMessage";
    req["chat_id"] = chatId;
    req["message_id"] = replyToId;
    Json::Value res = m_client.sendSync(req, 4.0);

    if (!res.isMember("content") || res["content"].get("@type", "").asString() != "messageSticker") {
        reply(chatId, "[!] The replied message is not a sticker.");
        return;
    }

    const auto& sticker = res["content"]["sticker"];
    std::string fileId = "";
    if (sticker.isMember("sticker") && sticker["sticker"].isMember("remote")) {
        fileId = sticker["sticker"]["remote"].get("id", "").asString();
    }
    std::string emoji = sticker.get("emoji", "X").asString();

    if (fileId.empty()) {
        reply(chatId, "[!] Could not extract sticker file ID from Telegram.");
        return;
    }

    std::string safeName;
    for (char c : m_stickerPackName) {
        if (std::isalnum((unsigned char)c)) safeName += std::tolower((unsigned char)c);
        else if (c == ' ' || c == '\'' || c == '-') safeName += '_';
    }
    if (safeName.empty()) safeName = "rayziens_collection";

    std::string botUsername = m_config.botUsername.empty() ? "Rayzzistbot" : m_config.botUsername;
    std::string setName = safeName + "_by_" + botUsername;

    reply(chatId, "[*] Adding sticker to '" + m_stickerPackName + "'...");
    addStickerToPack(chatId, fileId, emoji, setName);
}

void UserCommandHandler::addStickerToPack(int64_t chatId, const std::string& fileId,
                                           const std::string& emoji, const std::string& setName) {
    if (m_config.botToken.empty()) {
        reply(chatId, "[!] No bot token connected. Connect via 'skyien connect bot <token>' to manage sticker sets.");
        return;
    }

    int64_t myId = m_client.getMyUserId();
    bool added = botAddStickerToSet(m_config.botToken, myId, setName, fileId, emoji);
    if (!added) {
        bool created = botCreateStickerSet(m_config.botToken, myId, setName, m_stickerPackName, fileId, emoji);
        if (created) {
            m_stickersAddedCount++;
            saveState();
            if (!m_stickerMuted) replyAndPurge(chatId, "+sticker", 3);
        } else {
            if (!m_stickerMuted) replyAndPurge(chatId, "[!] Could not add sticker to pack. Ensure you have started the bot @" + m_config.botUsername + " first.", 5);
        }
    } else {
        m_stickersAddedCount++;
        saveState();
        if (!m_stickerMuted) replyAndPurge(chatId, "+sticker", 3);
    }
}

bool UserCommandHandler::applyTdlibProxy(ProxyEntry& p) {
    Json::Value req;
    req["@type"] = "addProxy";
    req["server"] = p.host;
    req["port"] = p.port;
    req["enable"] = true;

    if (p.type == "socks5") {
        Json::Value pType;
        pType["@type"] = "proxyTypeSocks5";
        pType["username"] = p.user;
        pType["password"] = p.pass;
        req["type"] = pType;
    } else {
        Json::Value pType;
        pType["@type"] = "proxyTypeHttp";
        pType["username"] = p.user;
        pType["password"] = p.pass;
        pType["http_only"] = false;
        req["type"] = pType;
    }

    Json::Value res = m_client.sendSync(req, 6.0);
    if (res.get("@type", "").asString() == "proxy") {
        p.tdlibProxyId = res.get("id", 0).asInt();
        return true;
    }
    return false;
}

void UserCommandHandler::disableTdlibProxy() {
    Json::Value req;
    req["@type"] = "disableProxy";
    m_client.sendSync(req, 4.0);
}

bool UserCommandHandler::testProxyCurl(const ProxyEntry& p) const {
    CURL* c = curl_easy_init();
    if (!c) return false;
    std::string addr = p.host + ":" + std::to_string(p.port);
    curl_easy_setopt(c, CURLOPT_URL, "https://api.telegram.org");
    curl_easy_setopt(c, CURLOPT_PROXY, addr.c_str());
    curl_easy_setopt(c, CURLOPT_PROXYTYPE, p.type == "socks5" ? CURLPROXY_SOCKS5 : CURLPROXY_HTTP);
    if (!p.user.empty()) {
        std::string auth = p.user + ":" + p.pass;
        curl_easy_setopt(c, CURLOPT_PROXYUSERPWD, auth.c_str());
    }
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 6L);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    CURLcode r = curl_easy_perform(c);
    curl_easy_cleanup(c);
    return (r == CURLE_OK);
}

std::string UserCommandHandler::proxyStatusStr() {
    std::lock_guard<std::mutex> lk(m_proxyMutex);
    if (!m_proxyEnabled) return "DISABLED";
    if (m_activeProxy < 0 || m_activeProxy >= static_cast<int>(m_proxyList.size())) {
        return "ENABLED (no active proxy selected - run " + m_prefix + "proxy test)";
    }
    const auto& p = m_proxyList[m_activeProxy];
    return "ACTIVE: " + p.type + "://" + p.host + ":" + std::to_string(p.port) +
           " [#" + std::to_string(m_activeProxy + 1) + "]";
}

void UserCommandHandler::pickBestProxy(int64_t notifyChatId) {
    std::lock_guard<std::mutex> lk(m_proxyMutex);
    if (m_proxyList.empty()) {
        if (notifyChatId != 0) {
            reply(notifyChatId, "[!] No proxies saved. Add one with:\n" + m_prefix + "proxy add <http|socks5> <host> <port> [user] [pass]");
        }
        return;
    }

    int bestIdx = -1;
    for (size_t i = 0; i < m_proxyList.size(); ++i) {
        if (testProxyCurl(m_proxyList[i])) {
            bestIdx = static_cast<int>(i);
            break;
        }
    }

    if (bestIdx >= 0) {
        m_activeProxy = bestIdx;
        m_proxyEnabled = true;
        applyTdlibProxy(m_proxyList[bestIdx]);
        saveState();
        if (notifyChatId != 0) {
            reply(notifyChatId, "[*] Selected and activated best proxy:\n" +
                                m_proxyList[bestIdx].type + "://" + m_proxyList[bestIdx].host + ":" +
                                std::to_string(m_proxyList[bestIdx].port));
        }
    } else {
        if (notifyChatId != 0) {
            reply(notifyChatId, "[!] All configured proxies failed test.");
        }
    }
}

void UserCommandHandler::cmdProxy(int64_t chatId, const std::string& args) {
    if (args.empty()) {
        m_proxyEnabled = !m_proxyEnabled;
        if (m_proxyEnabled) {
            if (m_activeProxy >= 0 && m_activeProxy < static_cast<int>(m_proxyList.size())) {
                applyTdlibProxy(m_proxyList[m_activeProxy]);
                reply(chatId, "[*] Real-time Proxy ENABLED.\nActive: " + proxyStatusStr());
            } else if (!m_proxyList.empty()) {
                reply(chatId, "[*] Real-time Proxy ENABLED. Testing proxies...");
                std::thread([this, chatId]() {
                    pickBestProxy(chatId);
                }).detach();
            } else {
                reply(chatId, "[*] Proxy ENABLED, but no proxies in list.\nAdd with: " + m_prefix + "proxy add <type> <host> <port>");
            }
        } else {
            disableTdlibProxy();
            reply(chatId, "[*] Real-time Proxy DISABLED.");
        }
        saveState();
        return;
    }

    if (args == "status") {
        reply(chatId, "[*] Real-time Proxy Status: " + proxyStatusStr());
        return;
    }

    if (args == "list") {
        std::lock_guard<std::mutex> lk(m_proxyMutex);
        if (m_proxyList.empty()) {
            reply(chatId, "[*] No proxies configured.\nAdd with: " + m_prefix + "proxy add <http|socks5> <host> <port> [user] [pass]");
            return;
        }
        std::string out = "[*] Configured Proxies (" + std::to_string(m_proxyList.size()) + " total):\n";
        for (size_t i = 0; i < m_proxyList.size(); ++i) {
            const auto& p = m_proxyList[i];
            out += (static_cast<int>(i) == m_activeProxy ? " [*] #" : " [ ] #") + std::to_string(i + 1) + " " +
                   p.type + "://" + p.host + ":" + std::to_string(p.port);
            if (!p.user.empty()) out += " (auth)";
            out += "\n";
        }
        reply(chatId, out);
        return;
    }

    if (args.rfind("add ", 0) == 0) {
        std::istringstream iss(args.substr(4));
        ProxyEntry pe;
        std::string portStr;
        iss >> pe.type >> pe.host >> portStr;
        try {
            pe.port = std::stoi(portStr);
        } catch (...) {
            pe.port = 0;
        }
        iss >> pe.user >> pe.pass;

        if (!pe.host.empty() && pe.port > 0) {
            if (pe.type != "http" && pe.type != "socks5") {
                pe.type = "http";
            }
            {
                std::lock_guard<std::mutex> lk(m_proxyMutex);
                m_proxyList.push_back(pe);
                if (m_activeProxy < 0) {
                    m_activeProxy = static_cast<int>(m_proxyList.size()) - 1;
                }
                saveState();
            }
            reply(chatId, "[*] Added proxy #" + std::to_string(m_proxyList.size()) + ": " +
                          pe.type + "://" + pe.host + ":" + std::to_string(pe.port));
        } else {
            reply(chatId, "[!] Usage: " + m_prefix + "proxy add <http|socks5> <host> <port> [user] [pass]");
        }
        return;
    }

    if (args.rfind("mode ", 0) == 0) {
        std::string mode = args.substr(5);
        if (mode == "http" || mode == "socks5") {
            m_proxyMode = mode;
            saveState();
            reply(chatId, "[*] Default proxy mode set to: " + m_proxyMode);
        } else {
            reply(chatId, "[!] Proxy mode must be 'http' or 'socks5'");
        }
        return;
    }

    if (args == "fetch") {
        int64_t msgId = replySync(chatId, "[*] Fetching fresh SOCKS5 proxies from public lists...");
        if (msgId == 0) return;
        std::thread([this, chatId, msgId]() {
            std::string out;
            if (httpGet("https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks5.txt", out)) {
                std::istringstream iss(out);
                std::string line;
                int count = 0;
                {
                    std::lock_guard<std::mutex> lk(m_proxyMutex);
                    while (std::getline(iss, line) && count < 20) {
                        if (line.empty() || line.find(":") == std::string::npos) continue;
                        size_t colonPos = line.find(":");
                        ProxyEntry pe;
                        pe.type = "socks5";
                        pe.host = line.substr(0, colonPos);
                        try {
                            pe.port = std::stoi(line.substr(colonPos + 1));
                            m_proxyList.push_back(pe);
                            count++;
                        } catch (...) {}
                    }
                    if (m_activeProxy < 0 && !m_proxyList.empty()) m_activeProxy = 0;
                    saveState();
                }
                editMessage(chatId, msgId, "[*] Successfully fetched and added " + std::to_string(count) + " free SOCKS5 proxies.\nRun '" + m_prefix + "proxy test' to auto-select the best one!");
            } else {
                editMessage(chatId, msgId, "[!] Failed to download proxy list.");
            }
        }).detach();
        return;
    }

    if (args == "test") {
        reply(chatId, "[*] Testing all proxies in list...");
        std::thread([this, chatId]() {
            pickBestProxy(chatId);
        }).detach();
        return;
    }

    reply(chatId, "[!] Unknown proxy command.\nOptions: list, add, mode, status, test");
}

void UserCommandHandler::cmdDownload(int64_t chatId, const std::string& args) {
    std::string targetUrl = args;
    std::string format = "'best[ext=mp4]/best'";
    bool isAudio = false;

    if (args.rfind("audio ", 0) == 0) {
        targetUrl = args.substr(6);
        isAudio = true;
        format = "'bestaudio/best' -x --audio-format mp3";
    } else if (args.rfind("720p ", 0) == 0) {
        targetUrl = args.substr(5);
        format = "'best[height<=720][ext=mp4]/best'";
    } else if (args.rfind("1080p ", 0) == 0) {
        targetUrl = args.substr(6);
        format = "'best[height<=1080][ext=mp4]/best'";
    } else if (args.rfind("480p ", 0) == 0) {
        targetUrl = args.substr(5);
        format = "'best[height<=480][ext=mp4]/best'";
    }

    int64_t msgId = replySync(chatId, "[*] Downloading: " + targetUrl + " [   ]");

    std::atomic<bool> downloading{true};
    std::thread dlAnim([this, chatId, msgId, targetUrl, &downloading]() {
        const char* frames[] = { "[=  ]", "[== ]", "[===]", "[ ==]", "[  =]", "[   ]" };
        int i = 0;
        while (downloading.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            if (!downloading.load()) break;
            editMessage(chatId, msgId, "[*] Downloading: " + targetUrl + " " + frames[i]);
            i = (i + 1) % 6;
        }
    });

    const char* home = std::getenv("HOME");
    std::string tmpDir = (home ? std::string(home) : "/tmp") + "/downloads/Skyien_Media/";
    std::system(("mkdir -p " + tmpDir).c_str());
    std::string ext = isAudio ? ".mp3" : ".mp4";
    std::string filename = tmpDir + "skyien_media_" + std::to_string(chatId) + ext;
    ::remove(filename.c_str());

    std::string cmd = "yt-dlp -f " + format + " -o " + shellEscape(filename) + " " + shellEscape(targetUrl) + " > /dev/null 2>&1";

    int ret = std::system(cmd.c_str());
    
    downloading.store(false);
    if (dlAnim.joinable()) dlAnim.join();

    if (ret != 0) {
        // Silently fail as requested by user
        deleteMsg(chatId, msgId);
        return;
    }

    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        deleteMsg(chatId, msgId);
        return;
    }
    long long sizeBytes = file.tellg();
    file.close();

    if (sizeBytes <= 0) {
        ::remove(filename.c_str());
        deleteMsg(chatId, msgId);
        return;
    }

    std::string mbStr = std::to_string(sizeBytes / (1024 * 1024));
    std::string typeStr = isAudio ? "audio" : "video";
    editMessage(chatId, msgId, "[*] Uploading " + typeStr + " (" + mbStr + " MB) [   ]");

    std::atomic<bool> uploading{true};
    std::thread upAnim([this, chatId, msgId, mbStr, typeStr, &uploading]() {
        const char* frames[] = { "[=  ]", "[== ]", "[===]", "[ ==]", "[  =]", "[   ]" };
        int i = 0;
        while (uploading.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            if (!uploading.load()) break;
            editMessage(chatId, msgId, "[*] Uploading " + typeStr + " (" + mbStr + " MB) " + frames[i]);
            i = (i + 1) % 6;
        }
    });

    Json::Value req;
    req["@type"] = "sendMessage";
    req["chat_id"] = chatId;

    Json::Value content;
    Json::Value inputFile;
    inputFile["@type"] = "inputFileLocal";
    inputFile["path"] = filename;

    if (isAudio) {
        content["@type"] = "inputMessageAudio";
        content["audio"] = inputFile;
    } else {
        content["@type"] = "inputMessageVideo";
        content["video"] = inputFile;
    }
    content["supports_streaming"] = true;
    req["input_message_content"] = content;

    Json::Value res = m_client.sendSync(req, 180.0);
    if (res.get("@type", "").asString() != "message") {
        // Fallback to sending as document
        content["@type"] = "inputMessageDocument";
        content["document"] = inputFile;
        req["input_message_content"] = content;
        res = m_client.sendSync(req, 180.0);
    }

    uploading.store(false);
    if (upAnim.joinable()) upAnim.join();

    if (res.get("@type", "").asString() == "message") {
        editMessage(chatId, msgId, "[*] Video sent successfully!");
    } else {
        editMessage(chatId, msgId, "[!] Upload failed. Saved locally to: " + filename);
        return;
    }

    std::thread([filename]() {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        ::remove(filename.c_str());
    }).detach();
}

bool UserCommandHandler::httpGet(const std::string& url, std::string& out) const {
    CURL* c = curl_easy_init();
    if (!c) return false;
    out.clear();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode r = curl_easy_perform(c);
    curl_easy_cleanup(c);
    return (r == CURLE_OK);
}

bool UserCommandHandler::httpPostJson(const std::string& url, const std::string& body, std::string& out) const {
    CURL* c = curl_easy_init();
    if (!c) return false;
    out.clear();
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    CURLcode r = curl_easy_perform(c);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return (r == CURLE_OK);
}

bool UserCommandHandler::botAddStickerToSet(const std::string& token, int64_t userId,
                                             const std::string& name, const std::string& fileId,
                                             const std::string& emoji) {
    std::string url = "https://api.telegram.org/bot" + token + "/addStickerToSet";
    Json::Value body;
    body["user_id"] = userId;
    body["name"] = name;
    body["sticker"]["sticker"] = fileId;
    body["sticker"]["emoji_list"][0] = emoji;

    Json::FastWriter w;
    std::string resp;
    httpPostJson(url, w.write(body), resp);

    Json::CharReaderBuilder b;
    Json::Value out;
    std::string errs;
    std::istringstream ss(resp);
    if (Json::parseFromStream(b, ss, &out, &errs)) {
        return out.get("ok", false).asBool();
    }
    return false;
}

bool UserCommandHandler::botCreateStickerSet(const std::string& token, int64_t userId,
                                              const std::string& name, const std::string& title,
                                              const std::string& fileId, const std::string& emoji) {
    std::string url = "https://api.telegram.org/bot" + token + "/createNewStickerSet";
    Json::Value body;
    body["user_id"] = userId;
    body["name"] = name;
    body["title"] = title;
    body["sticker_format"] = "static";
    body["stickers"][0]["sticker"] = fileId;
    body["stickers"][0]["emoji_list"][0] = emoji;

    Json::FastWriter w;
    std::string resp;
    httpPostJson(url, w.write(body), resp);

    Json::CharReaderBuilder b;
    Json::Value out;
    std::string errs;
    std::istringstream ss(resp);
    if (Json::parseFromStream(b, ss, &out, &errs)) {
        return out.get("ok", false).asBool();
    }
    return false;
}

static std::vector<std::string> extractAllUrls(const std::string& text) {
    std::vector<std::string> urls;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t httpPos = text.find("http://", pos);
        size_t httpsPos = text.find("https://", pos);
        size_t start = std::string::npos;
        if (httpPos != std::string::npos && httpsPos != std::string::npos) {
            start = std::min(httpPos, httpsPos);
        } else if (httpPos != std::string::npos) {
            start = httpPos;
        } else if (httpsPos != std::string::npos) {
            start = httpsPos;
        } else {
            break;
        }

        size_t end = start;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
               text[end] != '"' && text[end] != '\'' && text[end] != '<' && text[end] != '>') {
            end++;
        }
        std::string url = text.substr(start, end - start);
        while (!url.empty() && (url.back() == '.' || url.back() == ',' || url.back() == ')' || url.back() == ';')) {
            url.pop_back();
        }
        if (!url.empty()) {
            urls.push_back(url);
        }
        pos = end;
    }
    return urls;
}

static std::string safeFilenameFromUrl(const std::string& url) {
    auto qPos = url.find('?');
    std::string clean = (qPos != std::string::npos) ? url.substr(0, qPos) : url;
    auto sPos = clean.rfind('/');
    std::string name = (sPos != std::string::npos && sPos + 1 < clean.size()) ? clean.substr(sPos + 1) : "downloaded_file";
    if (name.find('.') == std::string::npos) {
        name += ".bin";
    }
    return name;
}

void UserCommandHandler::cmdFind(int64_t chatId, const std::string& rawQuery) {
    std::string query = rawQuery;
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front()))) query.erase(query.begin());
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.back()))) query.pop_back();
    if (query.size() >= 2 && query.front() == '"' && query.back() == '"') {
        query = query.substr(1, query.size() - 2);
    }

    reply(chatId, "[*] Searching chat for: " + (query.empty() ? "all links and documents" : ("\"" + query + "\"")) + " ...");

    std::string lq = query;
    std::transform(lq.begin(), lq.end(), lq.begin(), [](unsigned char c) { return std::tolower(c); });

    Json::Value req;
    req["@type"] = "searchChatMessages";
    req["chat_id"] = chatId;
    req["limit"] = 50;
    req["from_message_id"] = 0;
    req["offset"] = 0;

    if (lq == "link" || lq == "links" || lq == "url" || lq == "urls") {
        req["query"] = "";
        Json::Value filter;
        filter["@type"] = "searchMessagesFilterUrl";
        req["filter"] = filter;
    } else if (lq == "doc" || lq == "document" || lq == "documents" || lq == "pdf") {
        req["query"] = (lq == "pdf" ? "pdf" : "");
        Json::Value filter;
        filter["@type"] = "searchMessagesFilterDocument";
        req["filter"] = filter;
    } else if (lq == "video" || lq == "videos" || lq == "mp4") {
        req["query"] = (lq == "mp4" ? "mp4" : "");
        Json::Value filter;
        filter["@type"] = "searchMessagesFilterVideo";
        req["filter"] = filter;
    } else {
        req["query"] = query;
        Json::Value filter;
        filter["@type"] = "searchMessagesFilterEmpty";
        req["filter"] = filter;
    }

    Json::Value res = m_client.sendSync(req, 10.0);
    std::vector<FoundItem> items;
    std::set<std::string> seenUrls;
    std::set<int32_t> seenFileIds;

    if (res.isMember("messages") && res["messages"].isArray()) {
        for (const auto& msg : res["messages"]) {
            int64_t mId = msg.get("id", 0).asInt64();
            if (!msg.isMember("content")) continue;
            const auto& cnt = msg["content"];
            std::string ctype = cnt.get("@type", "").asString();

            // Extract text/caption URLs
            std::string textContent = "";
            if (ctype == "messageText" && cnt.isMember("text")) {
                textContent = cnt["text"].get("text", "").asString();
            } else if (cnt.isMember("caption") && cnt["caption"].isMember("text")) {
                textContent = cnt["caption"].get("text", "").asString();
            }

            if (!textContent.empty()) {
                auto urls = extractAllUrls(textContent);
                for (const auto& u : urls) {
                    if (seenUrls.count(u) == 0) {
                        seenUrls.insert(u);
                        FoundItem item;
                        item.index = static_cast<int>(items.size()) + 1;
                        item.type = "link";
                        item.title = u;
                        item.url = u;
                        item.messageId = mId;
                        items.push_back(item);
                        if (items.size() >= 20) break;
                    }
                }
            }

            if (items.size() >= 20) break;

            if (ctype == "messageDocument" && cnt.isMember("document")) {
                const auto& doc = cnt["document"];
                std::string fname = doc.get("file_name", "document.bin").asString();
                int32_t fid = doc.isMember("document") ? doc["document"].get("id", 0).asInt() : 0;
                if (fid != 0 && seenFileIds.count(fid) == 0) {
                    seenFileIds.insert(fid);
                    FoundItem item;
                    item.index = static_cast<int>(items.size()) + 1;
                    item.type = "document";
                    item.title = fname;
                    item.fileId = fid;
                    item.messageId = mId;
                    items.push_back(item);
                }
            } else if (ctype == "messageVideo" && cnt.isMember("video")) {
                const auto& vid = cnt["video"];
                std::string fname = vid.get("file_name", "").asString();
                if (fname.empty()) fname = "video_" + std::to_string(mId) + ".mp4";
                int32_t fid = vid.isMember("video") ? vid["video"].get("id", 0).asInt() : 0;
                if (fid != 0 && seenFileIds.count(fid) == 0) {
                    seenFileIds.insert(fid);
                    FoundItem item;
                    item.index = static_cast<int>(items.size()) + 1;
                    item.type = "video";
                    item.title = fname;
                    item.fileId = fid;
                    item.messageId = mId;
                    items.push_back(item);
                }
            } else if (ctype == "messageAudio" && cnt.isMember("audio")) {
                const auto& aud = cnt["audio"];
                std::string title = aud.get("title", "").asString();
                if (title.empty()) title = aud.get("file_name", "audio.mp3").asString();
                int32_t fid = aud.isMember("audio") ? aud["audio"].get("id", 0).asInt() : 0;
                if (fid != 0 && seenFileIds.count(fid) == 0) {
                    seenFileIds.insert(fid);
                    FoundItem item;
                    item.index = static_cast<int>(items.size()) + 1;
                    item.type = "audio";
                    item.title = title;
                    item.fileId = fid;
                    item.messageId = mId;
                    items.push_back(item);
                }
            }
        }
    }

    if (items.empty()) {
        reply(chatId, "[*] No matching links or files found for " + (query.empty() ? "links/documents" : ("\"" + query + "\"")) + " in this chat.");
        return;
    }

    {
        std::lock_guard<std::mutex> lk(m_foundMutex);
        m_chatFoundItems[chatId] = items;
    }

    std::string out = "[*] Found " + std::to_string(items.size()) + " items matching " +
                      (query.empty() ? "links/documents" : ("\"" + query + "\"")) + ":\n\n";

    for (const auto& it : items) {
        out += "[" + std::to_string(it.index) + "] [" + it.type + "] " + it.title + "\n";
    }

    out += "\nChoose an action:\n"
           + m_prefix + "dl <number>   - Download and send in this chat\n"
           + m_prefix + "save <number> - Save to local ~/downloads folder\n"
           + m_prefix + "dl all       - Download and send all\n"
           + m_prefix + "save all     - Save all to ~/downloads\n";

    reply(chatId, out);
}

bool UserCommandHandler::processFoundItem(int64_t chatId, const FoundItem& item, bool sendToChat) {
    std::string actionName = sendToChat ? "Sending" : "Saving";
    int64_t msgId = replySync(chatId, "[*] [" + std::to_string(item.index) + "] Processing " + item.title + " (" + actionName + ") [   ]");

    std::atomic<bool> downloading{true};
    std::thread dlAnim([this, chatId, msgId, item, actionName, &downloading]() {
        const char* frames[] = { "[=  ]", "[== ]", "[===]", "[ ==]", "[  =]", "[   ]" };
        int i = 0;
        while (downloading.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            if (!downloading.load()) break;
            editMessage(chatId, msgId, "[*] [" + std::to_string(item.index) + "] Processing " + item.title + " (" + actionName + ") " + frames[i]);
            i = (i + 1) % 6;
        }
    });

    struct AnimGuard {
        std::atomic<bool>& d;
        std::thread& t;
        ~AnimGuard() {
            d.store(false);
            if (t.joinable()) t.join();
        }
    } guard{downloading, dlAnim};

    const char* home = std::getenv("HOME");
    std::string downloadsDir = home ? (std::string(home) + "/downloads") : "/data/data/com.termux/files/home/downloads";

    if (item.type == "link") {
        std::string tmpDir = "/tmp/";
        std::string filename = tmpDir + "skyien_find_" + std::to_string(chatId) + "_" + std::to_string(item.index) + ".mp4";
        ::remove(filename.c_str());

        // Try yt-dlp first
        std::string cmd = "yt-dlp -f 'best[ext=mp4]/best' -o " + shellEscape(filename) + " " + shellEscape(item.url) + " > /dev/null 2>&1";
        std::system(cmd.c_str());

        std::ifstream checkFile(filename, std::ios::binary | std::ios::ate);
        long long sz = checkFile.is_open() ? (long long)checkFile.tellg() : -1;
        checkFile.close();

        // If yt-dlp did not produce a file, try curl for direct file links (pdf, zip, apk, etc.)
        if (sz <= 0) {
            std::string curlFname = safeFilenameFromUrl(item.url);
            filename = tmpDir + "skyien_find_" + std::to_string(chatId) + "_" + curlFname;
            cmd = "curl -s -L -o " + shellEscape(filename) + " " + shellEscape(item.url);
            std::system(cmd.c_str());
            std::ifstream checkCurl(filename, std::ios::binary | std::ios::ate);
            sz = checkCurl.is_open() ? (long long)checkCurl.tellg() : -1;
            checkCurl.close();
        }

        if (sz <= 0) {
            editMessage(chatId, msgId, "[!] Failed to download content from link: " + item.url);
            return false;
        }

        if (sendToChat) {
            Json::Value req;
            req["@type"] = "sendMessage";
            req["chat_id"] = chatId;

            Json::Value content;
            Json::Value inputFile;
            inputFile["@type"] = "inputFileLocal";
            inputFile["path"] = filename;

            if (filename.find(".mp4") != std::string::npos) {
                content["@type"] = "inputMessageVideo";
                content["video"] = inputFile;
                content["supports_streaming"] = true;
            } else {
                content["@type"] = "inputMessageDocument";
                content["document"] = inputFile;
            }
            req["input_message_content"] = content;

            Json::Value res = m_client.sendSync(req, 120.0);
            if (res.get("@type", "").asString() != "message") {
                content["@type"] = "inputMessageDocument";
                content["document"] = inputFile;
                req["input_message_content"] = content;
                res = m_client.sendSync(req, 120.0);
            }
            std::thread([filename]() {
                std::this_thread::sleep_for(std::chrono::minutes(5));
                ::remove(filename.c_str());
            }).detach();
            
            if (res.get("@type", "").asString() == "message") {
                editMessage(chatId, msgId, "[*] Sent item #" + std::to_string(item.index) + " (" + item.title + ")");
                return true;
            } else {
                editMessage(chatId, msgId, "[!] Telegram failed to upload item #" + std::to_string(item.index));
                return false;
            }
        } else {
            std::string dest = downloadsDir + "/" + safeFilenameFromUrl(item.url);
            std::string mvCmd = "mv " + shellEscape(filename) + " " + shellEscape(dest);
            std::system(mvCmd.c_str());
            editMessage(chatId, msgId, "[*] Saved item #" + std::to_string(item.index) + " to:\n" + dest);
            return true;
        }
    } else if (item.fileId != 0) {
        // Telegram file in TDLib
        Json::Value dlReq;
        dlReq["@type"] = "downloadFile";
        dlReq["file_id"] = item.fileId;
        dlReq["priority"] = 32;
        dlReq["synchronous"] = true;
        Json::Value dlRes = m_client.sendSync(dlReq, 90.0);

        std::string localPath = "";
        if (dlRes.isMember("local") && dlRes["local"].isMember("path")) {
            localPath = dlRes["local"]["path"].asString();
        }

        if (localPath.empty()) {
            editMessage(chatId, msgId, "[!] Could not download Telegram file for item #" + std::to_string(item.index));
            return false;
        }

        if (sendToChat) {
            Json::Value req;
            req["@type"] = "sendMessage";
            req["chat_id"] = chatId;

            Json::Value content;
            Json::Value inputFile;
            inputFile["@type"] = "inputFileLocal";
            inputFile["path"] = localPath;

            if (item.type == "video") {
                content["@type"] = "inputMessageVideo";
                content["video"] = inputFile;
                content["supports_streaming"] = true;
            } else {
                content["@type"] = "inputMessageDocument";
                content["document"] = inputFile;
            }
            req["input_message_content"] = content;

            Json::Value res = m_client.sendSync(req, 120.0);
            if (res.get("@type", "").asString() == "message") {
                editMessage(chatId, msgId, "[*] Sent item #" + std::to_string(item.index) + " (" + item.title + ")");
                return true;
            } else {
                editMessage(chatId, msgId, "[!] Failed to send item #" + std::to_string(item.index));
                return false;
            }
        } else {
            std::string dest = downloadsDir + "/" + item.title;
            std::string cpCmd = "cp " + shellEscape(localPath) + " " + shellEscape(dest);
            std::system(cpCmd.c_str());
            editMessage(chatId, msgId, "[*] Saved item #" + std::to_string(item.index) + " to:\n" + dest);
            return true;
        }
    }

    return false;
}

void UserCommandHandler::cmdDlItem(int64_t chatId, const std::string& arg) {
    std::vector<FoundItem> items;
    {
        std::lock_guard<std::mutex> lk(m_foundMutex);
        if (m_chatFoundItems.find(chatId) != m_chatFoundItems.end()) {
            items = m_chatFoundItems[chatId];
        }
    }

    if (items.empty()) {
        reply(chatId, "[!] No search results available. Run " + m_prefix + "find <query> first.");
        return;
    }

    if (arg == "all") {
        reply(chatId, "[*] Downloading and sending all " + std::to_string(items.size()) + " items in this chat...");
        for (const auto& item : items) {
            processFoundItem(chatId, item, true);
        }
        reply(chatId, "[*] Finished processing all items.");
        return;
    }

    try {
        int idx = std::stoi(arg);
        if (idx >= 1 && idx <= static_cast<int>(items.size())) {
            processFoundItem(chatId, items[idx - 1], true);
            return;
        }
    } catch (...) {}

    reply(chatId, "[!] Invalid selection. Specify a number from 1 to " + std::to_string(items.size()) + ", or 'all'.");
}

void UserCommandHandler::cmdSaveItem(int64_t chatId, const std::string& arg) {
    std::vector<FoundItem> items;
    {
        std::lock_guard<std::mutex> lk(m_foundMutex);
        if (m_chatFoundItems.find(chatId) != m_chatFoundItems.end()) {
            items = m_chatFoundItems[chatId];
        }
    }

    if (items.empty()) {
        reply(chatId, "[!] No search results available. Run " + m_prefix + "find <query> first.");
        return;
    }

    if (arg == "all") {
        reply(chatId, "[*] Saving all " + std::to_string(items.size()) + " items to ~/downloads...");
        for (const auto& item : items) {
            processFoundItem(chatId, item, false);
        }
        reply(chatId, "[*] Finished saving all items.");
        return;
    }

    try {
        int idx = std::stoi(arg);
        if (idx >= 1 && idx <= static_cast<int>(items.size())) {
            processFoundItem(chatId, items[idx - 1], false);
            return;
        }
    } catch (...) {}

    reply(chatId, "[!] Invalid selection. Specify a number from 1 to " + std::to_string(items.size()) + ", or 'all'.");
}

void UserCommandHandler::cmdRayz(int64_t chatId, int64_t cmdMsgId, const std::string& args) {
    if (args.empty()) {
        m_rayzEnabled = !m_rayzEnabled;
        saveState();
        replyAndPurge(chatId, std::string("[*] Rayz Auto-Responder is now: ") + (m_rayzEnabled ? "ON" : "OFF") + " (Mode: " + m_rayzMode + ")", 5);
        return;
    }

    if (args == "stop") {
        m_rayzEnabled = false;
        saveState();
        replyAndPurge(chatId, "[*] Rayz has stopped all actions and is now silent.", 5);
        UI::printInfo("[RAYZ] Stopped and silenced completely by user command.");
        return;
    }

    if (args == "exception" || args == "ignore" || args == "block") {
        std::lock_guard<std::mutex> lk(m_rayzExceptionMutex);
        if (m_rayzExceptionChats.count(chatId)) {
            m_rayzExceptionChats.erase(chatId);
            saveState();
            replyAndPurge(chatId, "[*] Removed chat " + std::to_string(chatId) + " from Rayz exception list.", 5);
        } else {
            m_rayzExceptionChats.insert(chatId);
            saveState();
            replyAndPurge(chatId, "[*] Added chat " + std::to_string(chatId) + " to Rayz exception list. Rayz will NEVER reply here.", 5);
        }
        return;
    }

    if (args == "exception list" || args == "exceptions") {
        std::string listStr = "[*] Rayz Exception Chats:\n";
        std::lock_guard<std::mutex> lk(m_rayzExceptionMutex);
        if (m_rayzExceptionChats.empty()) {
            listStr += "None";
        } else {
            for (auto cid : m_rayzExceptionChats) {
                listStr += "- " + std::to_string(cid) + "\n";
            }
        }
        replyAndPurge(chatId, listStr, 10);
        return;
    }

    if (args.rfind("mode ", 0) == 0) {
        std::string mode = args.substr(5);
        if (mode == "always" || mode == "offline") {
            m_rayzMode = mode;
            saveState();
            replyAndPurge(chatId, "[*] Rayz Auto-Responder mode set to: " + mode, 5);
            return;
        } else {
            replyAndPurge(chatId, "[!] Invalid mode. Use 'always' or 'offline'.", 5);
            return;
        }
    }

    // Direct Rayz Query
    std::thread([this, chatId, cmdMsgId, args]() {
        UI::printInfo("Rayz is thinking about direct prompt: " + args);
        std::string url = "https://router.huggingface.co/v1/chat/completions";
        Json::Value req;
        req["model"] = "Qwen/Qwen2.5-72B-Instruct";
        req["temperature"] = 0.85;
        req["top_p"] = 0.9;
        req["max_tokens"] = 512;
        
        Json::Value msgs(Json::arrayValue);

        std::string sysPrompt = "You are texting on Telegram from your personal account (nickname rayz). Chat naturally, freely, and authentically like a real person.\n"
                                "- Language: You fully understand all languages, especially Hindi, Hinglish (Hindi written in English alphabet), casual street slang, and English. Always reply in the exact same language, slang, and style the other person is speaking (if they speak Hinglish, reply in natural Hinglish; if Hindi, in Hindi; if English, in English).\n"
                                "- Flow: Never start with generic repetitive greetings like 'hey whats up' or 'hello' unless the person specifically just greeted you. Jump straight into the topic or react directly to what they said.\n"
                                "- Persona: Be casual, witty, expressive, and human. Never sound like an AI assistant. Never offer customer support or say 'how can I help'. Just text like a real friend.";
        Json::Value sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = sysPrompt;
        msgs.append(sysMsg);

        // Load conversation history
        {
            std::lock_guard<std::mutex> lk(m_rayzHistoryMutex);
            auto& hist = m_rayzChatHistory[chatId];
            hist.push_back({ "user", args });
            if (hist.size() > 10) {
                hist.erase(hist.begin(), hist.begin() + (hist.size() - 10));
            }
            for (const auto& item : hist) {
                Json::Value m;
                m["role"] = item.role;
                m["content"] = item.text;
                msgs.append(m);
            }
        }
        saveChatHistory();

        req["messages"] = msgs;
        
        Json::FastWriter writer;
        std::string body = writer.write(req);
        
        std::string out;
        long http_code = 0;
        int maxRetries = 10;
        while (maxRetries > 0) {
            out.clear();
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            std::string auth = "Authorization: Bearer " + m_hfToken;
            headers = curl_slist_append(headers, auth.c_str());
            
            CURL* c = curl_easy_init();
            if (!c) return;
            
            curl_easy_setopt(c, CURLOPT_URL, url.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
            
            std::string proxyStr;
            std::string proxyAuth;
            if (m_proxyEnabled && m_activeProxy >= 0 && m_activeProxy < static_cast<int>(m_proxyList.size())) {
                const auto& proxy = m_proxyList[m_activeProxy];
                std::string pScheme = (proxy.type == "socks5") ? "socks5h://" : proxy.type + "://";
                proxyStr = pScheme + proxy.host + ":" + std::to_string(proxy.port);
                curl_easy_setopt(c, CURLOPT_PROXY, proxyStr.c_str());
                if (!proxy.user.empty() || !proxy.pass.empty()) {
                    proxyAuth = proxy.user + ":" + proxy.pass;
                    curl_easy_setopt(c, CURLOPT_PROXYUSERPWD, proxyAuth.c_str());
                }
            }
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
                data->append((char*)ptr, size * nmemb);
                return size * nmemb;
            });
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
            curl_easy_perform(c);
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
            curl_slist_free_all(headers);
            curl_easy_cleanup(c);
            
            if (http_code == 503) {
                UI::printWarning("Rayz model is loading. Waiting 5s...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
                maxRetries--;
            } else {
                break;
            }
        }
        
        Json::Value res;
        Json::Reader reader;
        if (http_code == 200 && reader.parse(out, res) && res.isMember("choices") && res["choices"].isArray() && res["choices"].size() > 0) {
            std::string replyText = res["choices"][0]["message"]["content"].asString();
            if (!replyText.empty()) {
                {
                    std::lock_guard<std::mutex> lk(m_rayzHistoryMutex);
                    m_rayzChatHistory[chatId].push_back({ "assistant", replyText });
                }
                saveChatHistory();

                UI::printSuccess("Rayz replied directly in Chat " + std::to_string(chatId));
                Json::Value sReq;
                sReq["@type"] = "sendMessage";
                sReq["chat_id"] = chatId;
                if (cmdMsgId != 0) {
                    sReq["reply_to_message_id"] = cmdMsgId;
                    Json::Value replyTo;
                    replyTo["@type"] = "inputMessageReplyToMessage";
                    replyTo["message_id"] = cmdMsgId;
                    sReq["reply_to"] = replyTo;
                }
                Json::Value content;
                content["@type"] = "inputMessageText";
                Json::Value txt;
                txt["@type"] = "formattedText";
                txt["text"] = replyText;
                content["text"] = txt;
                sReq["input_message_content"] = content;
                m_client.sendAsync(sReq);
            }
        } else {
            UI::printError("Rayz API error (" + std::to_string(http_code) + "): " + out);
        }
    }).detach();
}

void UserCommandHandler::cmdHeyRayz(int64_t chatId, int64_t msgId, const std::string& prompt) {
    if (prompt.empty()) return;
    cmdRayz(chatId, msgId, prompt);
}

void UserCommandHandler::handleRayzIncoming(int64_t chatId, int64_t msgId, const std::string& text) {
    if (!m_rayzEnabled || text.empty()) return;

    std::thread([this, chatId, msgId, text]() {
        UI::printInfo("Rayz is thinking... (Chat: " + std::to_string(chatId) + ")");
        std::string url = "https://router.huggingface.co/v1/chat/completions";
        Json::Value req;
        req["model"] = "Qwen/Qwen2.5-72B-Instruct";
        req["temperature"] = 0.85;
        req["top_p"] = 0.9;
        req["max_tokens"] = 512;
        
        Json::Value msgs(Json::arrayValue);

        std::string sysPrompt = "You are texting on Telegram from your personal account (nickname rayz). Chat naturally, freely, and authentically like a real person.\n"
                                "- Language: You fully understand all languages, especially Hindi, Hinglish (Hindi written in English alphabet), casual street slang, and English. Always reply in the exact same language, slang, and style the other person is speaking (if they speak Hinglish, reply in natural Hinglish; if Hindi, in Hindi; if English, in English).\n"
                                "- Flow: Never start with generic repetitive greetings like 'hey whats up' or 'hello' unless the person specifically just greeted you. Jump straight into the topic or react directly to what they said.\n"
                                "- Persona: Be casual, witty, expressive, and human. Never sound like an AI assistant. Never offer customer support or say 'how can I help'. Just text like a real friend.";
        Json::Value sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = sysPrompt;
        msgs.append(sysMsg);

        // Load and update conversation history
        {
            std::lock_guard<std::mutex> lk(m_rayzHistoryMutex);
            auto& hist = m_rayzChatHistory[chatId];
            hist.push_back({ "user", text });
            if (hist.size() > 10) {
                hist.erase(hist.begin(), hist.begin() + (hist.size() - 10));
            }
            for (const auto& item : hist) {
                Json::Value m;
                m["role"] = item.role;
                m["content"] = item.text;
                msgs.append(m);
            }
        }
        saveChatHistory();

        req["messages"] = msgs;
        
        Json::FastWriter writer;
        std::string body = writer.write(req);
        
        std::string out;
        long http_code = 0;
        int maxRetries = 10;
        while (maxRetries > 0) {
            out.clear();
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            std::string auth = "Authorization: Bearer " + m_hfToken;
            headers = curl_slist_append(headers, auth.c_str());
            
            CURL* c = curl_easy_init();
            if (!c) return;
            
            curl_easy_setopt(c, CURLOPT_URL, url.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
            
            // PROXY SUPPORT FOR C++ CURL
            std::string proxyStr;
            std::string proxyAuth;
            if (m_proxyEnabled && m_activeProxy >= 0 && m_activeProxy < static_cast<int>(m_proxyList.size())) {
                const auto& proxy = m_proxyList[m_activeProxy];
                std::string pScheme = (proxy.type == "socks5") ? "socks5h://" : proxy.type + "://";
                proxyStr = pScheme + proxy.host + ":" + std::to_string(proxy.port);
                curl_easy_setopt(c, CURLOPT_PROXY, proxyStr.c_str());
                if (!proxy.user.empty() || !proxy.pass.empty()) {
                    proxyAuth = proxy.user + ":" + proxy.pass;
                    curl_easy_setopt(c, CURLOPT_PROXYUSERPWD, proxyAuth.c_str());
                }
            }
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
                data->append((char*)ptr, size * nmemb);
                return size * nmemb;
            });
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
            curl_easy_perform(c);
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
            curl_slist_free_all(headers);
            curl_easy_cleanup(c);
            
            if (http_code == 503) {
                UI::printWarning("Rayz model is loading. Waiting 5s...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
                maxRetries--;
            } else {
                break;
            }
        }
        
        Json::Value res;
        Json::Reader reader;
        if (http_code == 200 && reader.parse(out, res) && res.isMember("choices") && res["choices"].isArray() && res["choices"].size() > 0) {
            std::string replyText = res["choices"][0]["message"]["content"].asString();
            if (!replyText.empty()) {
                // Record assistant reply in conversation history
                {
                    std::lock_guard<std::mutex> lk(m_rayzHistoryMutex);
                    m_rayzChatHistory[chatId].push_back({ "assistant", replyText });
                }
                saveChatHistory();

                UI::printSuccess("Rayz replied in Chat " + std::to_string(chatId));

                // Hook and reply directly to the incoming message
                Json::Value sReq;
                sReq["@type"] = "sendMessage";
                sReq["chat_id"] = chatId;
                if (msgId != 0) {
                    sReq["reply_to_message_id"] = msgId;
                    Json::Value replyTo;
                    replyTo["@type"] = "inputMessageReplyToMessage";
                    replyTo["message_id"] = msgId;
                    sReq["reply_to"] = replyTo;
                }
                Json::Value content;
                content["@type"] = "inputMessageText";
                Json::Value txt;
                txt["@type"] = "formattedText";
                txt["text"] = replyText;
                content["text"] = txt;
                sReq["input_message_content"] = content;
                m_client.sendAsync(sReq);
            }
        } else {
            UI::printError("Rayz API error (" + std::to_string(http_code) + "): " + out);
        }
    }).detach();
}

void UserCommandHandler::cmdRayimage(int64_t chatId, int64_t cmdMsgId, const std::string& prompt, bool showStatus) {
    if (prompt.empty()) {
        if (showStatus) replyAndPurge(chatId, "[!] Usage: " + m_prefix + "rayimage <prompt>", 5);
        return;
    }

    int64_t statusMsgId = 0;
    if (showStatus) {
        statusMsgId = replySync(chatId, "[*] Generating image: " + prompt + " [wait...]");
    }

    std::thread([this, chatId, cmdMsgId, statusMsgId, prompt]() {
        UI::printInfo("Generating image for: " + prompt);
        std::string url = "https://router.huggingface.co/hf-inference/models/Qwen/Qwen-Image-2.1";
        Json::Value req;
        req["inputs"] = prompt;
        
        Json::FastWriter writer;
        std::string body = writer.write(req);
        
        std::string out;
        long http_code = 0;
        int maxRetries = 3;
        while (maxRetries > 0) {
            out.clear();
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            std::string auth = "Authorization: Bearer " + m_hfToken;
            headers = curl_slist_append(headers, auth.c_str());
            
            CURL* c = curl_easy_init();
            if (!c) { 
                if (statusMsgId != 0) deleteMsg(chatId, statusMsgId); 
                return; 
            }
            
            curl_easy_setopt(c, CURLOPT_URL, url.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
            
            std::string proxyStr;
            std::string proxyAuth;
            if (m_proxyEnabled && m_activeProxy >= 0 && m_activeProxy < static_cast<int>(m_proxyList.size())) {
                const auto& proxy = m_proxyList[m_activeProxy];
                std::string pScheme = (proxy.type == "socks5") ? "socks5h://" : proxy.type + "://";
                proxyStr = pScheme + proxy.host + ":" + std::to_string(proxy.port);
                curl_easy_setopt(c, CURLOPT_PROXY, proxyStr.c_str());
                if (!proxy.user.empty() || !proxy.pass.empty()) {
                    proxyAuth = proxy.user + ":" + proxy.pass;
                    curl_easy_setopt(c, CURLOPT_PROXYUSERPWD, proxyAuth.c_str());
                }
            }
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
                data->append((char*)ptr, size * nmemb);
                return size * nmemb;
            });
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
            curl_easy_perform(c);
            
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
            curl_slist_free_all(headers);
            curl_easy_cleanup(c);
            
            if (http_code == 503) {
                UI::printWarning("Image model is loading. Waiting 5s...");
                if (statusMsgId != 0) {
                    editMessage(chatId, statusMsgId, "[*] Generating image: " + prompt + " [model loading... " + std::to_string(maxRetries) + "]");
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
                maxRetries--;
            } else {
                break;
            }
        }
        
        if (http_code != 200 || out.empty() || out.find("error") != std::string::npos) {
            UI::printInfo("HF image gen unavailable, using high-speed fallback for: " + prompt);
            out.clear();
            CURL* esc = curl_easy_init();
            char* escaped = curl_easy_escape(esc, prompt.c_str(), prompt.length());
            std::string encoded = escaped ? escaped : prompt;
            if (escaped) curl_free(escaped);
            curl_easy_cleanup(esc);

            std::string fallbackUrl = "https://image.pollinations.ai/prompt/" + encoded + "?nologo=true";
            CURL* c2 = curl_easy_init();
            if (c2) {
                curl_easy_setopt(c2, CURLOPT_URL, fallbackUrl.c_str());
                curl_easy_setopt(c2, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
                    data->append((char*)ptr, size * nmemb);
                    return size * nmemb;
                });
                curl_easy_setopt(c2, CURLOPT_WRITEDATA, &out);
                curl_easy_setopt(c2, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(c2, CURLOPT_TIMEOUT, 30L);
                curl_easy_perform(c2);
                curl_easy_getinfo(c2, CURLINFO_RESPONSE_CODE, &http_code);
                curl_easy_cleanup(c2);
            }
        }
        
        if (http_code == 200 && !out.empty()) {
            const char* home = std::getenv("HOME");
            std::string tmpDir = (home ? std::string(home) : "/tmp") + "/downloads/Skyien_Media/";
            std::system(("mkdir -p " + tmpDir).c_str());
            std::string filename = tmpDir + "rayimage_" + std::to_string(chatId) + ".jpg";
            
            std::ofstream f(filename, std::ios::binary);
            f.write(out.data(), out.size());
            f.close();
            
            Json::Value sReq;
            sReq["@type"] = "sendMessage";
            sReq["chat_id"] = chatId;
            if (cmdMsgId != 0) {
                sReq["reply_to_message_id"] = cmdMsgId;
                Json::Value replyTo;
                replyTo["@type"] = "inputMessageReplyToMessage";
                replyTo["message_id"] = cmdMsgId;
                sReq["reply_to"] = replyTo;
            }
            
            Json::Value content;
            content["@type"] = "inputMessagePhoto";
            Json::Value inputFile;
            inputFile["@type"] = "inputFileLocal";
            inputFile["path"] = filename;
            content["photo"] = inputFile;
            
            Json::Value txt;
            txt["@type"] = "formattedText";
            txt["text"] = "Generated: " + prompt;
            content["caption"] = txt;
            
            sReq["input_message_content"] = content;
            
            m_client.sendSync(sReq, 60.0);
            if (statusMsgId != 0) {
                deleteMsg(chatId, statusMsgId);
            }
            UI::printSuccess("Image generated successfully!");
            
            std::this_thread::sleep_for(std::chrono::seconds(60));
            ::remove(filename.c_str());
        } else {
            if (statusMsgId != 0) {
                editMessage(chatId, statusMsgId, "[!] Failed to generate image. Try again later.");
            }
            UI::printError("Image gen failed: " + std::to_string(http_code));
        }
    }).detach();
}

} // namespace Skyien


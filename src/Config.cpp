#include "Config.hpp"
#include <json/json.h>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace Skyien {

Config::Config() {
    sessionDirectory = getDefaultSessionPath();
}

std::string Config::getDefaultConfigPath() {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : "/data/data/com.termux/files/home";
    return base + "/.skyien/config.json";
}

std::string Config::getDefaultSessionPath() {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : "/data/data/com.termux/files/home";
    return base + "/.skyien/tdlib_data";
}

std::string Config::getDefaultFinderStatePath() {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : "/data/data/com.termux/files/home";
    return base + "/.skyien/finder_state.json";
}

void Config::ensureDirectories() const {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : "/data/data/com.termux/files/home";
    std::string skyienDir = base + "/.skyien";
    mkdir(skyienDir.c_str(), 0700);
    mkdir(sessionDirectory.c_str(), 0700);
    std::string dataDir = sessionDirectory + "/data";
    mkdir(dataDir.c_str(), 0700);
    std::string filesDir = sessionDirectory + "/files";
    mkdir(filesDir.c_str(), 0700);
}

bool Config::isValid() const {
    return true;
}

bool Config::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, file, &root, &errs)) {
        return false;
    }

    if (root.isMember("api_id")) apiId = root["api_id"].asInt();
    if (root.isMember("api_hash")) apiHash = root["api_hash"].asString();
    if (root.isMember("phone_number")) phoneNumber = root["phone_number"].asString();
    if (root.isMember("session_directory")) sessionDirectory = root["session_directory"].asString();

    // Bot settings
    if (root.isMember("bot_token")) botToken = root["bot_token"].asString();
    if (root.isMember("bot_username")) botUsername = root["bot_username"].asString();
    if (root.isMember("bot_id")) botId = root["bot_id"].asInt64();

    // Channel and Group settings
    if (root.isMember("connected_channel_id")) connectedChannelId = root["connected_channel_id"].asInt64();
    if (root.isMember("connected_channel_title")) connectedChannelTitle = root["connected_channel_title"].asString();
    if (root.isMember("connected_group_id")) connectedGroupId = root["connected_group_id"].asInt64();
    if (root.isMember("connected_group_title")) connectedGroupTitle = root["connected_group_title"].asString();

    // Automation settings
    if (root.isMember("auto_reply_enabled")) autoReplyEnabled = root["auto_reply_enabled"].asBool();
    if (root.isMember("afk_mode")) afkMode = root["afk_mode"].asBool();
    if (root.isMember("afk_message")) afkMessage = root["afk_message"].asString();
    if (root.isMember("auto_read_messages")) autoReadMessages = root["auto_read_messages"].asBool();
    if (root.isMember("relay_to_channel")) relayToChannel = root["relay_to_channel"].asBool();
    if (root.isMember("relay_to_group")) relayToGroup = root["relay_to_group"].asBool();
    if (root.isMember("relay_to_bot")) relayToBot = root["relay_to_bot"].asBool();

    if (root.isMember("auto_reply_rules") && root["auto_reply_rules"].isArray()) {
        autoReplyRules.clear();
        for (const auto& item : root["auto_reply_rules"]) {
            AutoReplyRule rule;
            rule.trigger = item.get("trigger", "").asString();
            rule.matchType = item.get("match_type", "contains").asString();
            rule.response = item.get("response", "").asString();
            if (item.isMember("allowed_chats") && item["allowed_chats"].isArray()) {
                for (const auto& c : item["allowed_chats"]) {
                    rule.allowedChats.push_back(c.asInt64());
                }
            }
            if (!rule.trigger.empty() && !rule.response.empty()) {
                autoReplyRules.push_back(rule);
            }
        }
    }

    if (root.isMember("forward_rules") && root["forward_rules"].isArray()) {
        forwardRules.clear();
        for (const auto& item : root["forward_rules"]) {
            ForwardRule rule;
            rule.fromChatId = item.get("from_chat_id", 0).asInt64();
            rule.toChatId = item.get("to_chat_id", 0).asInt64();
            if (rule.fromChatId != 0 && rule.toChatId != 0) {
                forwardRules.push_back(rule);
            }
        }
    }

    return true;
}

bool Config::saveToFile(const std::string& path) const {
    ensureDirectories();
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    Json::Value root;
    root["api_id"] = apiId;
    root["api_hash"] = apiHash;
    root["phone_number"] = phoneNumber;
    root["session_directory"] = sessionDirectory;

    root["bot_token"] = botToken;
    root["bot_username"] = botUsername;
    root["bot_id"] = botId;

    root["connected_channel_id"] = connectedChannelId;
    root["connected_channel_title"] = connectedChannelTitle;
    root["connected_group_id"] = connectedGroupId;
    root["connected_group_title"] = connectedGroupTitle;

    root["auto_reply_enabled"] = autoReplyEnabled;
    root["afk_mode"] = afkMode;
    root["afk_message"] = afkMessage;
    root["auto_read_messages"] = autoReadMessages;
    root["relay_to_channel"] = relayToChannel;
    root["relay_to_group"] = relayToGroup;
    root["relay_to_bot"] = relayToBot;

    Json::Value rulesArray(Json::arrayValue);
    for (const auto& rule : autoReplyRules) {
        Json::Value r;
        r["trigger"] = rule.trigger;
        r["match_type"] = rule.matchType;
        r["response"] = rule.response;
        Json::Value chats(Json::arrayValue);
        for (auto id : rule.allowedChats) {
            chats.append(id);
        }
        r["allowed_chats"] = chats;
        rulesArray.append(r);
    }
    root["auto_reply_rules"] = rulesArray;

    Json::Value fwdArray(Json::arrayValue);
    for (const auto& fwd : forwardRules) {
        Json::Value f;
        f["from_chat_id"] = fwd.fromChatId;
        f["to_chat_id"] = fwd.toChatId;
        fwdArray.append(f);
    }
    root["forward_rules"] = fwdArray;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    file << Json::writeString(writer, root);
    return true;
}

} // namespace Skyien

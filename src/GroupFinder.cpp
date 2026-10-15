#include "GroupFinder.hpp"
#include "TerminalUI.hpp"
#include "EmojiFilter.hpp"
#include "Config.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>
#include <random>
#include <chrono>
#include <thread>
#include <algorithm>
#include <csignal>

namespace Skyien {

// External signal from main.cpp
extern std::atomic<bool> g_stopSignal;

// ---------------------------------------------------------------------------
// PacingConfig Implementation
// ---------------------------------------------------------------------------

int32_t PacingConfig::getNextDelaySeconds() const {
    switch (mode) {
        case PacingMode::MIN_10_MINUTES:
            return 600; // 10 minutes = 600s
        case PacingMode::RANDOM_4M_TO_1H: {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<int32_t> dist(240, 3600); // 4 min to 1 hr
            return dist(gen);
        }
        case PacingMode::CUSTOM_FIXED:
            return std::max(1, customFixedSec);
        case PacingMode::CUSTOM_RANDOM: {
            int32_t minS = std::max(1, std::min(customMinSec, customMaxSec));
            int32_t maxS = std::max(minS, std::max(customMinSec, customMaxSec));
            if (minS >= maxS) return minS;
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<int32_t> dist(minS, maxS);
            return dist(gen);
        }
    }
    return 600;
}

std::string PacingConfig::getModeDescription() const {
    switch (mode) {
        case PacingMode::MIN_10_MINUTES:
            return "Safe Minimum (10 minutes fixed)";
        case PacingMode::RANDOM_4M_TO_1H:
            return "Human Random (4 minutes to 1 hour)";
        case PacingMode::CUSTOM_FIXED:
            return "Custom Fixed (" + UI::formatDuration(customFixedSec) + ")";
        case PacingMode::CUSTOM_RANDOM:
            return "Custom Random (" + UI::formatDuration(customMinSec) + " to " + UI::formatDuration(customMaxSec) + ")";
    }
    return "Unknown";
}

Json::Value PacingConfig::toJson() const {
    Json::Value v;
    std::string mStr = "min10";
    if (mode == PacingMode::RANDOM_4M_TO_1H) mStr = "random_4m_1h";
    else if (mode == PacingMode::CUSTOM_FIXED) mStr = "custom_fixed";
    else if (mode == PacingMode::CUSTOM_RANDOM) mStr = "custom_random";

    v["mode"] = mStr;
    v["custom_fixed_sec"] = customFixedSec;
    v["custom_min_sec"] = customMinSec;
    v["custom_max_sec"] = customMaxSec;
    return v;
}

PacingConfig PacingConfig::fromJson(const Json::Value& val) {
    PacingConfig cfg;
    if (!val.isObject()) return cfg;

    std::string mStr = val.get("mode", "min10").asString();
    if (mStr == "random_4m_1h") cfg.mode = PacingMode::RANDOM_4M_TO_1H;
    else if (mStr == "custom_fixed") cfg.mode = PacingMode::CUSTOM_FIXED;
    else if (mStr == "custom_random") cfg.mode = PacingMode::CUSTOM_RANDOM;
    else cfg.mode = PacingMode::MIN_10_MINUTES;

    cfg.customFixedSec = val.get("custom_fixed_sec", 600).asInt();
    cfg.customMinSec = val.get("custom_min_sec", 240).asInt();
    cfg.customMaxSec = val.get("custom_max_sec", 3600).asInt();
    return cfg;
}

void PacingConfig::configureInteractive() {
    std::cout << "\n" << UI::CYAN << UI::BOLD << "[=== Pacing & Delay Configuration ===]" << UI::RESET << "\n";
    std::cout << " Current Pacing: " << UI::BOLD << getModeDescription() << UI::RESET << "\n\n";
    std::cout << " Select pacing profile between joins:\n";
    std::cout << "  [1] Minimum 10 Minutes (600s) - Safe standard gap\n";
    std::cout << "  [2] Random Interval (4 min to 1 hour) - Natural unpredictable gap\n";
    std::cout << "  [3] Custom Fixed Delay - Specify exact interval\n";
    std::cout << "  [4] Custom Random Interval - Specify min & max bounds\n";
    std::cout << "  [0] Keep current setting\n\n";

    std::string choice = UI::prompt("Select option [0-4]");
    if (choice == "1") {
        mode = PacingMode::MIN_10_MINUTES;
        UI::printSuccess("Pacing profile set to: Minimum 10 Minutes.");
    } else if (choice == "2") {
        mode = PacingMode::RANDOM_4M_TO_1H;
        UI::printSuccess("Pacing profile set to: Random Interval (4 min to 1 hour).");
    } else if (choice == "3") {
        mode = PacingMode::CUSTOM_FIXED;
        std::cout << "\nEnter custom delay:\n";
        std::string unit = UI::prompt("Unit: [s]econds, [m]inutes, or [h]ours (default: m)");
        int mult = 60;
        if (!unit.empty() && (unit[0] == 'h' || unit[0] == 'H')) mult = 3600;
        else if (!unit.empty() && (unit[0] == 's' || unit[0] == 'S')) mult = 1;

        std::string valStr = UI::prompt("Duration value");
        try {
            double val = std::stod(valStr);
            customFixedSec = std::max(1, static_cast<int32_t>(val * mult));
            UI::printSuccess("Custom fixed delay set to: " + UI::formatDuration(customFixedSec));
        } catch (...) {
            UI::printWarning("Invalid input. Keeping " + UI::formatDuration(customFixedSec));
        }
    } else if (choice == "4") {
        mode = PacingMode::CUSTOM_RANDOM;
        std::cout << "\nEnter custom random interval bounds:\n";
        std::string unit = UI::prompt("Unit: [s]econds, [m]inutes, or [h]ours (default: m)");
        int mult = 60;
        if (!unit.empty() && (unit[0] == 'h' || unit[0] == 'H')) mult = 3600;
        else if (!unit.empty() && (unit[0] == 's' || unit[0] == 'S')) mult = 1;

        std::string minStr = UI::prompt("Minimum gap");
        std::string maxStr = UI::prompt("Maximum gap");
        try {
            double minV = std::stod(minStr);
            double maxV = std::stod(maxStr);
            int32_t sMin = static_cast<int32_t>(minV * mult);
            int32_t sMax = static_cast<int32_t>(maxV * mult);
            if (sMin > sMax) std::swap(sMin, sMax);
            customMinSec = std::max(1, sMin);
            customMaxSec = std::max(customMinSec, sMax);
            UI::printSuccess("Custom random interval set: " + UI::formatDuration(customMinSec) + " to " + UI::formatDuration(customMaxSec));
        } catch (...) {
            UI::printWarning("Invalid input. Keeping default bounds.");
        }
    }
}

// ---------------------------------------------------------------------------
// Serialization Implementations
// ---------------------------------------------------------------------------

Json::Value GroupSearchResult::toJson() const {
    Json::Value v;
    v["chat_id"] = static_cast<Json::Int64>(chatId);
    v["title"] = title;
    v["username"] = username;
    v["member_count"] = memberCount;
    v["online_count"] = onlineCount;
    v["can_send_messages"] = canSendMessages;
    v["is_supergroup"] = isSupergroup;
    v["is_channel"] = isChannel;
    v["type_label"] = typeLabel;
    v["already_joined"] = alreadyJoined;
    return v;
}

GroupSearchResult GroupSearchResult::fromJson(const Json::Value& v) {
    GroupSearchResult r;
    r.chatId = v.get("chat_id", 0).asInt64();
    r.title = v.get("title", "").asString();
    r.username = v.get("username", "").asString();
    r.memberCount = v.get("member_count", 0).asInt();
    r.onlineCount = v.get("online_count", 0).asInt();
    r.canSendMessages = v.get("can_send_messages", true).asBool();
    r.isSupergroup = v.get("is_supergroup", true).asBool();
    r.isChannel = v.get("is_channel", false).asBool();
    r.typeLabel = v.get("type_label", "Group").asString();
    r.alreadyJoined = v.get("already_joined", false).asBool();
    return r;
}

Json::Value JoinedChatRecord::toJson() const {
    Json::Value v;
    v["chat_id"] = static_cast<Json::Int64>(chatId);
    v["title"] = title;
    v["username"] = username;
    v["type_label"] = typeLabel;
    v["joined_at"] = static_cast<Json::Int64>(joinedAt);
    return v;
}

JoinedChatRecord JoinedChatRecord::fromJson(const Json::Value& v) {
    JoinedChatRecord r;
    r.chatId = v.get("chat_id", 0).asInt64();
    r.title = v.get("title", "").asString();
    r.username = v.get("username", "").asString();
    r.typeLabel = v.get("type_label", "Group").asString();
    r.joinedAt = v.get("joined_at", 0).asInt64();
    return r;
}

Json::Value JoinQueueState::toJson() const {
    Json::Value v;
    v["in_progress"] = inProgress;
    v["current_index"] = currentIndex;

    Json::Value pArr(Json::arrayValue);
    for (const auto& p : pending) {
        pArr.append(p.toJson());
    }
    v["pending"] = pArr;

    Json::Value cArr(Json::arrayValue);
    for (const auto& c : completed) {
        cArr.append(c.toJson());
    }
    v["completed"] = cArr;

    return v;
}

JoinQueueState JoinQueueState::fromJson(const Json::Value& v) {
    JoinQueueState s;
    s.inProgress = v.get("in_progress", false).asBool();
    s.currentIndex = v.get("current_index", 0).asInt();

    if (v.isMember("pending") && v["pending"].isArray()) {
        for (const auto& item : v["pending"]) {
            s.pending.push_back(GroupSearchResult::fromJson(item));
        }
    }
    if (v.isMember("completed") && v["completed"].isArray()) {
        for (const auto& item : v["completed"]) {
            s.completed.push_back(JoinedChatRecord::fromJson(item));
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// GroupFinder Implementation
// ---------------------------------------------------------------------------

int32_t GroupFinder::parseQuantity(const std::string& input) {
    if (input.empty()) return 0;
    std::string s;
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            s.push_back(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (s.empty()) return 0;

    double mult = 1.0;
    if (s.back() == 'k') {
        mult = 1000.0;
        s.pop_back();
    } else if (s.back() == 'm') {
        mult = 1000000.0;
        s.pop_back();
    }

    try {
        double val = std::stod(s);
        return static_cast<int32_t>(val * mult);
    } catch (...) {
        return 0;
    }
}

GroupFinder::GroupFinder(TelegramClient& client, const std::string& stateFilePath)
    : m_client(client), m_stateFilePath(stateFilePath) {
    if (m_stateFilePath.empty()) {
        m_stateFilePath = Config::getDefaultFinderStatePath();
    }
    loadState();
}

void GroupFinder::loadState() {
    std::ifstream file(m_stateFilePath);
    if (!file.is_open()) return;

    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    if (!Json::parseFromStream(b, file, &root, &errs)) {
        return;
    }

    if (root.isMember("pacing")) {
        m_pacing = PacingConfig::fromJson(root["pacing"]);
    }
    if (root.isMember("joined_chats") && root["joined_chats"].isObject()) {
        m_joinedChats.clear();
        for (const auto& idStr : root["joined_chats"].getMemberNames()) {
            try {
                int64_t id = std::stoll(idStr);
                m_joinedChats[id] = JoinedChatRecord::fromJson(root["joined_chats"][idStr]);
            } catch (...) {}
        }
    }
    if (root.isMember("last_results") && root["last_results"].isArray()) {
        m_lastResults.clear();
        for (const auto& item : root["last_results"]) {
            m_lastResults.push_back(GroupSearchResult::fromJson(item));
        }
    }
    m_lastQuery = root.get("last_query", "").asString();
    m_lastAimMembers = root.get("last_aim_members", 0).asInt();

    if (root.isMember("join_queue")) {
        m_queueState = JoinQueueState::fromJson(root["join_queue"]);
    }
}

void GroupFinder::saveState() {
    Json::Value root;
    root["pacing"] = m_pacing.toJson();
    root["last_query"] = m_lastQuery;
    root["last_aim_members"] = m_lastAimMembers;

    Json::Value jObj(Json::objectValue);
    for (const auto& [id, record] : m_joinedChats) {
        jObj[std::to_string(id)] = record.toJson();
    }
    root["joined_chats"] = jObj;

    Json::Value resArr(Json::arrayValue);
    for (const auto& res : m_lastResults) {
        resArr.append(res.toJson());
    }
    root["last_results"] = resArr;

    root["join_queue"] = m_queueState.toJson();

    std::ofstream out(m_stateFilePath);
    if (out.is_open()) {
        Json::StreamWriterBuilder w;
        w["indentation"] = "  ";
        out << Json::writeString(w, root);
    }
}

bool GroupFinder::isChatInJoinedCache(int64_t chatId) const {
    return m_joinedChats.find(chatId) != m_joinedChats.end();
}

void GroupFinder::markChatJoined(int64_t chatId, const std::string& title, const std::string& username, const std::string& typeLabel) {
    JoinedChatRecord rec;
    rec.chatId = chatId;
    rec.title = title;
    rec.username = username;
    rec.typeLabel = typeLabel;
    rec.joinedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_joinedChats[chatId] = rec;
    saveState();
}

std::vector<GroupSearchResult> GroupFinder::search(const GroupFilterCriteria& criteria) {
    std::vector<GroupSearchResult> results;
    std::unordered_set<int64_t> seenChatIds;

    std::string infoMsg = "Searching groups & channels for '" + criteria.query + "'";
    if (criteria.maxMembers > 0) {
        infoMsg += " (Aim: 0 to " + std::to_string(criteria.maxMembers) + " members)";
    }
    if (criteria.targetOnline > 0) {
        infoMsg += " (~" + std::to_string(criteria.targetOnline) + " online)";
    }
    UI::printInfo(infoMsg + "...");

    std::vector<int64_t> candidateChatIds;

    // 1. Resolve direct username/link
    std::string cleanQuery = criteria.query;
    if (cleanQuery.rfind("https://t.me/", 0) == 0) cleanQuery = cleanQuery.substr(13);
    else if (cleanQuery.rfind("t.me/", 0) == 0) cleanQuery = cleanQuery.substr(5);
    if (!cleanQuery.empty() && cleanQuery[0] == '@') cleanQuery = cleanQuery.substr(1);

    if (cleanQuery.find(' ') == std::string::npos && !cleanQuery.empty()) {
        Json::Value pubReq;
        pubReq["@type"] = "searchPublicChat";
        pubReq["username"] = cleanQuery;
        Json::Value pubRes = m_client.sendSync(pubReq, 5.0);
        if (pubRes.get("@type", "").asString() == "chat") {
            int64_t id = pubRes.get("id", 0).asInt64();
            if (id != 0 && seenChatIds.insert(id).second) {
                candidateChatIds.push_back(id);
            }
        }
    }

    // 2. Global search for public chats
    Json::Value publicReq;
    publicReq["@type"] = "searchPublicChats";
    publicReq["query"] = criteria.query;
    Json::Value publicRes = m_client.sendSync(publicReq, 8.0);
    if (publicRes.get("@type", "").asString() == "chats" && publicRes.isMember("chat_ids")) {
        for (const auto& idVal : publicRes["chat_ids"]) {
            int64_t id = idVal.asInt64();
            if (seenChatIds.insert(id).second) {
                candidateChatIds.push_back(id);
            }
        }
    }

    // 3. Server-side chat search
    Json::Value srvReq;
    srvReq["@type"] = "searchChatsOnServer";
    srvReq["query"] = criteria.query;
    srvReq["limit"] = 50;
    Json::Value srvRes = m_client.sendSync(srvReq, 6.0);
    if (srvRes.get("@type", "").asString() == "chats" && srvRes.isMember("chat_ids")) {
        for (const auto& idVal : srvRes["chat_ids"]) {
            int64_t id = idVal.asInt64();
            if (seenChatIds.insert(id).second) {
                candidateChatIds.push_back(id);
            }
        }
    }

    // 4. Local cached chat search
    Json::Value localReq;
    localReq["@type"] = "searchChats";
    localReq["query"] = criteria.query;
    localReq["limit"] = 50;
    Json::Value localRes = m_client.sendSync(localReq, 4.0);
    if (localRes.get("@type", "").asString() == "chats" && localRes.isMember("chat_ids")) {
        for (const auto& idVal : localRes["chat_ids"]) {
            int64_t id = idVal.asInt64();
            if (seenChatIds.insert(id).second) {
                candidateChatIds.push_back(id);
            }
        }
    }

    if (candidateChatIds.empty()) {
        UI::printWarning("No candidate communities found from Telegram search.");
        return results;
    }

    UI::printInfo("Evaluating " + std::to_string(candidateChatIds.size()) + " candidate groups & channels (filtering joined & checking limits)...");

    for (int64_t chatId : candidateChatIds) {
        // First check local persistent joined registry
        if (criteria.excludeJoined && isChatInJoinedCache(chatId)) {
            continue; // Already joined: do not show!
        }

        // Live check if already joined in Telegram
        if (criteria.excludeJoined && m_client.isChatJoined(chatId)) {
            // Record in persistent registry to speed up future searches
            markChatJoined(chatId, "Joined Community", "", "Community");
            continue; // Already joined: do not show!
        }

        Json::Value chatReq;
        chatReq["@type"] = "getChat";
        chatReq["chat_id"] = chatId;
        Json::Value chat = m_client.sendSync(chatReq, 3.0);

        if (chat.get("@type", "").asString() != "chat" || !chat.isMember("type")) {
            continue;
        }

        std::string typeStr = chat["type"].get("@type", "").asString();
        int64_t supergroupId = 0;
        int64_t basicGroupId = 0;
        bool isChannel = false;
        std::string typeLabel = "Group";

        if (typeStr == "chatTypeSupergroup") {
            isChannel = chat["type"].get("is_channel", false).asBool();
            supergroupId = chat["type"].get("supergroup_id", 0).asInt64();
            typeLabel = isChannel ? "Channel" : "Supergroup";
        } else if (typeStr == "chatTypeBasicGroup") {
            basicGroupId = chat["type"].get("basic_group_id", 0).asInt64();
            typeLabel = "Group";
        } else {
            continue; // Skip private chats or secret chats
        }

        // Check channel / group inclusion criteria
        if (isChannel && !criteria.includeChannels) continue;
        if (!isChannel && !criteria.includeGroups) continue;

        // Permissions check
        bool canSend = true;
        if (chat.isMember("permissions")) {
            const auto& p = chat["permissions"];
            if (p.isMember("can_send_basic_messages")) {
                canSend = p["can_send_basic_messages"].asBool();
            } else if (p.isMember("can_send_messages")) {
                canSend = p["can_send_messages"].asBool();
            }
        }
        if (criteria.requireCanSend && !canSend) {
            continue;
        }

        int32_t memberCount = 0;
        int32_t onlineCount = 0;
        std::string username = "";

        if (supergroupId != 0) {
            Json::Value fullReq;
            fullReq["@type"] = "getSupergroupFullInfo";
            fullReq["supergroup_id"] = supergroupId;
            Json::Value fullRes = m_client.sendSync(fullReq, 4.0);
            if (fullRes.get("@type", "").asString() == "supergroupFullInfo") {
                memberCount = fullRes.get("member_count", 0).asInt();
                onlineCount = fullRes.get("online_member_count", 0).asInt();
            }

            Json::Value sgReq;
            sgReq["@type"] = "getSupergroup";
            sgReq["supergroup_id"] = supergroupId;
            Json::Value sgRes = m_client.sendSync(sgReq, 3.0);
            if (sgRes.isMember("usernames") && sgRes["usernames"].isMember("active_usernames")) {
                const auto& active = sgRes["usernames"]["active_usernames"];
                if (!active.empty()) username = active[0].asString();
            } else if (sgRes.isMember("username") && !sgRes["username"].asString().empty()) {
                username = sgRes["username"].asString();
            }
        } else if (basicGroupId != 0) {
            Json::Value fullReq;
            fullReq["@type"] = "getBasicGroupFullInfo";
            fullReq["basic_group_id"] = basicGroupId;
            Json::Value fullRes = m_client.sendSync(fullReq, 4.0);
            if (fullRes.get("@type", "").asString() == "basicGroupFullInfo") {
                memberCount = fullRes.get("members", Json::Value()).size();
            }
        }

        // Aim member filtering: "if i give aim 10k, it means 0 to 10k all groups"
        if (criteria.maxMembers > 0) {
            if (memberCount < criteria.minMembers || memberCount > criteria.maxMembers) {
                continue; // Skip: outside the 0 to 10k aim range!
            }
        }

        GroupSearchResult res;
        res.chatId = chatId;
        res.title = EmojiFilter::strip(chat.get("title", typeLabel).asString());
        res.username = username;
        res.memberCount = memberCount;
        res.onlineCount = onlineCount;
        res.canSendMessages = canSend;
        res.isSupergroup = (supergroupId != 0);
        res.isChannel = isChannel;
        res.typeLabel = typeLabel;
        res.alreadyJoined = false;

        results.push_back(res);
    }

    // Sort results: highest members up to the aim first
    std::sort(results.begin(), results.end(), [](const GroupSearchResult& a, const GroupSearchResult& b) {
        return a.memberCount > b.memberCount;
    });

    // Persist search results to JSON
    m_lastResults = results;
    m_lastQuery = criteria.query;
    m_lastAimMembers = criteria.maxMembers;
    saveState();

    return results;
}

void GroupFinder::printResults(const std::vector<GroupSearchResult>& list) {
    if (list.empty()) {
        std::cout << UI::YELLOW << "[!] No new unjoined groups or channels found matching criteria." << UI::RESET << "\n";
        return;
    }

    std::cout << "\n" << UI::CYAN << UI::BOLD << "+---------------------------------------------------------------------------------------------------------+" << UI::RESET << "\n";
    std::cout << UI::CYAN << UI::BOLD << "|                                      NEW UNJOINED COMMUNITIES FOUND                                      |" << UI::RESET << "\n";
    std::cout << UI::CYAN << UI::BOLD << "+---------------------------------------------------------------------------------------------------------+" << UI::RESET << "\n";
    std::cout << UI::BOLD
              << std::left << std::setw(6)  << "#"
              << std::left << std::setw(12) << "TYPE"
              << std::left << std::setw(16) << "CHAT ID"
              << std::left << std::setw(28) << "TITLE"
              << std::left << std::setw(10) << "MEMBERS"
              << std::left << std::setw(9)  << "ONLINE"
              << std::left << std::setw(11) << "SEND MSGS"
              << "USERNAME" << UI::RESET << "\n";
    std::cout << std::string(105, '-') << "\n";

    for (size_t i = 0; i < list.size(); ++i) {
        const auto& g = list[i];
        std::string titleTrunc = (g.title.size() > 25) ? g.title.substr(0, 22) + "..." : g.title;
        std::string userStr = g.username.empty() ? "-" : ("@" + g.username);
        std::string sendStr = g.canSendMessages ? (std::string(UI::GREEN) + "[YES]" + UI::RESET) : (std::string(UI::RED) + "[NO]" + UI::RESET);
        std::string typeBadge = g.isChannel ? (std::string(UI::MAGENTA) + "Channel" + UI::RESET) : (std::string(UI::BLUE) + g.typeLabel + UI::RESET);

        std::cout << std::left << std::setw(6)  << ("[" + std::to_string(i + 1) + "]")
                  << std::left << std::setw(20) << typeBadge
                  << std::left << std::setw(16) << g.chatId
                  << std::left << std::setw(28) << titleTrunc
                  << std::left << std::setw(10) << g.memberCount
                  << std::left << std::setw(9)  << (g.onlineCount > 0 ? (std::string(UI::YELLOW) + std::to_string(g.onlineCount) + UI::RESET) : "-")
                  << std::left << std::setw(19) << sendStr
                  << userStr << "\n";
    }
    std::cout << std::string(105, '-') << "\n";
    std::cout << "[+] Found " << UI::BOLD << list.size() << UI::RESET << " new unjoined communities (saved to state JSON).\n\n";
}

bool GroupFinder::countdownTimer(int32_t seconds, const std::string& nextTitle) {
    if (seconds <= 0) return true;

    std::cout << "\n" << UI::CYAN << "[*] Pacing gap: " << UI::BOLD << UI::formatDuration(seconds)
              << UI::RESET << " scheduled before joining: " << UI::BOLD << nextTitle << UI::RESET << "\n";
    std::cout << " " << UI::DIM << "[~] Tip: Press Ctrl+C to skip wait or abort queue" << UI::RESET << "\n";

    auto startTime = std::chrono::steady_clock::now();
    int32_t remaining = seconds;

    while (remaining > 0) {
        if (g_stopSignal.load()) {
            g_stopSignal.store(false);
            std::cout << "\r" << std::string(80, ' ') << "\r";
            std::cout << "\n" << UI::YELLOW << "[!] Timer paused by user." << UI::RESET << "\n";
            std::string ans = UI::prompt("[s]kip wait and join immediately, or [a]bort remaining queue (default: s)");
            if (ans.empty() || ans[0] == 's' || ans[0] == 'S') {
                UI::printInfo("Skipped wait. Joining now...");
                return true;
            } else {
                UI::printInfo("Join-all session paused. Progress preserved in JSON.");
                return false;
            }
        }

        std::string timeStr = UI::formatDuration(remaining);
        std::cout << "\r [*] Reverse countdown: [ " << UI::BOLD << UI::CYAN << timeStr << UI::RESET << " ] remaining... " << std::flush;

        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count();
        remaining = seconds - static_cast<int32_t>(elapsed);
    }

    std::cout << "\r" << std::string(80, ' ') << "\r";
    UI::printSuccess("Gap finished (" + UI::formatDuration(seconds) + "). Proceeding to next join...");
    return true;
}

bool GroupFinder::joinAll(const std::vector<GroupSearchResult>& groups) {
    if (groups.empty()) {
        UI::printWarning("No groups or channels to join.");
        return false;
    }

    m_queueState.inProgress = true;
    m_queueState.pending = groups;
    m_queueState.completed.clear();
    m_queueState.currentIndex = 0;
    saveState();

    std::cout << "\n" << UI::GREEN << UI::BOLD << "[=== Starting Join-All Engine ===]" << UI::RESET << "\n";
    std::cout << " Total Communities to Join: " << UI::BOLD << groups.size() << UI::RESET << "\n";
    std::cout << " Active Pacing Profile:     " << UI::BOLD << m_pacing.getModeDescription() << UI::RESET << "\n";
    std::cout << " State Persistence File:    " << UI::DIM << m_stateFilePath << UI::RESET << "\n\n";

    size_t total = groups.size();
    size_t successfulJoins = 0;

    for (size_t i = 0; i < total; ++i) {
        if (g_stopSignal.load()) {
            g_stopSignal.store(false);
            UI::printWarning("Join-all interrupted by user. Queue saved to JSON.");
            m_queueState.inProgress = false;
            saveState();
            return false;
        }

        const auto& target = groups[i];
        m_queueState.currentIndex = static_cast<int32_t>(i);
        saveState();

        std::string userStr = target.username.empty() ? "" : (" (@" + target.username + ")");
        std::cout << UI::BOLD << "\n[" << (i + 1) << "/" << total << "] " << UI::RESET
                  << "Joining " << target.typeLabel << ": " << UI::BOLD << target.title << UI::RESET
                  << userStr << " (ID: " << target.chatId << ")...\n";

        bool ok = m_client.joinChat(target.chatId);
        if (ok) {
            UI::printSuccess("Successfully joined " + target.title + "!");
            markChatJoined(target.chatId, target.title, target.username, target.typeLabel);
            successfulJoins++;

            JoinedChatRecord rec;
            rec.chatId = target.chatId;
            rec.title = target.title;
            rec.username = target.username;
            rec.typeLabel = target.typeLabel;
            rec.joinedAt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            m_queueState.completed.push_back(rec);
            saveState();
        } else {
            UI::printError("Could not join " + target.title + " (may require invite link, admin approval, or rate limited).");
        }

        // If more items remain in the queue, apply pacing gap with live countdown
        if (i + 1 < total) {
            const auto& nextTarget = groups[i + 1];
            int32_t delay = m_pacing.getNextDelaySeconds();
            bool proceed = countdownTimer(delay, nextTarget.title);
            if (!proceed) {
                // User chose to abort
                m_queueState.inProgress = false;
                saveState();
                return false;
            }
        }
    }

    m_queueState.inProgress = false;
    m_queueState.pending.clear();
    saveState();

    std::cout << "\n" << UI::GREEN << UI::BOLD << "[+] Join-All Completed!" << UI::RESET << "\n";
    std::cout << " Successfully joined: " << UI::BOLD << successfulJoins << "/" << total << UI::RESET << " communities.\n";
    std::cout << " All state updated in: " << UI::DIM << m_stateFilePath << UI::RESET << "\n\n";

    return true;
}

bool GroupFinder::resumeJoinQueue() {
    loadState();
    if (m_queueState.pending.empty()) {
        UI::printWarning("No pending join queue found in " + m_stateFilePath);
        return false;
    }

    std::vector<GroupSearchResult> remaining;
    for (size_t i = m_queueState.currentIndex; i < m_queueState.pending.size(); ++i) {
        remaining.push_back(m_queueState.pending[i]);
    }

    if (remaining.empty()) {
        UI::printSuccess("Saved join queue was already completed.");
        m_queueState.inProgress = false;
        saveState();
        return true;
    }

    UI::printInfo("Resuming join queue with " + std::to_string(remaining.size()) + " remaining communities...");
    return joinAll(remaining);
}

void GroupFinder::runInteractiveFinder() {
    std::cout << UI::CYAN << UI::BOLD << "\n[=== Interactive Group & Channel Finder ===]" << UI::RESET << "\n";
    std::cout << " Active Pacing Profile: " << UI::BOLD << m_pacing.getModeDescription() << UI::RESET << "\n";
    std::cout << " Saved Joined Records:  " << UI::BOLD << m_joinedChats.size() << UI::RESET << " communities\n";
    std::cout << " State JSON:            " << UI::DIM << m_stateFilePath << UI::RESET << "\n\n";

    std::string query = UI::prompt("Enter search keywords or @username (e.g. dev, crypto, trading)");
    if (query.empty()) return;

    std::cout << "\n" << UI::BOLD << "Aim Member Limit (0 to Aim):" << UI::RESET << "\n";
    std::cout << " " << UI::DIM << "Tip: Enter '10k' to find all groups/channels from 0 up to 10,000 members." << UI::RESET << "\n";
    std::string targetMStr = UI::prompt("Aim member limit (e.g. 10k, 5k, 500, or Enter for all)");
    std::string targetOStr = UI::prompt("Target online members (e.g. 100, 1k, or Enter for any)");
    std::string canSendStr = UI::prompt("Require permission to send messages? (y/n, default: n)");

    GroupFilterCriteria criteria;
    criteria.query = query;
    criteria.minMembers = 0;
    criteria.maxMembers = parseQuantity(targetMStr);
    criteria.targetOnline = parseQuantity(targetOStr);
    criteria.requireCanSend = (canSendStr == "y" || canSendStr == "Y");
    criteria.includeChannels = true;
    criteria.includeGroups = true;
    criteria.excludeJoined = true; // Never show already joined ones!

    auto results = search(criteria);
    printResults(results);

    if (results.empty()) {
        return;
    }

    while (true) {
        std::cout << UI::BOLD << "Available Operations:" << UI::RESET << "\n"
                  << "  [J] " << UI::GREEN << UI::BOLD << "JOIN ALL " << UI::RESET << "(" << results.size() << " communities with safe pacing)\n"
                  << "  [P] Configure Pacing profile (" << m_pacing.getModeDescription() << ")\n"
                  << "  [1-" << results.size() << "] Select individual community\n"
                  << "  [0] Back to Main Menu\n\n";

        std::string act = UI::prompt("Select action");
        if (act == "0" || act == "back" || act == "q") {
            break;
        } else if (act == "j" || act == "J" || act == "join" || act == "join-all") {
            joinAll(results);
            break;
        } else if (act == "p" || act == "P" || act == "pacing") {
            m_pacing.configureInteractive();
            saveState();
        } else {
            int idx = 0;
            try { idx = std::stoi(act); } catch (...) {}
            if (idx >= 1 && idx <= static_cast<int>(results.size())) {
                const auto& chosen = results[idx - 1];
                std::cout << "\nSelected " << chosen.typeLabel << ": " << UI::BOLD << chosen.title << UI::RESET
                          << " (ID: " << chosen.chatId << ")\n";
                std::cout << "Actions:\n"
                          << "  [1] Join this " << chosen.typeLabel << "\n"
                          << "  [2] Add to Targetor channel monitoring\n"
                          << "  [3] Send a message\n"
                          << "  [0] Back\n\n";

                std::string subAct = UI::prompt("Action [0-3]");
                if (subAct == "1") {
                    UI::printInfo("Joining " + chosen.title + "...");
                    if (m_client.joinChat(chosen.chatId)) {
                        UI::printSuccess("Successfully joined " + chosen.title + "!");
                        markChatJoined(chosen.chatId, chosen.title, chosen.username, chosen.typeLabel);
                    } else {
                        UI::printError("Could not join (may require invite link or approval).");
                    }
                } else if (subAct == "2") {
                    const char* home = std::getenv("HOME");
                    std::string base = home ? home : "";
                    std::vector<std::string> candidates = {
                        base + "/.skyien/targetor_config.json",
                        "targetor/targetor_config.json",
                        base + "/Desktop/Skyien/targetor/targetor_config.json",
                        base + "/Skyien/targetor/targetor_config.json",
                        "/data/data/com.termux/files/home/Skyien/targetor/targetor_config.json"
                    };
                    std::string targetCfgPath = base.empty() ? "targetor/targetor_config.json" : (base + "/.skyien/targetor_config.json");
                    for (const auto& cand : candidates) {
                        std::ifstream testF(cand);
                        if (testF.good()) {
                            targetCfgPath = cand;
                            break;
                        }
                    }

                    std::ifstream f(targetCfgPath);
                    Json::Value root;
                    Json::CharReaderBuilder b;
                    std::string errs;
                    if (Json::parseFromStream(b, f, &root, &errs)) {
                        f.close();
                        root["monitored_channels"].append(chosen.chatId);
                        std::ofstream out(targetCfgPath);
                        Json::StreamWriterBuilder w;
                        w["indentation"] = "  ";
                        out << Json::writeString(w, root);
                        UI::printSuccess("Added " + chosen.title + " (" + std::to_string(chosen.chatId) + ") to Targetor monitoring!");
                    }
                } else if (subAct == "3") {
                    std::string msg = UI::prompt("Enter message text");
                    if (!msg.empty()) {
                        if (m_client.sendMessage(chosen.chatId, EmojiFilter::strip(msg))) {
                            UI::printSuccess("Message sent to " + chosen.title + "!");
                        } else {
                            UI::printError("Failed to deliver message.");
                        }
                    }
                }
            }
        }
    }
}

} // namespace Skyien

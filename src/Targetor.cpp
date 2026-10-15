#include "Targetor.hpp"
#include "TerminalUI.hpp"
#include "EmojiFilter.hpp"
#include <json/json.h>
#include <iostream>
#include <fstream>
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Skyien {

static std::string toLowerString(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

Targetor::Targetor(TelegramClient& client, const Config& config)
    : m_client(client), m_baseConfig(config) {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : "";
    std::vector<std::string> candidates = {
        base + "/.skyien/targetor_config.json",
        "targetor/targetor_config.json",
        base + "/Desktop/Skyien/targetor/targetor_config.json",
        base + "/Skyien/targetor/targetor_config.json",
        "/data/data/com.termux/files/home/Skyien/targetor/targetor_config.json"
    };
    m_configPath = base.empty() ? "targetor/targetor_config.json" : (base + "/.skyien/targetor_config.json");
    for (const auto& cand : candidates) {
        std::ifstream f(cand);
        if (f.good()) {
            m_configPath = cand;
            break;
        }
    }
    
    // Default custom message
    m_customMessageTemplate = "Hello! Saw your recent inquiry. Reaching out directly from my profile to assist. [->] Let's discuss details.";
    
    loadConfig();
}

std::string Targetor::formatDuration(int64_t seconds) {
    int64_t days = seconds / 86400;
    seconds %= 86400;
    int64_t hours = seconds / 3600;
    seconds %= 3600;
    int64_t mins = seconds / 60;
    int64_t secs = seconds % 60;

    std::ostringstream ss;
    if (days > 0) ss << days << "d ";
    if (hours > 0 || days > 0) ss << hours << "h ";
    if (mins > 0 || hours > 0 || days > 0) ss << mins << "m ";
    ss << secs << "s";
    return ss.str();
}

int64_t Targetor::computeNextRandomGap() {
    // Generates a randomized interval based on current escalating base gap
    std::random_device rd;
    std::mt19937_64 gen(rd());
    
    int64_t lower = m_currentBaseGap;
    int64_t upper = std::min(m_maxGapSeconds, static_cast<int64_t>(m_currentBaseGap * 1.3));
    if (upper < lower) upper = lower;

    std::uniform_int_distribution<int64_t> dist(lower, upper);
    return dist(gen);
}

bool Targetor::loadConfig(const std::string& path) {
    std::string targetPath = path.empty() ? m_configPath : path;
    std::ifstream file(targetPath);
    if (!file.is_open()) return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, file, &root, &errs)) return false;

    if (root.isMember("min_gap_seconds")) m_minGapSeconds = root["min_gap_seconds"].asInt64();
    if (root.isMember("max_gap_seconds")) m_maxGapSeconds = root["max_gap_seconds"].asInt64();
    if (root.isMember("current_base_gap")) m_currentBaseGap = root["current_base_gap"].asInt64();
    if (root.isMember("escalation_step")) m_escalationStep = root["escalation_step"].asInt64();
    if (root.isMember("escalation_multiplier")) m_escalationMultiplier = root["escalation_multiplier"].asDouble();
    if (root.isMember("daily_limit")) m_dailyLimit = root["daily_limit"].asInt();
    if (root.isMember("custom_message_template")) m_customMessageTemplate = root["custom_message_template"].asString();

    if (root.isMember("monitored_channels") && root["monitored_channels"].isArray()) {
        m_monitoredChannels.clear();
        for (const auto& c : root["monitored_channels"]) {
            m_monitoredChannels.push_back(c.asInt64());
        }
    }

    if (root.isMember("keywords") && root["keywords"].isArray()) {
        m_keywords.clear();
        for (const auto& k : root["keywords"]) {
            m_keywords.push_back(k.asString());
        }
    }

    if (root.isMember("contacted_user_ids") && root["contacted_user_ids"].isArray()) {
        m_contactedUserIds.clear();
        for (const auto& id : root["contacted_user_ids"]) {
            m_contactedUserIds.insert(id.asInt64());
        }
    }

    return true;
}

bool Targetor::saveConfig(const std::string& path) const {
    std::string targetPath = path.empty() ? m_configPath : path;
    std::ofstream file(targetPath);
    if (!file.is_open()) return false;

    Json::Value root;
    root["min_gap_seconds"] = m_minGapSeconds;
    root["max_gap_seconds"] = m_maxGapSeconds;
    root["current_base_gap"] = m_currentBaseGap;
    root["escalation_step"] = m_escalationStep;
    root["escalation_multiplier"] = m_escalationMultiplier;
    root["daily_limit"] = m_dailyLimit;
    root["custom_message_template"] = m_customMessageTemplate;

    Json::Value channels(Json::arrayValue);
    for (auto c : m_monitoredChannels) channels.append(c);
    root["monitored_channels"] = channels;

    Json::Value keywords(Json::arrayValue);
    for (const auto& k : m_keywords) keywords.append(k);
    root["keywords"] = keywords;

    Json::Value contacted(Json::arrayValue);
    for (auto id : m_contactedUserIds) contacted.append(id);
    root["contacted_user_ids"] = contacted;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    file << Json::writeString(writer, root);
    return true;
}

void Targetor::start() {
    if (m_running.load()) return;

    UserProfile me = m_client.getMe();
    m_myUserId = me.id;

    m_running.store(true);
    m_workerThread = std::thread(&Targetor::loopWorker, this);

    std::cout << UI::GREEN << "[+] Targetor Engine Started" << UI::RESET << "\n";
    std::cout << "    [#] Monitored Channels: " << m_monitoredChannels.size() << "\n";
    std::cout << "    [#] Trigger Keywords:   " << m_keywords.size() << "\n";
    std::cout << "    [#] Initial Min Gap:    " << formatDuration(m_minGapSeconds) << "\n";
    std::cout << "    [#] Maximum Gap:        " << formatDuration(m_maxGapSeconds) << "\n";
    std::cout << "    [#] Current Gap Tier:   " << formatDuration(m_currentBaseGap) << "\n";
}

void Targetor::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    saveConfig();
    std::cout << UI::YELLOW << "[!] Targetor Engine Stopped" << UI::RESET << "\n";
}

bool Targetor::isRunning() const {
    return m_running.load();
}

void Targetor::addMonitoredChannel(int64_t chatId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto c : m_monitoredChannels) {
        if (c == chatId) return;
    }
    m_monitoredChannels.push_back(chatId);
    saveConfig();
    std::cout << UI::GREEN << "[+] Added Channel " << chatId << " to Targetor monitoring." << UI::RESET << "\n";
}

void Targetor::addKeyword(const std::string& keyword) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string clean = EmojiFilter::strip(keyword);
    m_keywords.push_back(clean);
    saveConfig();
    std::cout << UI::GREEN << "[+] Added Keyword '" << clean << "' to Targetor conditions." << UI::RESET << "\n";
}

void Targetor::setGaps(int64_t minSec, int64_t maxSec) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minGapSeconds = minSec;
    m_maxGapSeconds = maxSec;
    if (m_currentBaseGap < m_minGapSeconds) m_currentBaseGap = m_minGapSeconds;
    if (m_currentBaseGap > m_maxGapSeconds) m_currentBaseGap = m_maxGapSeconds;
    saveConfig();
}

void Targetor::setCustomTemplate(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_customMessageTemplate = EmojiFilter::strip(text);
    saveConfig();
}

void Targetor::onUpdate(const Json::Value& update) {
    if (!m_running.load()) return;
    if (!update.isMember("@type") || update["@type"].asString() != "updateNewMessage") return;

    const auto& msg = update["message"];
    int64_t chatId = msg.get("chat_id", 0).asInt64();

    // Check if channel is monitored
    bool isMonitored = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto c : m_monitoredChannels) {
            if (c == chatId) {
                isMonitored = true;
                break;
            }
        }
    }
    if (!isMonitored) return;

    // Check if message is from a regular user (not anonymous channel or self)
    if (!msg.isMember("sender_id")) return;
    const auto& sender = msg["sender_id"];
    if (sender.get("@type", "").asString() != "messageSenderUser") return;

    int64_t userId = sender.get("user_id", 0).asInt64();
    if (userId == 0 || userId == m_myUserId) return;

    // Check if user has already been contacted
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_contactedUserIds.count(userId) > 0) return;
    }

    // Extract text content and strip emojis
    std::string text;
    if (msg.isMember("content")) {
        const auto& c = msg["content"];
        if (c.get("@type", "").asString() == "messageText" && c.isMember("text")) {
            text = c["text"].get("text", "").asString();
        }
    }
    if (text.empty()) return;

    std::string cleanText = EmojiFilter::strip(text);
    std::string lowerText = toLowerString(cleanText);

    // Check keyword triggers
    std::string matchedKeyword;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& kw : m_keywords) {
            std::string lowerKw = toLowerString(kw);
            if (lowerText.find(lowerKw) != std::string::npos) {
                matchedKeyword = kw;
                break;
            }
        }
    }

    if (matchedKeyword.empty()) return;

    // Candidate found! Queue it
    TargetCandidate cand;
    cand.userId = userId;
    cand.fromChatId = chatId;
    cand.matchedKeyword = matchedKeyword;
    cand.detectedAt = std::chrono::system_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Avoid duplicate queueing
        for (const auto& item : m_pendingQueue) {
            if (item.userId == userId) return;
        }
        m_pendingQueue.push_back(cand);
    }

    std::cout << UI::CYAN << "[TARGET] [*] Match spotted in Channel " << chatId 
              << " from User " << userId << " [Keyword: '" << matchedKeyword << "']" << UI::RESET << "\n";
}

void Targetor::loopWorker() {
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        TargetCandidate cand;
        bool hasCandidate = false;

        auto now = std::chrono::system_clock::now();
        int64_t nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_pendingQueue.empty()) {
                if (nowEpoch >= m_nextScheduledDispatchTime) {
                    cand = m_pendingQueue.front();
                    m_pendingQueue.pop_front();
                    hasCandidate = true;
                }
            }
        }

        if (hasCandidate) {
            processCandidate(cand);
        }
    }
}

void Targetor::processCandidate(const TargetCandidate& cand) {
    // Check if daily limit reached
    if (m_dmsSentToday >= m_dailyLimit) {
        std::cout << UI::YELLOW << "[TARGET] [!] Daily safe limit reached (" << m_dailyLimit << "). Pausing for tomorrow." << UI::RESET << "\n";
        return;
    }

    // Double check contacted set
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_contactedUserIds.count(cand.userId) > 0) return;
    }

    // Format custom message (strip all emojis)
    std::string textToSend = EmojiFilter::strip(m_customMessageTemplate);

    std::cout << UI::MAGENTA << "[TARGET] [->] Sending custom DM to User " << cand.userId << "..." << UI::RESET << "\n";
    bool sent = m_client.sendMessage(cand.userId, textToSend);

    auto now = std::chrono::system_clock::now();
    int64_t nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    if (sent) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_contactedUserIds.insert(cand.userId);
        m_dmsSentToday++;
        m_lastSentTime = now;

        // Escalate the interval from less to large
        m_currentBaseGap = std::min(m_maxGapSeconds, static_cast<int64_t>(m_currentBaseGap * m_escalationMultiplier + m_escalationStep));
        int64_t nextGap = computeNextRandomGap();
        m_nextScheduledDispatchTime = nowEpoch + nextGap;

        saveConfig();

        std::cout << UI::GREEN << "[TARGET] [+] Successfully delivered DM to User " << cand.userId << "!\n"
                  << "         [+] Escalated Gap Tier: " << formatDuration(m_currentBaseGap) << "\n"
                  << "         [+] Next Randomized Interval: " << formatDuration(nextGap) << " from now\n"
                  << UI::RESET;
    } else {
        std::cout << UI::RED << "[TARGET] [X] Could not send DM to User " << cand.userId << " (User privacy or blocked)." << UI::RESET << "\n";
        // Re-queue or skip to avoid getting stuck
        std::lock_guard<std::mutex> lock(m_mutex);
        m_contactedUserIds.insert(cand.userId); // Mark as attempted
    }
}

void Targetor::printStatus() const {
    std::cout << UI::CYAN << UI::BOLD << "\n[=== Targetor Engine Status ===]" << UI::RESET << "\n";
    std::cout << "  [-] Active:               " << (m_running.load() ? (std::string(UI::GREEN) + "RUNNING" + UI::RESET) : (std::string(UI::YELLOW) + "STOPPED" + UI::RESET)) << "\n";
    std::cout << "  [-] Monitored Channels:   " << m_monitoredChannels.size() << "\n";
    for (auto c : m_monitoredChannels) {
        std::cout << "      [>] Channel ID: " << c << "\n";
    }
    std::cout << "  [-] Conditions/Keywords:  " << m_keywords.size() << "\n";
    for (const auto& kw : m_keywords) {
        std::cout << "      [*] '" << kw << "'\n";
    }
    std::cout << "  [-] Custom DM Text:       \"" << m_customMessageTemplate << "\"\n";
    std::cout << "  [-] Min Interval Gap:     " << formatDuration(m_minGapSeconds) << "\n";
    std::cout << "  [-] Max Interval Gap:     " << formatDuration(m_maxGapSeconds) << "\n";
    std::cout << "  [-] Current Escalation:   " << formatDuration(m_currentBaseGap) << " (Multiplier: " << m_escalationMultiplier << "x)\n";
    std::cout << "  [-] Pending Target Queue: " << m_pendingQueue.size() << " detected users\n";
    std::cout << "  [-] Contacted Users:      " << m_contactedUserIds.size() << " total\n";
    std::cout << UI::CYAN << "────────────────────────────────\n" << UI::RESET;
}

void Targetor::runInteractiveConfig() {
    while (true) {
        printStatus();
        std::cout << UI::BOLD << "Configure Targetor Options:" << UI::RESET << "\n"
                  << "  [1] [+] Add Monitored Channel\n"
                  << "  [2] [-] Clear Monitored Channels\n"
                  << "  [3] [+] Add Keyword / Condition\n"
                  << "  [4] [-] Clear Keywords\n"
                  << "  [5] [=] Edit Custom Message Template\n"
                  << "  [6] [~] Set Interval Gaps (Min & Max, default: 2h to 4d)\n"
                  << "  [7] [R] Reset Escalation to Min Gap\n"
                  << "  [0] [X] Back to Main Menu\n\n";

        std::string choice = UI::prompt("Enter selection [0-7]");
        if (choice == "0" || choice == "back" || choice == "b") break;

        if (choice == "1") {
            int64_t id = m_client.selectChatInteractive("Select channel to monitor");
            if (id != 0) {
                addMonitoredChannel(id);
            }
        } else if (choice == "2") {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_monitoredChannels.clear();
            saveConfig();
            UI::printSuccess("Monitored channels cleared.");
        } else if (choice == "3") {
            std::string kw = UI::prompt("Enter keyword or trigger phrase");
            if (!kw.empty()) addKeyword(kw);
        } else if (choice == "4") {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_keywords.clear();
            saveConfig();
            UI::printSuccess("Keywords cleared.");
        } else if (choice == "5") {
            std::string tmpl = UI::prompt("Enter new custom DM message");
            if (!tmpl.empty()) setCustomTemplate(tmpl);
        } else if (choice == "6") {
            std::cout << "Enter durations in hours (e.g. 2 for 2 hours, 96 for 4 days):\n";
            std::string minH = UI::prompt("Min Gap in hours (default 2)");
            std::string maxH = UI::prompt("Max Gap in hours (default 96 [4 days])");
            try {
                int64_t minSec = (!minH.empty() ? std::stoll(minH) : 2) * 3600;
                int64_t maxSec = (!maxH.empty() ? std::stoll(maxH) : 96) * 3600;
                setGaps(minSec, maxSec);
                UI::printSuccess("Interval gaps updated.");
            } catch (...) {
                UI::printError("Invalid input.");
            }
        } else if (choice == "7") {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_currentBaseGap = m_minGapSeconds;
            saveConfig();
            UI::printSuccess("Escalation reset to " + formatDuration(m_minGapSeconds));
        }
    }
}

} // namespace Skyien

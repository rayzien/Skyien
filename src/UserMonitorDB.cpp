#include "UserMonitorDB.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Skyien {

void UserMonitorDB::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_path = path;
    std::ifstream file(path);
    if (!file.is_open()) return;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (Json::parseFromStream(builder, file, &root, &errs)) {
        const Json::Value& users = root["users"];
        for (const auto& u : users) {
            UserRecord record;
            record.id = u["id"].asInt64();
            record.currentUsername = u["currentUsername"].asString();
            record.currentFirstName = u["currentFirstName"].asString();
            record.currentLastName = u["currentLastName"].asString();
            
            for (const auto& h : u["usernameHistory"]) {
                record.usernameHistory.push_back(h.asString());
            }
            for (const auto& g : u["groups"]) {
                record.groups.insert(g.asInt64());
            }
            m_users[record.id] = record;
        }
    }
}

void UserMonitorDB::save() {
    if (m_path.empty()) return;
    Json::Value root;
    Json::Value users(Json::arrayValue);

    for (const auto& pair : m_users) {
        const auto& rec = pair.second;
        Json::Value u;
        u["id"] = rec.id;
        u["currentUsername"] = rec.currentUsername;
        u["currentFirstName"] = rec.currentFirstName;
        u["currentLastName"] = rec.currentLastName;
        
        Json::Value history(Json::arrayValue);
        for (const auto& h : rec.usernameHistory) history.append(h);
        u["usernameHistory"] = history;

        Json::Value groups(Json::arrayValue);
        for (auto g : rec.groups) groups.append(g);
        u["groups"] = groups;

        users.append(u);
    }
    root["users"] = users;

    std::ofstream file(m_path);
    Json::StreamWriterBuilder writer;
    file << Json::writeString(writer, root);
}

void UserMonitorDB::trackUser(int64_t id, const std::string& username, const std::string& firstName, const std::string& lastName, int64_t groupId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& rec = m_users[id];
    if (rec.id == 0) rec.id = id;
    
    if (rec.currentUsername != username) {
        if (!rec.currentUsername.empty()) {
            if (std::find(rec.usernameHistory.begin(), rec.usernameHistory.end(), rec.currentUsername) == rec.usernameHistory.end()) {
                rec.usernameHistory.push_back(rec.currentUsername);
            }
        }
        rec.currentUsername = username;
    }
    rec.currentFirstName = firstName;
    rec.currentLastName = lastName;
    if (groupId != 0) {
        rec.groups.insert(groupId);
    }
    // We can save async but for simplicity we won't block every update with a save here
}

void UserMonitorDB::addInboxMessage(int64_t userId, const std::string& name, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inbox.push_back({userId, name, message});
}

std::string UserMonitorDB::getInboxSummary() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_inbox.empty()) return "Inbox is empty.";
    
    std::stringstream ss;
    ss << "[*] *INBOX REPLIES*\n";
    for (size_t i = 0; i < m_inbox.size(); ++i) {
        ss << "[" << i+1 << "] From: " << m_inbox[i].name << " (ID: " << m_inbox[i].userId << ")\n"
           << "Message: " << m_inbox[i].text << "\n\n";
    }
    m_inbox.clear();
    return ss.str();
}

} // namespace Skyien

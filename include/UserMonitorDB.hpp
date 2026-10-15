#pragma once
#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <set>
#include <mutex>
#include <json/json.h>

namespace Skyien {

struct UserRecord {
    int64_t id{0};
    std::string currentUsername;
    std::string currentFirstName;
    std::string currentLastName;
    std::vector<std::string> usernameHistory;
    std::set<int64_t> groups;
};

class UserMonitorDB {
public:
    static UserMonitorDB& getInstance() {
        static UserMonitorDB instance;
        return instance;
    }

    void load(const std::string& path);
    void save();

    void trackUser(int64_t id, const std::string& username, const std::string& firstName, const std::string& lastName, int64_t groupId);
    std::string getInboxSummary();
    void addInboxMessage(int64_t userId, const std::string& name, const std::string& message);

private:
    UserMonitorDB() = default;
    std::string m_path;
    std::unordered_map<int64_t, UserRecord> m_users;
    
    struct InboxMsg {
        int64_t userId;
        std::string name;
        std::string text;
    };
    std::vector<InboxMsg> m_inbox;
    std::mutex m_mutex;
};

} // namespace Skyien

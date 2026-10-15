#include "TelegramClient.hpp"
#include "TerminalUI.hpp"
#include "EmojiFilter.hpp"
#include <td/telegram/td_json_client.h>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace Skyien {

TelegramClient::TelegramClient(const Config& config)
    : m_config(config) {
    Json::Value setLog;
    setLog["@type"] = "setLogVerbosityLevel";
    setLog["new_verbosity_level"] = 1;
    Json::FastWriter writer;
    td_json_client_execute(nullptr, writer.write(setLog).c_str());

    m_tdClient = td_json_client_create();
}

TelegramClient::~TelegramClient() {
    stop();
    if (m_tdClient) {
        td_json_client_destroy(m_tdClient);
        m_tdClient = nullptr;
    }
}

bool TelegramClient::start() {
    if (!m_tdClient) return false;
    if (m_running.load()) return true;

    m_running.store(true);
    m_workerThread = std::thread(&TelegramClient::workerLoop, this);
    return true;
}

void TelegramClient::stop() {
    if (!m_running.load()) return;

    m_running.store(false);
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void TelegramClient::setUpdateHandler(UpdateHandler handler) {
    m_updateHandler = std::move(handler);
}

void TelegramClient::sendAsync(const Json::Value& request) {
    if (!m_tdClient) return;
    Json::FastWriter writer;
    std::string jsonStr = writer.write(request);
    td_json_client_send(m_tdClient, jsonStr.c_str());
}

Json::Value TelegramClient::sendSync(Json::Value request, double timeoutSec) {
    if (!m_tdClient || !m_running.load()) {
        Json::Value err;
        err["@type"] = "error";
        err["message"] = "Client not running";
        return err;
    }

    uint64_t extraId = m_nextExtraId.fetch_add(1);
    request["@extra"] = std::to_string(extraId);

    std::future<Json::Value> fut;
    {
        std::lock_guard<std::mutex> lock(m_promisesMutex);
        fut = m_pendingPromises[extraId].get_future();
    }

    sendAsync(request);

    if (fut.wait_for(std::chrono::duration<double>(timeoutSec)) == std::future_status::ready) {
        return fut.get();
    } else {
        std::lock_guard<std::mutex> lock(m_promisesMutex);
        m_pendingPromises.erase(extraId);
        Json::Value err;
        err["@type"] = "error";
        err["message"] = "Request timed out";
        return err;
    }
}

void TelegramClient::workerLoop() {
    Json::CharReaderBuilder builder;
    while (m_running.load()) {
        const char* res = td_json_client_receive(m_tdClient, 0.5);
        if (!res) continue;

        std::string jsonStr(res);
        Json::Value root;
        std::string errs;
        std::istringstream s(jsonStr);
        if (!Json::parseFromStream(builder, s, &root, &errs)) {
            continue;
        }

        handleIncoming(root);
    }
}

void TelegramClient::handleIncoming(const Json::Value& obj) {
    if (obj.isMember("@extra")) {
        std::string extraStr = obj["@extra"].asString();
        try {
            uint64_t extraId = std::stoull(extraStr);
            std::lock_guard<std::mutex> lock(m_promisesMutex);
            auto it = m_pendingPromises.find(extraId);
            if (it != m_pendingPromises.end()) {
                it->second.set_value(obj);
                m_pendingPromises.erase(it);
                return;
            }
        } catch (...) {
            // Ignore parse errors on extra
        }
    }

    if (obj.isMember("@type")) {
        std::string type = obj["@type"].asString();
        if (type == "updateAuthorizationState") {
            std::string state = obj["authorization_state"]["@type"].asString();
            {
                std::lock_guard<std::mutex> lock(m_authMutex);
                m_currentAuthState = state;
            }
            m_authCv.notify_all();
        } else if (type == "updateNewChat") {
            const auto& c = obj["chat"];
            int64_t id = c.get("id", 0).asInt64();
            if (id != 0) {
                ChatInfo info;
                info.id = id;
                info.title = c.get("title", "Unnamed").asString();
                info.unreadCount = c.get("unread_count", 0).asInt();
                if (c.isMember("type")) {
                    info.type = c["type"].get("@type", "").asString();
                }
                std::lock_guard<std::mutex> lock(m_chatsMutex);
                bool exists = false;
                for (auto cid : m_chatOrder) {
                    if (cid == id) { exists = true; break; }
                }
                if (!exists) m_chatOrder.push_back(id);
                m_chatsCache[id] = info;
            }
        } else if (type == "updateChatTitle") {
            int64_t id = obj.get("chat_id", 0).asInt64();
            std::lock_guard<std::mutex> lock(m_chatsMutex);
            if (m_chatsCache.count(id)) {
                m_chatsCache[id].title = obj.get("title", "").asString();
            }
        } else if (type == "updateChatReadInbox") {
            int64_t id = obj.get("chat_id", 0).asInt64();
            std::lock_guard<std::mutex> lock(m_chatsMutex);
            if (m_chatsCache.count(id)) {
                m_chatsCache[id].unreadCount = obj.get("unread_count", 0).asInt();
            }
        }

        if (m_updateHandler) {
            m_updateHandler(obj);
        }
    }
}

bool TelegramClient::authenticate(const std::string& explicitPhoneNumber) {
    start();

    std::string phoneToUse = explicitPhoneNumber;
    if (phoneToUse.empty() && !m_config.phoneNumber.empty()) {
        phoneToUse = m_config.phoneNumber;
    }

    std::string lastHandledState = "";

    while (m_running.load()) {
        std::string state;
        {
            std::unique_lock<std::mutex> lock(m_authMutex);
            m_authCv.wait_for(lock, std::chrono::milliseconds(200), [this, &lastHandledState]() {
                return !m_currentAuthState.empty() && m_currentAuthState != lastHandledState;
            });
            state = m_currentAuthState;
        }

        if (state.empty() || state == lastHandledState) continue;
        lastHandledState = state;

        if (state == "authorizationStateWaitTdlibParameters") {
            Json::Value p;
            p["@type"] = "setTdlibParameters";
            p["database_directory"] = m_config.sessionDirectory + "/data";
            p["files_directory"] = m_config.sessionDirectory + "/files";
            p["use_file_database"] = true;
            p["use_chat_info_database"] = true;
            p["use_message_database"] = true;
            p["use_secret_chats"] = false;
            p["api_id"] = m_config.getEffectiveApiId();
            p["api_hash"] = m_config.getEffectiveApiHash();
            p["system_language_code"] = "en";
            p["device_model"] = "Termux AArch64";
            p["system_version"] = "Android";
            p["application_version"] = "1.0.0";
            p["enable_storage_optimizer"] = true;

            sendAsync(p);
        } else if (state == "authorizationStateWaitEncryptionKey") {
            Json::Value req;
            req["@type"] = "checkDatabaseEncryptionKey";
            req["encryption_key"] = "";
            sendAsync(req);
        } else if (state == "authorizationStateWaitPhoneNumber") {
            while (true) {
                if (phoneToUse.empty()) {
                    std::cout << UI::CYAN << UI::BOLD << "\n[Telegram Authorizer]" << UI::RESET << "\n";
                    phoneToUse = UI::prompt("Enter your phone number with country code (e.g. +1234567890)");
                }
                
                UI::printInfo("Requesting login code for: " + phoneToUse + "...");
                Json::Value req;
                req["@type"] = "setAuthenticationPhoneNumber";
                req["phone_number"] = phoneToUse;
                Json::Value res = sendSync(req, 10.0);

                if (res.get("@type", "").asString() == "error") {
                    UI::printError("Failed: " + res.get("message", "Invalid phone number").asString());
                    phoneToUse.clear();
                } else {
                    break;
                }
            }
        } else if (state == "authorizationStateWaitCode") {
            UI::printSuccess("Telegram login code sent! Please check your official Telegram app or SMS.");
            while (true) {
                std::string code = UI::prompt("Enter Telegram Login Code");
                if (code.empty()) continue;

                UI::printInfo("Verifying code...");
                Json::Value req;
                req["@type"] = "checkAuthenticationCode";
                req["code"] = code;
                Json::Value res = sendSync(req, 10.0);

                if (res.get("@type", "").asString() == "error") {
                    UI::printError("Code verification failed: " + res.get("message", "Incorrect code").asString());
                    std::cout << "Please try entering the code again.\n";
                } else {
                    break;
                }
            }
        } else if (state == "authorizationStateWaitPassword") {
            UI::printInfo("Two-Step Verification (2FA) is enabled on this account.");
            while (true) {
                std::string password = UI::prompt("Enter 2FA Cloud Password", true);
                UI::printInfo("Checking password...");
                Json::Value req;
                req["@type"] = "checkAuthenticationPassword";
                req["password"] = password;
                Json::Value res = sendSync(req, 10.0);

                if (res.get("@type", "").asString() == "error") {
                    UI::printError("Password verification failed: " + res.get("message", "Wrong password").asString());
                } else {
                    break;
                }
            }
        } else if (state == "authorizationStateReady") {
            m_authenticated.store(true);
            UI::printSuccess("Telegram Account Authorized & Logged In Successfully!");

            // Pre-load chats immediately
            for (int i = 0; i < 3; ++i) {
                Json::Value loadReq;
                loadReq["@type"] = "loadChats";
                Json::Value cList;
                cList["@type"] = "chatListMain";
                loadReq["chat_list"] = cList;
                loadReq["limit"] = 50;
                sendAsync(loadReq);
            }
            return true;
        } else if (state == "authorizationStateLoggingOut" || state == "authorizationStateClosing" || state == "authorizationStateClosed") {
            m_authenticated.store(false);
            UI::printWarning("Telegram session ended or closed.");
            return false;
        }
    }

    return m_authenticated.load();
}

bool TelegramClient::isAuthenticated() const {
    return m_authenticated.load();
}

UserProfile TelegramClient::getMe() {
    Json::Value req;
    req["@type"] = "getMe";
    Json::Value res = sendSync(req, 8.0);

    UserProfile p;
    if (res.get("@type", "").asString() == "user") {
        p.id = res.get("id", 0).asInt64();
        p.firstName = res.get("first_name", "").asString();
        p.lastName = res.get("last_name", "").asString();
        p.phoneNumber = res.get("phone_number", "").asString();
        if (res.isMember("usernames") && res["usernames"].isMember("active_usernames")) {
            const auto& active = res["usernames"]["active_usernames"];
            if (!active.empty()) {
                p.username = active[0].asString();
            }
        }
        m_myUserId = p.id;
    }
    return p;
}

int64_t TelegramClient::getMyUserId() {
    if (m_myUserId == 0) {
        getMe();
    }
    return m_myUserId;
}

std::vector<ChatInfo> TelegramClient::getChats(int limit) {
    // 1. Trigger loading chats from main list
    for (int i = 0; i < 2; ++i) {
        Json::Value loadReq;
        loadReq["@type"] = "loadChats";
        Json::Value cList;
        cList["@type"] = "chatListMain";
        loadReq["chat_list"] = cList;
        loadReq["limit"] = limit;
        Json::Value loadRes = sendSync(loadReq, 3.0);
        if (loadRes.get("@type", "").asString() == "error") {
            break;
        }
    }

    // 2. Fetch chats list
    Json::Value req;
    req["@type"] = "getChats";
    Json::Value cList;
    cList["@type"] = "chatListMain";
    req["chat_list"] = cList;
    req["limit"] = limit;
    Json::Value res = sendSync(req, 5.0);

    if (res.get("@type", "").asString() == "chats" && res.isMember("chat_ids")) {
        for (const auto& idVal : res["chat_ids"]) {
            int64_t chatId = idVal.asInt64();
            Json::Value chatReq;
            chatReq["@type"] = "getChat";
            chatReq["chat_id"] = chatId;
            Json::Value chatRes = sendSync(chatReq, 2.0);

            if (chatRes.get("@type", "").asString() == "chat") {
                ChatInfo info;
                info.id = chatId;
                info.title = chatRes.get("title", "Unnamed").asString();
                info.unreadCount = chatRes.get("unread_count", 0).asInt();
                info.type = chatRes.get("type", Json::Value())["@type"].asString();

                std::lock_guard<std::mutex> lock(m_chatsMutex);
                bool exists = false;
                for (auto cid : m_chatOrder) {
                    if (cid == chatId) { exists = true; break; }
                }
                if (!exists) m_chatOrder.push_back(chatId);
                m_chatsCache[chatId] = info;
            }
        }
    }

    // 3. Assemble results preserving chat order
    std::vector<ChatInfo> list;
    std::lock_guard<std::mutex> lock(m_chatsMutex);
    int count = 0;
    for (auto id : m_chatOrder) {
        if (m_chatsCache.count(id)) {
            list.push_back(m_chatsCache[id]);
            count++;
            if (count >= limit) break;
        }
    }

    if (list.empty()) {
        for (const auto& [id, info] : m_chatsCache) {
            list.push_back(info);
            if (static_cast<int>(list.size()) >= limit) break;
        }
    }

    return list;
}

int64_t TelegramClient::selectChatInteractive(const std::string& promptText) {
    auto chats = getChats(20);
    if (chats.empty()) {
        UI::printWarning("No chats found in cache or Telegram not synced yet.");
        std::string raw = UI::prompt(promptText + " (or enter raw Chat ID)");
        try {
            return std::stoll(raw);
        } catch (...) {
            return 0;
        }
    }

    std::cout << "\n" << UI::CYAN << UI::BOLD << "[Available Chats]" << UI::RESET << "\n";
    for (size_t i = 0; i < chats.size(); ++i) {
        std::string t = EmojiFilter::strip(chats[i].title);
        if (t.size() > 30) t = t.substr(0, 27) + "...";
        std::cout << "  " << UI::BOLD << "[" << (i + 1) << "]" << UI::RESET << " "
                  << std::left << std::setw(32) << t
                  << UI::DIM << "(ID: " << chats[i].id << ")" << UI::RESET;
        if (chats[i].unreadCount > 0) {
            std::cout << " " << UI::YELLOW << "[" << chats[i].unreadCount << " unread]" << UI::RESET;
        }
        std::cout << "\n";
    }
    std::cout << UI::CYAN << "----------------------------------------------------" << UI::RESET << "\n";

    std::string input = UI::prompt(promptText + " [1-" + std::to_string(chats.size()) + "] or raw ID");
    if (input.empty()) return 0;

    try {
        int idx = std::stoi(input);
        if (idx >= 1 && idx <= static_cast<int>(chats.size())) {
            int64_t chosenId = chats[idx - 1].id;
            UI::printSuccess("Selected: " + EmojiFilter::strip(chats[idx - 1].title) + " (ID: " + std::to_string(chosenId) + ")");
            return chosenId;
        }
    } catch (...) {}

    try {
        return std::stoll(input);
    } catch (...) {
        UI::printError("Invalid selection.");
        return 0;
    }
}

bool TelegramClient::sendMessage(int64_t chatId, const std::string& text) {
    Json::Value req;
    req["@type"] = "sendMessage";
    req["chat_id"] = chatId;

    Json::Value content;
    content["@type"] = "inputMessageText";
    Json::Value formattedText;
    formattedText["@type"] = "formattedText";
    formattedText["text"] = text;
    content["text"] = formattedText;

    req["input_message_content"] = content;

    Json::Value res = sendSync(req, 8.0);
    return res.get("@type", "").asString() == "message";
}

bool TelegramClient::joinChat(int64_t chatId) {
    Json::Value req;
    req["@type"] = "joinChat";
    req["chat_id"] = chatId;
    Json::Value res = sendSync(req, 8.0);
    return res.get("@type", "").asString() == "ok";
}

bool TelegramClient::isChatJoined(int64_t chatId) {
    if (chatId == 0) return false;

    // Fast check in internal chat cache
    {
        std::lock_guard<std::mutex> lock(m_chatsMutex);
        if (m_chatsCache.count(chatId)) {
            return true;
        }
    }

    Json::Value chatReq;
    chatReq["@type"] = "getChat";
    chatReq["chat_id"] = chatId;
    Json::Value chat = sendSync(chatReq, 3.0);

    if (chat.get("@type", "").asString() != "chat") {
        return false;
    }

    // Positions array indicates chat is currently in user's chat list
    if (chat.isMember("positions") && chat["positions"].isArray() && !chat["positions"].empty()) {
        return true;
    }

    // Supergroup or BasicGroup status check
    if (chat.isMember("type")) {
        std::string typeStr = chat["type"].get("@type", "").asString();
        if (typeStr == "chatTypeSupergroup") {
            int64_t sgId = chat["type"].get("supergroup_id", 0).asInt64();
            if (sgId != 0) {
                Json::Value sgReq;
                sgReq["@type"] = "getSupergroup";
                sgReq["supergroup_id"] = sgId;
                Json::Value sgRes = sendSync(sgReq, 3.0);
                if (sgRes.isMember("status") && sgRes["status"].isMember("@type")) {
                    std::string st = sgRes["status"]["@type"].asString();
                    if (st == "chatMemberStatusMember" ||
                        st == "chatMemberStatusAdministrator" ||
                        st == "chatMemberStatusCreator" ||
                        st == "chatMemberStatusRestricted") {
                        return true;
                    }
                    if (st == "chatMemberStatusLeft" || st == "chatMemberStatusBanned") {
                        return false;
                    }
                }
            }
        } else if (typeStr == "chatTypeBasicGroup") {
            int64_t bgId = chat["type"].get("basic_group_id", 0).asInt64();
            if (bgId != 0) {
                Json::Value bgReq;
                bgReq["@type"] = "getBasicGroup";
                bgReq["basic_group_id"] = bgId;
                Json::Value bgRes = sendSync(bgReq, 3.0);
                if (bgRes.isMember("status") && bgRes["status"].isMember("@type")) {
                    std::string st = bgRes["status"]["@type"].asString();
                    if (st == "chatMemberStatusMember" ||
                        st == "chatMemberStatusAdministrator" ||
                        st == "chatMemberStatusCreator") {
                        return true;
                    }
                    if (st == "chatMemberStatusLeft" || st == "chatMemberStatusBanned") {
                        return false;
                    }
                }
            }
        }
    }

    // Check chat member status for self
    int64_t myUid = getMyUserId();
    if (myUid != 0) {
        Json::Value cmReq;
        cmReq["@type"] = "getChatMember";
        cmReq["chat_id"] = chatId;
        Json::Value sender;
        sender["@type"] = "messageSenderUser";
        sender["user_id"] = myUid;
        cmReq["member_id"] = sender;
        Json::Value cmRes = sendSync(cmReq, 2.5);
        if (cmRes.get("@type", "").asString() == "chatMember") {
            std::string st = cmRes.get("status", Json::Value()).get("@type", "").asString();
            if (st == "chatMemberStatusMember" ||
                st == "chatMemberStatusAdministrator" ||
                st == "chatMemberStatusCreator" ||
                st == "chatMemberStatusRestricted") {
                return true;
            }
        }
    }

    return false;
}

bool TelegramClient::viewMessages(int64_t chatId, const std::vector<int64_t>& messageIds) {
    Json::Value req;
    req["@type"] = "viewMessages";
    req["chat_id"] = chatId;
    Json::Value ids(Json::arrayValue);
    for (auto id : messageIds) {
        ids.append(id);
    }
    req["message_ids"] = ids;
    req["force_read"] = true;
    sendAsync(req);
    return true;
}

} // namespace Skyien

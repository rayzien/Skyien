#include "BotBridge.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>

namespace Skyien {

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

bool BotBridge::httpGet(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

bool BotBridge::httpPostJson(const std::string& url, const std::string& jsonBody, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    response.clear();
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

bool BotBridge::verifyToken(const std::string& token, std::string& outUsername, int64_t& outId) {
    if (token.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + token + "/getMe";
    std::string resp;
    if (!httpGet(url, resp)) return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream s(resp);
    if (!Json::parseFromStream(builder, s, &root, &errs)) return false;

    if (root.get("ok", false).asBool() && root.isMember("result")) {
        const auto& result = root["result"];
        outId = result.get("id", 0).asInt64();
        outUsername = result.get("username", "").asString();
        return true;
    }
    return false;
}


bool BotBridge::getUpdates(const std::string& token, int64_t offset, Json::Value& outUpdates) {
    if (token.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + token + "/getUpdates?timeout=30&offset=" + std::to_string(offset);
    std::string resp;
    if (!httpGet(url, resp)) return false;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream s(resp);
    if (!Json::parseFromStream(builder, s, &outUpdates, &errs)) return false;
    return outUpdates.get("ok", false).asBool();
}
bool BotBridge::sendMessage(const std::string& token, int64_t chatId, const std::string& text) {
    if (token.empty() || chatId == 0 || text.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + token + "/sendMessage";

    Json::Value body;
    body["chat_id"] = chatId;
    body["text"] = text;
    Json::FastWriter writer;
    std::string payload = writer.write(body);

    std::string resp;
    if (!httpPostJson(url, payload, resp)) return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream s(resp);
    if (!Json::parseFromStream(builder, s, &root, &errs)) return false;

    return root.get("ok", false).asBool();
}

} // namespace Skyien

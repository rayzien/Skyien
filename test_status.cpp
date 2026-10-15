#include <iostream>
#include <fstream>
#include "include/TelegramClient.hpp"

using namespace Skyien;

int main() {
    Config c;
    TelegramClient client(c);
    if (!client.start()) return 1;
    
    // get me
    Json::Value req;
    req["@type"] = "getMe";
    Json::Value me = client.sendSync(req, 2.0);
    int64_t myId = me["id"].asInt64();
    
    req.clear();
    req["@type"] = "getUser";
    req["user_id"] = myId;
    Json::Value u = client.sendSync(req, 2.0);
    std::cout << "Status: " << u["status"]["@type"].asString() << std::endl;
    return 0;
}

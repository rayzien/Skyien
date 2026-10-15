/*
 * Skyien - High-Speed C++ Telegram Account Automation CLI
 * Origin Verification Token: SKYIEN-SIG-RAYZIEN-2026-X81F
 * Architecture Lineage: rayzien / proprietary core (Open Distribution)
 * Digital Fingerprint: 534b7969-656e-5261-797a-69656e417574
 */

#include "SkyienSignature.hpp"
#include "UserMonitorDB.hpp"
#include "Config.hpp"
#include "TelegramClient.hpp"
#include "AutomationEngine.hpp"
#include "TerminalUI.hpp"
#include "BotBridge.hpp"
#include "Targetor.hpp"
#include "GroupFinder.hpp"
#include "EmojiFilter.hpp"
#include <iostream>
#include <iomanip>
#include <csignal>
#include <thread>
#include <chrono>
#include <sstream>

namespace Skyien {
std::atomic<bool> g_stopSignal{false};
}

using namespace Skyien;

void signalHandler(int) {
    Skyien::g_stopSignal.store(true);
}

void printHelp() {
    UI::printBanner();
    std::cout << UI::BOLD << "USAGE:" << UI::RESET << "\n"
              << "  skyien                               Launch Interactive Hub & Control Center\n"
              << "  skyien <command> [arguments...]\n\n"
              << UI::BOLD << "COMMANDS:" << UI::RESET << "\n"
              << "  " << UI::GREEN << "auth [phone]" << UI::RESET << "                   Authorizer: enter phone number and code directly\n"
              << "  " << UI::GREEN << "find-groups <query> [options]" << UI::RESET << "   Search unjoined groups & channels (--aim 10k, --join-all)\n"
              << "  " << UI::GREEN << "join-all <query> [options]" << UI::RESET << "       Join all found groups/channels (--resume to resume)\n"
              << "  " << UI::GREEN << "targetor [run|config]" << UI::RESET << "          Channel Listener & Escalating DM Automation\n"
              << "  " << UI::GREEN << "targetor add-channel <id>" << UI::RESET << "      Add channel to Targetor listener\n"
              << "  " << UI::GREEN << "targetor add-keyword <word>" << UI::RESET << "    Add trigger condition to Targetor\n"
              << "  " << UI::GREEN << "connect bot <token>" << UI::RESET << "         Connect a Telegram Bot (@BotFather token)\n"
              << "  " << UI::GREEN << "connect channel <id> [title]" << UI::RESET << "  Connect target Channel for logs & relays\n"
              << "  " << UI::GREEN << "connect group <id> [title]" << UI::RESET << "    Connect target Group for alerts & relays\n"
              << "  " << UI::GREEN << "broadcast <message>" << UI::RESET << "          Send message to connected Channel & Group\n"
              << "  " << UI::GREEN << "me" << UI::RESET << "                            Display Telegram profile and ID\n"
              << "  " << UI::GREEN << "chats [limit]" << UI::RESET << "                  List recent chats with IDs and unread counts\n"
              << "  " << UI::GREEN << "send <chat_id> <text>" << UI::RESET << "          Send a message to a specific chat/user/group\n"
              << "  " << UI::GREEN << "auto | daemon" << UI::RESET << "                  Start the automation engine\n"
              << "  " << UI::GREEN << "console | repl" << UI::RESET << "                 Launch interactive CLI shell\n"
              << "  " << UI::GREEN << "status" << UI::RESET << "                        Show account, bot, and channel status\n"
              << "  " << UI::GREEN << "help" << UI::RESET << "                          Show this help manual\n\n"
              << UI::DIM << "Persistence: ~/.skyien/finder_state.json, ~/.skyien/config.json\n" << UI::RESET;
}

bool ensureAuthenticated(Config& /*config*/, TelegramClient& client, const std::string& phoneArg = "") {
    if (!client.authenticate(phoneArg)) {
        UI::printError("Authentication failed.");
        return false;
    }
    return true;
}

void printDashboard(const Config& config, const UserProfile& me) {
    std::cout << UI::CYAN << UI::BOLD << "\n+----------------------------------------------------------+" << UI::RESET << "\n";
    std::cout << UI::CYAN << UI::BOLD << "|                    SKYIEN CONTROL HUB                    |" << UI::RESET << "\n";
    std::cout << UI::CYAN << UI::BOLD << "+----------------------------------------------------------+" << UI::RESET << "\n";
    std::cout << "  " << UI::BOLD << "[-] Account:   " << UI::RESET << me.firstName << " " << me.lastName 
              << " (@" << me.username << ") [ID: " << me.id << "]\n";
    std::cout << "  " << UI::BOLD << "[-] Phone:     " << UI::RESET << me.phoneNumber << "\n";
    std::cout << "  " << UI::BOLD << "[#] Bot:       " << UI::RESET 
              << (!config.botToken.empty() ? (std::string(UI::GREEN) + "@" + config.botUsername + " (Online)" + UI::RESET) : (std::string(UI::YELLOW) + "Not connected ('skyien connect bot <token>')" + UI::RESET)) << "\n";
    std::cout << "  " << UI::BOLD << "[>] Channel:   " << UI::RESET 
              << (config.connectedChannelId != 0 ? (UI::GREEN + config.connectedChannelTitle + " [" + std::to_string(config.connectedChannelId) + "]" + UI::RESET) : (std::string(UI::DIM) + "Not connected" + UI::RESET)) << "\n";
    std::cout << "  " << UI::BOLD << "[>] Group:     " << UI::RESET 
              << (config.connectedGroupId != 0 ? (UI::GREEN + config.connectedGroupTitle + " [" + std::to_string(config.connectedGroupId) + "]" + UI::RESET) : (std::string(UI::DIM) + "Not connected" + UI::RESET)) << "\n";
    std::cout << "  " << UI::BOLD << "[*] Auto-Reply:" << UI::RESET << (config.autoReplyEnabled ? (std::string(UI::GREEN) + " Active (" + std::to_string(config.autoReplyRules.size()) + " rules)" + UI::RESET) : " Disabled") << "\n";
    std::cout << "  " << UI::BOLD << "[*] AFK Mode:  " << UI::RESET << (config.afkMode ? (std::string(UI::YELLOW) + " Active" + UI::RESET) : " Inactive") << "\n";
    std::cout << "  " << UI::BOLD << "[*] Userbot:   " << UI::RESET << UI::GREEN << "Active (Commands, Sticker Hunter, Real-Time Proxy, Reels)" << UI::RESET << "\n";
    std::cout << UI::CYAN << "------------------------------------------------------------" << UI::RESET << "\n\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    Config config;
    std::string configPath = Config::getDefaultConfigPath();
    config.loadFromFile(configPath);
    config.ensureDirectories();

    TelegramClient client(config);

    // If executed without arguments, open Interactive Hub!
    if (argc == 1) {
        UI::printBanner();
        UI::printInfo("Initializing Skyien...");
        if (!ensureAuthenticated(config, client)) {
            return 1;
        }

        UserProfile me = client.getMe();
        AutomationEngine engine(client, config);
        client.setUpdateHandler([&engine](const Json::Value& u) { engine.onUpdate(u); });
        engine.start();

        while (!g_stopSignal.load()) {
            printDashboard(config, me);
            std::cout << UI::BOLD << "Choose an action:" << UI::RESET << "\n"
                      << "  [1] [+] Start Automation Daemon (Auto-reply & Relays)\n"
                      << "  [2] [>] Launch Interactive Console\n"
                      << "  [3] [TARGET] Targetor (Channel Monitor & Escalating DMs)\n"
                      << "  [4] [FIND] Group & Channel Finder (Search, 0-10k Aim Filter & Join All)\n"
                      << "  [5] [#] Connect / Update Telegram Bot\n"
                      << "  [6] [^] Connect Channel & Group\n"
                      << "  [7] [!] Broadcast Message to Channel / Group\n"
                      << "  [8] [=] List Recent Chats\n"
                      << "  [9] [>] Send a Message\n"
                      << "  [10] [*] Toggle AFK Mode\n"

                      << "  [11] [M] Check Inbox (Broadcast Replies)\n"

                      << "  [0] [X] Exit\n\n";

            std::string choice = UI::prompt("Enter selection [0-10]");
            if (choice == "0" || choice == "exit" || choice == "q") {
                std::cout << "Goodbye!\n";
                engine.stop();
                break;
            } else if (choice == "1") {
                UI::printInfo("Daemon running in foreground. Userbot active. Press Ctrl+C to return to menu.");
                engine.printStatus();
                while (!g_stopSignal.load()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                g_stopSignal.store(false);
            } else if (choice == "2") {
                std::cout << UI::GREEN << "Type /help for console commands, /exit to return.\n" << UI::RESET;
                std::string line;
                while (true) {
                    std::cout << UI::BOLD << "skyien> " << UI::RESET << std::flush;
                    if (!std::getline(std::cin, line) || line == "/exit" || line == "/quit") break;
                    if (line == "/chats") {
                        auto chats = client.getChats(10);
                        for (const auto& c : chats) {
                            std::cout << "  " << c.id << "\t" << c.title << "\t(" << c.unreadCount << " unread)\n";
                        }
                    } else if (line.rfind("/send", 0) == 0) {
                        std::istringstream iss(line);
                        std::string cmd; int64_t destId;
                        iss >> cmd >> destId;
                        std::string msg; std::getline(iss, msg);
                        if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                        if (destId != 0 && !msg.empty()) client.sendMessage(destId, EmojiFilter::strip(msg));
                    } else {
                        std::cout << "Commands: /chats, /send <id> <msg>, /status, /exit\n";
                    }
                }
            } else if (choice == "3") {
                Targetor targetor(client, config);
                targetor.runInteractiveConfig();
                std::string runNow = UI::prompt("Start Targetor Listener now? (y/n)");
                if (runNow == "y" || runNow == "Y") {
                    client.setUpdateHandler([&targetor](const Json::Value& u) { targetor.onUpdate(u); });
                    targetor.start();
                    UI::printInfo("Targetor listening to channels. Press Ctrl+C to stop.");
                    while (!g_stopSignal.load()) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    g_stopSignal.store(false);
                    targetor.stop();
                    client.setUpdateHandler([&engine](const Json::Value& u) { engine.onUpdate(u); });
                }
            } else if (choice == "4") {
                GroupFinder finder(client);
                finder.runInteractiveFinder();
            } else if (choice == "5") {
                std::string token = UI::prompt("Enter Bot Token from @BotFather");
                std::string bUsername; int64_t bId = 0;
                UI::printInfo("Verifying bot token with Telegram...");
                if (BotBridge::verifyToken(token, bUsername, bId)) {
                    config.botToken = token;
                    config.botUsername = bUsername;
                    config.botId = bId;
                    config.saveToFile(configPath);
                    UI::printSuccess("Connected Bot @" + bUsername + " (ID: " + std::to_string(bId) + ")");
                } else {
                    UI::printError("Failed to verify bot token. Please check and try again.");
                }
            } else if (choice == "6") {
                std::cout << "\n" << UI::CYAN << "[Connect Channel]" << UI::RESET << "\n";
                int64_t chId = client.selectChatInteractive("Select Channel by number");
                if (chId != 0) {
                    config.connectedChannelId = chId;
                    config.connectedChannelTitle = "Channel " + std::to_string(chId);
                    config.relayToChannel = true;
                }
                std::cout << "\n" << UI::CYAN << "[Connect Group]" << UI::RESET << "\n";
                int64_t grpId = client.selectChatInteractive("Select Group by number");
                if (grpId != 0) {
                    config.connectedGroupId = grpId;
                    config.connectedGroupTitle = "Group " + std::to_string(grpId);
                    config.relayToGroup = true;
                }
                config.saveToFile(configPath);
                UI::printSuccess("Connected Channel and Group updated!");
            } else if (choice == "7") {
                std::string msg = UI::prompt("Enter message to broadcast");
                if (!msg.empty()) {
                    AutomationEngine engine(client, config);
                    engine.broadcast(EmojiFilter::strip(msg));
                }
            } else if (choice == "8") {
                auto chats = client.getChats(25);
                std::cout << "\n" << UI::CYAN << UI::BOLD << "[=== Available Chats ===]" << UI::RESET << "\n";
                if (chats.empty()) {
                    std::cout << UI::YELLOW << "[!] No chats found or Telegram sync in progress. Try again in a few moments.\n" << UI::RESET;
                } else {
                    for (size_t i = 0; i < chats.size(); ++i) {
                        std::cout << "  [" << (i + 1) << "] "
                                  << std::left << std::setw(30) << EmojiFilter::strip(chats[i].title)
                                  << " (ID: " << chats[i].id << ")";
                        if (chats[i].unreadCount > 0) {
                            std::cout << " " << UI::YELLOW << "[" << chats[i].unreadCount << " unread]" << UI::RESET;
                        }
                        std::cout << "\n";
                    }
                }
            } else if (choice == "9") {
                int64_t dest = client.selectChatInteractive("Select recipient chat by number");
                if (dest != 0) {
                    std::string msg = UI::prompt("Enter message text");
                    if (!msg.empty()) {
                        if (client.sendMessage(dest, EmojiFilter::strip(msg))) {
                            UI::printSuccess("Message sent!");
                        } else {
                            UI::printError("Failed to deliver message.");
                        }
                    }
                }
            } else if (choice == "10") {
                config.afkMode = !config.afkMode;
                config.saveToFile(configPath);
                UI::printSuccess(std::string("AFK Mode is now: ") + (config.afkMode ? "ENABLED" : "DISABLED"));
            } else if (choice == "11") {
                std::cout << "\n" << UserMonitorDB::getInstance().getInboxSummary() << "\n";
            }
        }
        return 0;
    }

    std::string command = argv[1];

    if (command == "help" || command == "--help" || command == "-h") {
        printHelp();
        return 0;
    }

    if (command == "--verify-signature" || command == "provenance" || command == "--origin") {
        std::cout << "[+] Skyien Origin Verification:\n"
                  << "    Signature Token: " << Provenance::getOriginSignature() << "\n"
                  << "    Fingerprint:     " << Provenance::getFingerprint() << "\n"
                  << "    Origin Author:   rayzien\n"
                  << "    Status:          VERIFIED ORIGINAL BUILD\n";
        return 0;
    }

    if (command == "find-groups" || command == "find" || command == "search") {
        if (!ensureAuthenticated(config, client)) return 1;
        GroupFinder finder(client);

        if (argc < 3) {
            finder.runInteractiveFinder();
            return 0;
        }

        GroupFilterCriteria criteria;
        criteria.query = argv[2];
        criteria.minMembers = 0;
        criteria.includeChannels = true;
        criteria.includeGroups = true;
        criteria.excludeJoined = true;

        bool doJoinAll = false;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--aim" || arg == "--members" || arg == "--max-members") && i + 1 < argc) {
                criteria.maxMembers = GroupFinder::parseQuantity(argv[++i]);
                criteria.minMembers = 0;
            } else if (arg == "--min-members" && i + 1 < argc) {
                criteria.minMembers = GroupFinder::parseQuantity(argv[++i]);
            } else if ((arg == "--online" || arg == "--min-online") && i + 1 < argc) {
                criteria.targetOnline = GroupFinder::parseQuantity(argv[++i]);
            } else if (arg == "--can-send") {
                criteria.requireCanSend = true;
            } else if (arg == "--join-all") {
                doJoinAll = true;
            } else if (arg == "--pacing" && i + 1 < argc) {
                std::string pMode = argv[++i];
                auto& pcfg = finder.getPacingConfig();
                if (pMode == "min10" || pMode == "1") pcfg.mode = PacingMode::MIN_10_MINUTES;
                else if (pMode == "random" || pMode == "random_4m_1h" || pMode == "2") pcfg.mode = PacingMode::RANDOM_4M_TO_1H;
                else if (pMode == "custom" || pMode == "3") pcfg.mode = PacingMode::CUSTOM_FIXED;
                else if (pMode == "custom-random" || pMode == "4") pcfg.mode = PacingMode::CUSTOM_RANDOM;
                finder.saveState();
            } else if (arg == "--delay" && i + 1 < argc) {
                finder.getPacingConfig().mode = PacingMode::CUSTOM_FIXED;
                finder.getPacingConfig().customFixedSec = std::max(1, std::stoi(argv[++i]));
                finder.saveState();
            } else if (arg == "--min-delay" && i + 1 < argc) {
                finder.getPacingConfig().mode = PacingMode::CUSTOM_RANDOM;
                finder.getPacingConfig().customMinSec = std::max(1, std::stoi(argv[++i]));
                finder.saveState();
            } else if (arg == "--max-delay" && i + 1 < argc) {
                finder.getPacingConfig().mode = PacingMode::CUSTOM_RANDOM;
                finder.getPacingConfig().customMaxSec = std::max(1, std::stoi(argv[++i]));
                finder.saveState();
            }
        }

        auto results = finder.search(criteria);
        GroupFinder::printResults(results);

        if (doJoinAll && !results.empty()) {
            finder.joinAll(results);
        }
        return 0;
    }

    if (command == "join-all" || command == "join") {
        if (!ensureAuthenticated(config, client)) return 1;
        GroupFinder finder(client);

        if (argc > 2 && std::string(argv[2]) == "--resume") {
            finder.resumeJoinQueue();
            return 0;
        }

        if (argc < 3) {
            UI::printInfo("Usage: skyien join-all <query> [--aim 10k] [--pacing min10|random|custom] OR skyien join-all --resume");
            finder.resumeJoinQueue();
            return 0;
        }

        GroupFilterCriteria criteria;
        criteria.query = argv[2];
        criteria.minMembers = 0;
        criteria.includeChannels = true;
        criteria.includeGroups = true;
        criteria.excludeJoined = true;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--aim" || arg == "--members" || arg == "--max-members") && i + 1 < argc) {
                criteria.maxMembers = GroupFinder::parseQuantity(argv[++i]);
            } else if (arg == "--pacing" && i + 1 < argc) {
                std::string pMode = argv[++i];
                auto& pcfg = finder.getPacingConfig();
                if (pMode == "min10" || pMode == "1") pcfg.mode = PacingMode::MIN_10_MINUTES;
                else if (pMode == "random" || pMode == "2") pcfg.mode = PacingMode::RANDOM_4M_TO_1H;
                else if (pMode == "custom" || pMode == "3") pcfg.mode = PacingMode::CUSTOM_FIXED;
                else if (pMode == "custom-random" || pMode == "4") pcfg.mode = PacingMode::CUSTOM_RANDOM;
                finder.saveState();
            }
        }

        auto results = finder.search(criteria);
        GroupFinder::printResults(results);
        if (!results.empty()) {
            finder.joinAll(results);
        }
        return 0;
    }

    if (command == "targetor") {
        if (!ensureAuthenticated(config, client)) return 1;
        Targetor targetor(client, config);

        std::string sub = (argc > 2) ? argv[2] : "";

        if (sub == "run") {
            client.setUpdateHandler([&targetor](const Json::Value& u) { targetor.onUpdate(u); });
            targetor.start();
            UI::printInfo("Targetor listening to channels in background. Press Ctrl+C to stop.");
            while (!g_stopSignal.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            targetor.stop();
            return 0;
        } else if (sub == "add-channel") {
            if (argc < 4) {
                UI::printError("Usage: skyien targetor add-channel <channel_id>");
                return 1;
            }
            targetor.addMonitoredChannel(std::stoll(argv[3]));
            return 0;
        } else if (sub == "add-keyword") {
            if (argc < 4) {
                UI::printError("Usage: skyien targetor add-keyword <word>");
                return 1;
            }
            targetor.addKeyword(argv[3]);
            return 0;
        } else {
            targetor.runInteractiveConfig();
            std::string runNow = UI::prompt("Run Targetor listening engine now? (y/n)");
            if (runNow == "y" || runNow == "Y") {
                client.setUpdateHandler([&targetor](const Json::Value& u) { targetor.onUpdate(u); });
                targetor.start();
                UI::printInfo("Targetor active. Press Ctrl+C to stop.");
                while (!g_stopSignal.load()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                targetor.stop();
            }
            return 0;
        }
    }

    if (command == "auth" || command == "login") {
        UI::printBanner();
        std::string phoneArg;
        if (argc > 2) phoneArg = argv[2];

        if (ensureAuthenticated(config, client, phoneArg)) {
            UserProfile me = client.getMe();
            std::cout << "\n" << UI::CYAN << UI::BOLD << "+-------------------------------------+" << UI::RESET << "\n";
            std::cout << UI::GREEN << UI::BOLD << "   AUTHORIZATION COMPLETE!  " << UI::RESET << "\n";
            std::cout << "+-------------------------------------+\n";
            std::cout << "  User ID:      " << me.id << "\n";
            std::cout << "  Name:         " << me.firstName << " " << me.lastName << "\n";
            std::cout << "  Username:     @" << me.username << "\n";
            std::cout << "  Phone:        " << me.phoneNumber << "\n";
            std::cout << "+-------------------------------------+\n\n";
            UI::printSuccess("Session stored in: " + config.sessionDirectory);
            UI::printInfo("Type 'skyien' to launch the Control Hub anytime.");

            if (!me.phoneNumber.empty()) {
                config.phoneNumber = me.phoneNumber;
                config.saveToFile(configPath);
            }
        }
        return 0;
    }

    if (command == "connect") {
        if (argc < 3) {
            UI::printError("Usage: skyien connect <bot|channel|group> [arguments...]");
            return 1;
        }
        std::string target = argv[2];
        if (target == "bot") {
            std::string token;
            if (argc > 3) token = argv[3];
            else token = UI::prompt("Enter Bot Token from @BotFather");

            std::string bUsername; int64_t bId = 0;
            UI::printInfo("Verifying bot token...");
            if (BotBridge::verifyToken(token, bUsername, bId)) {
                config.botToken = token;
                config.botUsername = bUsername;
                config.botId = bId;
                config.saveToFile(configPath);
                UI::printSuccess("Connected Bot @" + bUsername + " (ID: " + std::to_string(bId) + ")");
            } else {
                UI::printError("Invalid bot token or unable to reach Telegram Bot API.");
                return 1;
            }
            return 0;
        } else if (target == "channel") {
            if (argc < 4) {
                UI::printError("Usage: skyien connect channel <channel_id> [title]");
                return 1;
            }
            config.connectedChannelId = std::stoll(argv[3]);
            if (argc > 4) config.connectedChannelTitle = EmojiFilter::strip(argv[4]);
            else config.connectedChannelTitle = "Channel " + std::to_string(config.connectedChannelId);
            config.relayToChannel = true;
            config.saveToFile(configPath);
            UI::printSuccess("Channel connected: " + config.connectedChannelTitle + " (" + std::to_string(config.connectedChannelId) + ")");
            return 0;
        } else if (target == "group") {
            if (argc < 4) {
                UI::printError("Usage: skyien connect group <group_id> [title]");
                return 1;
            }
            config.connectedGroupId = std::stoll(argv[3]);
            if (argc > 4) config.connectedGroupTitle = EmojiFilter::strip(argv[4]);
            else config.connectedGroupTitle = "Group " + std::to_string(config.connectedGroupId);
            config.relayToGroup = true;
            config.saveToFile(configPath);
            UI::printSuccess("Group connected: " + config.connectedGroupTitle + " (" + std::to_string(config.connectedGroupId) + ")");
            return 0;
        } else {
            UI::printError("Unknown target: " + target + ". Use bot, channel, or group.");
            return 1;
        }
    }

    if (command == "broadcast") {
        if (argc < 3) {
            UI::printError("Usage: skyien broadcast <message text...>");
            return 1;
        }
        std::ostringstream ss;
        for (int i = 2; i < argc; ++i) {
            if (i > 2) ss << " ";
            ss << argv[i];
        }
        std::string message = EmojiFilter::strip(ss.str());
        AutomationEngine engine(client, config);
        engine.broadcast(message);
        return 0;
    }

    if (command == "me") {
        if (!ensureAuthenticated(config, client)) return 1;
        UserProfile me = client.getMe();
        std::cout << "\n" << UI::CYAN << UI::BOLD << "[=== Telegram Profile ===]" << UI::RESET << "\n";
        std::cout << "User ID:      " << me.id << "\n";
        std::cout << "First Name:   " << me.firstName << "\n";
        std::cout << "Last Name:    " << me.lastName << "\n";
        std::cout << "Username:     @" << me.username << "\n";
        std::cout << "Phone:        " << me.phoneNumber << "\n";
        std::cout << UI::CYAN << "=========================" << UI::RESET << "\n";
        return 0;
    }

    if (command == "chats") {
        if (!ensureAuthenticated(config, client)) return 1;
        int limit = 20;
        if (argc > 2) {
            try { limit = std::stoi(argv[2]); } catch (...) {}
        }
        UI::printInfo("Fetching recent " + std::to_string(limit) + " chats...");
        auto chats = client.getChats(limit);

        std::cout << "\n" << UI::CYAN << UI::BOLD << "+-------------------------------------------------------------------------+" << UI::RESET << "\n";
        std::cout << UI::CYAN << UI::BOLD << "|                              AVAILABLE CHATS                            |" << UI::RESET << "\n";
        std::cout << UI::CYAN << UI::BOLD << "+-------------------------------------------------------------------------+" << UI::RESET << "\n";
        std::cout << UI::BOLD
                  << std::left << std::setw(6)  << "#"
                  << std::left << std::setw(18) << "CHAT ID"
                  << std::left << std::setw(30) << "TITLE"
                  << std::left << std::setw(10) << "UNREAD"
                  << "TYPE" << UI::RESET << "\n";
        std::cout << std::string(75, '-') << "\n";

        for (size_t i = 0; i < chats.size(); ++i) {
            const auto& c = chats[i];
            std::cout << std::left << std::setw(6)  << ("[" + std::to_string(i + 1) + "]")
                      << std::left << std::setw(18) << c.id
                      << std::left << std::setw(30) << (c.title.size() > 28 ? c.title.substr(0, 25) + "..." : c.title)
                      << std::left << std::setw(10) << (c.unreadCount > 0 ? (UI::YELLOW + std::to_string(c.unreadCount) + UI::RESET) : "0")
                      << c.type << "\n";
        }
        std::cout << std::string(75, '-') << "\n";
        return 0;
    }

    if (command == "send") {
        if (!ensureAuthenticated(config, client)) return 1;

        int64_t chatId = 0;
        std::string message;

        if (argc < 3) {
            chatId = client.selectChatInteractive("Select recipient chat by number");
            if (chatId == 0) return 0;
            message = UI::prompt("Enter message text");
        } else {
            try {
                chatId = std::stoll(argv[2]);
            } catch (...) {
                UI::printError("Invalid Chat ID format: " + std::string(argv[2]));
                return 1;
            }
            if (argc < 4) {
                message = UI::prompt("Enter message text");
            } else {
                std::ostringstream ss;
                for (int i = 3; i < argc; ++i) {
                    if (i > 3) ss << " ";
                    ss << argv[i];
                }
                message = ss.str();
            }
        }

        std::string cleanMessage = EmojiFilter::strip(message);
        if (cleanMessage.empty()) {
            UI::printWarning("Cannot send empty message.");
            return 1;
        }

        UI::printInfo("Sending message to chat " + std::to_string(chatId) + "...");
        if (client.sendMessage(chatId, cleanMessage)) {
            UI::printSuccess("Message dispatched successfully!");
        } else {
            UI::printError("Failed to deliver message.");
            return 1;
        }
        return 0;
    }

    if (command == "auto" || command == "daemon") {
        UI::printBanner();
        if (!ensureAuthenticated(config, client)) return 1;

        AutomationEngine engine(client, config);
        client.setUpdateHandler([&engine](const Json::Value& u) {
            engine.onUpdate(u);
        });

        engine.start();
        UI::printInfo("Automation daemon running. Press Ctrl+C to stop.");
        engine.printStatus();

        while (!g_stopSignal.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "\n";
        UI::printInfo("Stopping automation daemon...");
        engine.stop();
        return 0;
    }

    if (command == "status") {
        UserProfile me;
        if (client.authenticate()) {
            me = client.getMe();
        }
        printDashboard(config, me);
        return 0;
    }

    UI::printError("Unknown command '" + command + "'. Run 'skyien help' for usage.");
    return 1;
}

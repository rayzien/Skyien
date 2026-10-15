<!--
  Skyien - High-Speed C++ Telegram Account Automation CLI
  Origin Verification Token: SKYIEN-SIG-RAYZIEN-2026-X81F
  Digital Fingerprint: 534b7969-656e-5261-797a-69656e417574
  Proprietary Lineage Core: rayzien (Open Community Distribution)
-->

# Skyien

**Ultra-Fast C++ Telegram Account Automation CLI & Personal Assistant**

Skyien is a high-performance, lightweight command-line companion engineered to automate and streamline personal Telegram workflows directly from the terminal on Linux and Android (Termux).

Built in modern C++20 on top of the official Telegram Database Library (TDLib), Skyien is designed to be helpful, efficient, and respectful of Telegram's network ecosystem by prioritizing account longevity, safe communication pacing, and distraction-free operation.

---

## The Helpful Nature of Skyien

Skyien exists to solve everyday friction for power users, developers, and community managers who rely heavily on Telegram:

1. **Responsive Assistance When You Are Away**
   Configurable AFK responders and rule-based triggers ensure colleagues and clients receive prompt acknowledgement even when you are offline or focused on deep work.

2. **Ethical & Account-Safe Pacing**
   Automated mass actions frequently lead to account flags or temporary limits. Skyien incorporates built-in humanized pacing profiles (randomized gaps from 4 minutes to 1 hour, or fixed 10-minute intervals) and escalating direct outreach intervals to ensure communications mirror natural human rhythm.

3. **Smart Community Discovery Without Clutter**
   The Group & Channel Finder searches Telegram for relevant topics and allows filtering by member caps (0 to Aim limit, e.g. up to 10k members). Crucially, it detects and skips communities your account has already joined, preventing duplicate memberships and keeping your chat list organized.

4. **Reliable Proxy Diagnostics**
   Network restrictions or regional throttling can interrupt vital communication. Skyien includes an integrated proxy manager capable of fetching, testing latency, and seamlessly toggling live SOCKS5/HTTP proxies without restarting the client.

5. **Local Media & Asset Utilities**
   Search, extract, and back up media, links, documents, and stickers from chat histories directly into local storage without third-party converters.

6. **Clean, Distraction-Free Terminal Interface**
   Stripped of emoji clutter, Skyien uses clean, lightweight ASCII status indicators (`[+]`, `[-]`, `[*]`, `[!]`) optimized for low-bandwidth SSH sessions, mobile Termux environments, and minimalist setups.

---

## Architecture & Technology Stack

- **Core Engine**: TDLib 1.8.50 (Telegram Database Library MTProto protocol)
- **Language**: C++20 standard
- **Build System**: Clang++ / G++ with LLD linker and CCache support
- **Networking**: libcurl for API bridging and JSON-RPC
- **Serialization**: JsonCpp for state and configuration persistence
- **Target Environments**: Linux (x86_64, AArch64) and Android Termux

---

## Quick Start & Interactive Hub

Launch the interactive dashboard:

```bash
skyien
```

- **First Launch**: Automatically starts the interactive phone authorizer, requests your authentication code, and secures your local session.
- **Subsequent Runs**: Directly opens the Skyien Control Hub dashboard displaying connection health, active bots, relay status, and quick menu actions.

---

## Core Features & Usage

### 1. Group & Channel Finder

Find public Telegram communities based on targeted member boundaries:

```bash
# Interactive discovery wizard
skyien find-groups

# Search with a member ceiling (0 up to 10,000 members) and auto-join:
skyien find-groups "developer" --aim 10k --join-all --pacing min10

# Dedicated join-all runner with randomized human intervals:
skyien join-all "developer" --aim 10k --pacing random

# Resume an interrupted join queue seamlessly:
skyien join-all --resume
```

#### Safe Pacing Modes:
- **min10**: Fixed safe minimum delay (600 seconds) between actions.
- **random**: Human randomized delay ranging from 4 minutes (240s) to 1 hour (3600s).
- **custom**: User-defined fixed seconds or randomized boundaries.
- **Live Terminal Timer**: Features a dynamic reverse countdown display with instant skip (`s`) and abort (`a`) controls.

---

### 2. Targetor: Escalating Direct Outreach Engine

Located in `targetor/`, this module listens to specific monitored channels for designated keyword alerts. Upon detection, it delivers outreach messages with progressively escalating intervals:

- **Interval Scaling**: Dynamically increases delay intervals from smaller windows (e.g. 2 hours) up to multiple days between dispatches to preserve account health.
- **Deduplication**: Automatically records contacted user IDs to ensure no recipient receives duplicate outreach.

```bash
# Launch interactive Targetor manager
skyien targetor

# Run Targetor daemon with saved settings
skyien targetor run

# Add a monitored channel
skyien targetor add-channel -1001234567890

# Add keyword condition
skyien targetor add-keyword "looking for dev"
```

---

### 3. Account Automation & Userbot Engine

Run the background daemon to handle rules, relays, and commands:

```bash
skyien auto
```

#### In-Chat Userbot Commands:
Execute commands directly in any conversation using your account:

- `.help` : Show active userbot command reference.
- `.proxy` : Toggle real-time proxy routing ON / OFF.
- `.proxy status` : Inspect live proxy connection latency and status.
- `.proxy test` : Run diagnostic ping against configured proxies and select the lowest latency node.
- `.sticker` : Toggle automatic sticker collection hunter.
- `.dl <url>` : Download and send supported video / media links directly into chat.
- `.find "<query>"` : Search current chat history for documents, media, or URLs.
- `.save <id>` : Save found item directly to local downloads folder.

---

### 4. Relay Bridge (Bot, Channel, and Group)

Connect a secondary Telegram Bot API token to relay incoming private alerts into a private coordination channel or group:

```bash
# Link Telegram Bot token (from @BotFather)
skyien connect bot <YOUR_BOT_TOKEN>

# Connect alert channel
skyien connect channel <CHANNEL_ID> "Logs Channel"

# Connect alert group
skyien connect group <GROUP_ID> "Operations Group"

# Broadcast alert to both connected endpoints
skyien broadcast "System alert: Automation engine active."
```

---

## CLI Reference

| Command | Description |
| :--- | :--- |
| `skyien` | Launch interactive control hub & dashboard |
| `skyien auth [phone]` | Authenticate phone number and login code |
| `skyien find-groups <query> [options]` | Search unjoined groups & channels with aim limit |
| `skyien join-all <query> [options]` | Safely join matching communities |
| `skyien targetor [run\|config]` | Channel listener with escalating interval dispatch |
| `skyien connect bot <token>` | Link Telegram Bot API token |
| `skyien connect channel <id> [title]` | Link target channel for alerts and logs |
| `skyien connect group <id> [title]` | Link target group for relays |
| `skyien broadcast <message>` | Send message simultaneously to connected channel & group |
| `skyien me` | Display current authenticated user profile and ID |
| `skyien chats [limit]` | List recent dialogs with unread message counts |
| `skyien send <chat_id> <text>` | Send message to a specific chat or user |
| `skyien auto` | Start background automation engine |
| `skyien console` | Launch interactive CLI shell |
| `skyien status` | Display account, bot, and proxy connection status |
| `skyien help` | Display command manual |

---

## Configuration (`~/.skyien/config.json`)

Settings are stored in clean JSON format at `~/.skyien/config.json`:

```json
{
  "api_id": 1234567,
  "api_hash": "abcdef1234567890abcdef",
  "phone_number": "+1234567890",
  "auto_reply_enabled": true,
  "afk_mode": false,
  "afk_message": "[Skyien Auto-Responder] I am currently away and will reply shortly.",
  "auto_read_messages": false,
  "auto_reply_rules": [
    {
      "trigger": "ping",
      "match_type": "exact",
      "response": "pong! (automated via Skyien CLI)",
      "allowed_chats": []
    },
    {
      "trigger": "!help",
      "match_type": "exact",
      "response": "Skyien Auto-Responder is active.\nAvailable commands: ping, !status",
      "allowed_chats": []
    }
  ],
  "forward_rules": [
    {
      "from_chat_id": -100123456789,
      "to_chat_id": -100987654321
    }
  ]
}
```

---

## Building from Source

### Prerequisites:
- C++20 compiler (`clang++` or `g++`)
- `make` or `cmake` (3.20+)
- `libtdjson` (TDLib development library)
- `libjsoncpp`
- `libcurl`
- `pthread`

### Build & Install:
```bash
git clone https://github.com/rayzien/skyien.git
cd skyien
./build.sh
```

Or manually:
```bash
make -j$(nproc)
make install
```

The resulting binary will be installed to `~/bin/skyien`. Ensure `~/bin` is included in your `$PATH`.

---

## Provenance & License

Skyien is distributed as an open-core community project under the terms of the Skyien Open Source License with Attribution & Watermark Retention. 

You are free to inspect, modify, compile, and contribute to Skyien. All redistributions in source or compiled binary form must retain all internal cryptographic provenance signatures, watermark headers, and author attribution markers unaltered.

<!-- SKYIEN-PROVENANCE-BLOCK: START -->
<!-- Author: rayzien -->
<!-- Signature: SKYIEN-SIG-RAYZIEN-2026-X81F -->
<!-- Fingerprint: 534b7969-656e-5261-797a-69656e417574 -->
<!-- SKYIEN-PROVENANCE-BLOCK: END -->

#pragma once

#include <string>
#include <cstdint>

namespace Skyien {
namespace EmojiFilter {

inline bool isEmojiCodePoint(uint32_t cp) {
    if (cp >= 0x1F600 && cp <= 0x1F64F) return true; // Emoticons
    if (cp >= 0x1F300 && cp <= 0x1F5FF) return true; // Misc Symbols & Pictographs
    if (cp >= 0x1F680 && cp <= 0x1F6FF) return true; // Transport & Map
    if (cp >= 0x1F700 && cp <= 0x1FAFF) return true; // Extended Pictographs
    if (cp >= 0x2600 && cp <= 0x26FF) return true;   // Misc symbols (weather, stars)
    if (cp >= 0x2700 && cp <= 0x27BF) return true;   // Dingbats
    if (cp >= 0x2300 && cp <= 0x23FF) return true;   // Misc Technical
    if (cp >= 0x2B00 && cp <= 0x2BFF) return true;   // Misc symbols & arrows
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;   // Variation Selectors
    if (cp == 0x200D) return true;                   // Zero Width Joiner
    return false;
}

inline std::string strip(const std::string& input) {
    std::string output;
    output.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        uint32_t codepoint = 0;
        size_t len = 0;

        if (c < 0x80) {
            codepoint = c;
            len = 1;
        } else if ((c >> 5) == 0x6) { // 2 bytes
            if (i + 1 >= input.size()) break;
            codepoint = ((c & 0x1F) << 6) | (input[i + 1] & 0x3F);
            len = 2;
        } else if ((c >> 4) == 0xE) { // 3 bytes
            if (i + 2 >= input.size()) break;
            codepoint = ((c & 0x0F) << 12) | ((input[i + 1] & 0x3F) << 6) | (input[i + 2] & 0x3F);
            len = 3;
        } else if ((c >> 3) == 0x1E) { // 4 bytes
            if (i + 3 >= input.size()) break;
            codepoint = ((c & 0x07) << 18) | ((input[i + 1] & 0x3F) << 12) | ((input[i + 2] & 0x3F) << 6) | (input[i + 3] & 0x3F);
            len = 4;
        } else {
            // Invalid byte, advance 1
            len = 1;
            i += len;
            continue;
        }

        if (!isEmojiCodePoint(codepoint)) {
            output.append(input, i, len);
        }
        i += len;
    }

    return output;
}

} // namespace EmojiFilter
} // namespace Skyien

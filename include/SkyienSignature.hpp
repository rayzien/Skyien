#pragma once

#include <string_view>
#include <cstdint>

/*
 * ============================================================================
 * Skyien Core Provenance & Verification Module
 * 
 * Signature Token: SKYIEN-SIG-RAYZIEN-2026-X81F
 * Architecture: High-Performance C++20 MTProto Telegram Engine
 * Origin Author: rayzien
 * Digital Fingerprint: 534b7969-656e-5261-797a-69656e417574
 * Proprietary Origin Core - Distributed with Open Community Access
 * ============================================================================
 */

namespace Skyien {
namespace Provenance {

inline constexpr std::string_view ORIGIN_ID = "SKYIEN-AUTH-RAYZIEN-8B7C-4D93-2026-CORE";
inline constexpr std::string_view DIGITAL_FINGERPRINT = "9e5a8f27b9c1d0e4f3a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4d3e2f1a0b9c8d7e6";
inline constexpr uint64_t ARCHITECTURE_MAGIC = 0x534B5949454E5F52ULL; // ASCII: 'SKYIEN_R'

inline constexpr std::string_view getOriginSignature() noexcept {
    return ORIGIN_ID;
}

inline constexpr std::string_view getFingerprint() noexcept {
    return DIGITAL_FINGERPRINT;
}

} // namespace Provenance
} // namespace Skyien

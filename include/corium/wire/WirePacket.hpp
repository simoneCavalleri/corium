#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace corium::wire {

/// @brief Default magic header identifier for Corium binary wire packets (0xC041).
inline constexpr uint16_t CORIUM_WIRE_MAGIC = 0xC041;

/// @brief Calculate CRC-16-CCITT checksum over a byte span without lookup tables.
[[nodiscard]] constexpr uint16_t calculateCrc16(std::span<const uint8_t> data) noexcept {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= static_cast<uint16_t>(byte) << 8;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

/// @brief Current schema version for Corium binary wire packets.
inline constexpr uint8_t CORIUM_WIRE_VERSION = 1;

/// @brief Header structure framing binary wire packets for serial, CAN, SPI, or network transport.
#pragma pack(push, 1)
struct WireHeader {
    uint16_t magic{CORIUM_WIRE_MAGIC};
    uint8_t version{CORIUM_WIRE_VERSION};
    uint8_t typeIndex{0};
    uint8_t flags{0};
    uint8_t reserved{0};
    uint16_t payloadLength{0};
    uint16_t checksum{0};
};
#pragma pack(pop)

/// @ingroup wire
/// @brief Statically-sized zero-heap binary wire packet.
/// @tparam MaxPayloadSize Maximum payload capacity in bytes (default: 64).
template <size_t MaxPayloadSize = 64>
struct WirePacket {
    WireHeader header{};
    std::array<uint8_t, MaxPayloadSize> payload{};

    constexpr WirePacket() = default;

    /// @brief Finalize packet header, set payload length and calculate CRC16 checksum.
    void finalize(uint8_t typeIdx, uint16_t length, uint8_t flags = 0, uint8_t version = CORIUM_WIRE_VERSION) noexcept {
        header.magic = CORIUM_WIRE_MAGIC;
        header.version = version;
        header.typeIndex = typeIdx;
        header.flags = flags;
        header.reserved = 0;
        header.payloadLength = length > MaxPayloadSize ? static_cast<uint16_t>(MaxPayloadSize) : length;
        header.checksum = calculateCrc16(std::span<const uint8_t>(payload.data(), header.payloadLength));
    }

    /// @brief Validate packet magic identifier, schema version, payload length bounds, and CRC16 checksum.
    [[nodiscard]] bool isValid() const noexcept {
        if (header.magic != CORIUM_WIRE_MAGIC) {
            return false;
        }
        if (header.version != CORIUM_WIRE_VERSION) {
            return false;
        }
        if (header.payloadLength > MaxPayloadSize) {
            return false;
        }
        uint16_t expected = calculateCrc16(std::span<const uint8_t>(payload.data(), header.payloadLength));
        return header.checksum == expected;
    }

    /// @brief Total serialized wire size in bytes (header + payload length).
    [[nodiscard]] constexpr size_t totalWireSize() const noexcept {
        return sizeof(WireHeader) + header.payloadLength;
    }
};

} // namespace corium::wire

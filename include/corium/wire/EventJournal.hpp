/**
 * @file EventJournal.hpp
 * @ingroup wire
 * @brief Zero-heap binary event journal for deterministic recording and replay.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

#include "corium/internal/VariantIndex.hpp"
#include "corium/policies/QueuePolicies.hpp"
#include "corium/wire/WirePacket.hpp"

namespace corium::wire {

/// @brief Magic identifier for Corium binary event journals ("JOUR" in hex: 0x4a4f5552).
inline constexpr uint32_t CORIUM_JOURNAL_MAGIC = 0x4a4f5552;

/// @brief Current schema version for Corium event journals.
inline constexpr uint32_t CORIUM_JOURNAL_VERSION = 1;

/// @brief Computes a deterministic 64-bit ABI hash for an EventVariant type.
template <typename EventVariant>
[[nodiscard]] constexpr uint64_t computeVariantSchemaHash() noexcept {
    constexpr size_t numTypes = std::variant_size_v<EventVariant>;
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a 64-bit basis
    hash ^= static_cast<uint64_t>(numTypes);
    hash *= 0x100000001b3ULL;
    return hash;
}

#pragma pack(push, 1)

/// @brief Header placed at the beginning of an event journal binary stream.
struct JournalHeader {
    uint32_t magic{CORIUM_JOURNAL_MAGIC};
    uint32_t version{CORIUM_JOURNAL_VERSION};
    uint64_t schemaHash{0};
    uint32_t recordCount{0};
    uint32_t reserved{0};
};

/// @brief Header preceding every serialized event record in the journal.
struct JournalRecordHeader {
    uint64_t timestampUs{0};
    uint32_t typeIndex{0};
    uint32_t payloadLength{0};
    uint16_t checksum{0};
    uint8_t priority{static_cast<uint8_t>(EventPriority::Normal)};
    uint8_t typeSignature{0};
};

#pragma pack(pop)

/// @ingroup wire
/// @brief Statically allocated binary event journal writer for zero-heap post-mortem logging and record playback.
/// @tparam EventVariant Variant of supported event types.
/// @tparam BufferCapacity Total byte capacity of the journal storage buffer.
template <typename EventVariant, size_t BufferCapacity = 4096>
class EventJournalWriter {
public:
    constexpr EventJournalWriter() noexcept {
        initHeader();
    }

    /// @brief Reset journal to initial empty state.
    void reset() noexcept {
        m_offset = 0;
        m_recordCount = 0;
        initHeader();
    }

    /// @brief Record a concrete typed event into the journal.
    /// @tparam Event Concrete event struct type (must be trivially copyable).
    /// @param event Event instance to record.
    /// @param timestampUs Timestamp in microseconds.
    /// @param priority Priority of the event.
    /// @return true if record was successfully written; false if journal is full.
    template <typename Event>
    bool record(const Event& event, uint64_t timestampUs, EventPriority priority = EventPriority::Normal) noexcept {
        static_assert(std::is_trivially_copyable_v<Event>, "Event must be trivially copyable for journal serialization.");
        constexpr size_t typeIdx = corium::variant_index_v<Event, EventVariant>;
        static_assert(typeIdx != static_cast<size_t>(-1), "Event type is not in the specified EventVariant.");

        constexpr size_t recordSize = sizeof(JournalRecordHeader) + sizeof(Event);
        if (m_offset + recordSize > BufferCapacity) {
            return false;
        }

        JournalRecordHeader recHeader{};
        recHeader.timestampUs = timestampUs;
        recHeader.typeIndex = static_cast<uint32_t>(typeIdx);
        recHeader.payloadLength = static_cast<uint32_t>(sizeof(Event));
        recHeader.priority = static_cast<uint8_t>(priority);
        recHeader.typeSignature = computeTypeSignature<Event>();
        recHeader.checksum = calculateCrc16(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(&event), sizeof(Event)));

        // Write record header
        std::memcpy(&m_buffer[m_offset], &recHeader, sizeof(JournalRecordHeader));
        m_offset += sizeof(JournalRecordHeader);

        // Write event payload
        std::memcpy(&m_buffer[m_offset], &event, sizeof(Event));
        m_offset += sizeof(Event);

        m_recordCount++;
        updateHeaderCount();
        return true;
    }

    /// @brief Number of records written.
    [[nodiscard]] size_t recordCount() const noexcept {
        return m_recordCount;
    }

    /// @brief Total bytes written into buffer (header + all records).
    [[nodiscard]] size_t bytesWritten() const noexcept {
        return m_offset;
    }

    /// @brief Read-only view of the serialized journal data.
    [[nodiscard]] std::span<const uint8_t> data() const noexcept {
        return std::span<const uint8_t>(m_buffer.data(), m_offset);
    }

private:
    void initHeader() noexcept {
        JournalHeader hdr{};
        hdr.magic = CORIUM_JOURNAL_MAGIC;
        hdr.version = CORIUM_JOURNAL_VERSION;
        hdr.schemaHash = computeVariantSchemaHash<EventVariant>();
        hdr.recordCount = 0;
        hdr.reserved = 0;
        std::memcpy(&m_buffer[0], &hdr, sizeof(JournalHeader));
        m_offset = sizeof(JournalHeader);
    }

    void updateHeaderCount() noexcept {
        auto* hdr = reinterpret_cast<JournalHeader*>(&m_buffer[0]);
        hdr->recordCount = static_cast<uint32_t>(m_recordCount);
    }

    std::array<uint8_t, BufferCapacity> m_buffer{};
    size_t m_offset{0};
    size_t m_recordCount{0};
};

/// @ingroup wire
/// @brief Zero-heap event journal reader and deterministic player into Corium EventSinks.
/// @tparam EventVariant Variant of supported event types.
template <typename EventVariant>
class EventJournalReader {
public:
    /// @brief Construct reader over a byte span.
    explicit EventJournalReader(std::span<const uint8_t> journalData) noexcept
        : m_data(journalData) {
        validateAndParseHeader();
    }

    /// @brief Returns true if the journal header is valid (magic, version, schema match).
    [[nodiscard]] bool isValid() const noexcept {
        return m_valid;
    }

    /// @brief Total number of records declared in the header.
    [[nodiscard]] size_t totalRecords() const noexcept {
        return m_valid ? m_header.recordCount : 0;
    }

    /// @brief Rewind playback cursor to the first record.
    void rewind() noexcept {
        m_cursor = sizeof(JournalHeader);
    }

    /// @brief Replay all valid records in the journal directly into an EventSink.
    /// @tparam Sink EventSink or EventBus handle type.
    /// @param sink Target sink to receive replayed events.
    /// @return Number of events successfully replayed.
    template <typename Sink>
    size_t replayInto(Sink& sink) noexcept {
        if (!m_valid) {
            return 0;
        }

        rewind();
        size_t replayed = 0;

        while (m_cursor + sizeof(JournalRecordHeader) <= m_data.size()) {
            JournalRecordHeader recHeader{};
            std::memcpy(&recHeader, &m_data[m_cursor], sizeof(JournalRecordHeader));

            size_t payloadStart = m_cursor + sizeof(JournalRecordHeader);
            if (payloadStart + recHeader.payloadLength > m_data.size()) {
                break; // Truncated record
            }

            // Verify CRC
            uint16_t expectedCrc = calculateCrc16(std::span<const uint8_t>(
                &m_data[payloadStart], recHeader.payloadLength));
            if (recHeader.checksum != expectedCrc) {
                break; // Corrupted record
            }

            // Deserialize and push
            constexpr size_t numTypes = std::variant_size_v<EventVariant>;
            if (recHeader.typeIndex < numTypes) {
                bool pushed = deserializeIndex<Sink>(
                    recHeader,
                    &m_data[payloadStart],
                    sink,
                    std::make_index_sequence<numTypes>{}
                );
                if (pushed) {
                    replayed++;
                }
            }

            m_cursor = payloadStart + recHeader.payloadLength;
        }

        return replayed;
    }

private:
    void validateAndParseHeader() noexcept {
        if (m_data.size() < sizeof(JournalHeader)) {
            m_valid = false;
            return;
        }

        std::memcpy(&m_header, m_data.data(), sizeof(JournalHeader));

        if (m_header.magic != CORIUM_JOURNAL_MAGIC) {
            m_valid = false;
            return;
        }
        if (m_header.version != CORIUM_JOURNAL_VERSION) {
            m_valid = false;
            return;
        }
        if (m_header.schemaHash != computeVariantSchemaHash<EventVariant>()) {
            m_valid = false;
            return;
        }

        m_valid = true;
        m_cursor = sizeof(JournalHeader);
    }

    template <typename Sink, size_t... Is>
    bool deserializeIndex(
        const JournalRecordHeader& recHeader,
        const uint8_t* payload,
        Sink& sink,
        std::index_sequence<Is...>
    ) noexcept {
        bool handled = false;
        (void)((recHeader.typeIndex == Is ? (handled = deserializeExact<Is, Sink>(recHeader, payload, sink), true) : false) || ...);
        return handled;
    }

    template <size_t Index, typename Sink>
    bool deserializeExact(
        const JournalRecordHeader& recHeader,
        const uint8_t* payload,
        Sink& sink
    ) noexcept {
        using TargetEvent = std::variant_alternative_t<Index, EventVariant>;
        if (recHeader.payloadLength != sizeof(TargetEvent)) {
            return false;
        }

        if (recHeader.typeSignature != 0 && recHeader.typeSignature != computeTypeSignature<TargetEvent>()) {
            return false;
        }

        TargetEvent evt{};
        std::memcpy(&evt, payload, sizeof(TargetEvent));
        auto prio = static_cast<EventPriority>(recHeader.priority);
        sink.post(EventVariant{std::move(evt)}, prio);
        return true;
    }

    std::span<const uint8_t> m_data;
    JournalHeader m_header{};
    size_t m_cursor{0};
    bool m_valid{false};
};

} // namespace corium::wire

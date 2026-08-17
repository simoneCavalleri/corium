#pragma once

#include <cstring>
#include <type_traits>
#include <utility>
#include <variant>

#include "corium/internal/VariantIndex.hpp"
#include "corium/policies/QueuePolicies.hpp"
#include "corium/wire/WirePacket.hpp"

namespace corium::wire {

/// @ingroup wire
/// @brief Zero-heap type-safe serializer and deserializer for Corium event variants over binary wire streams.
class WireSerializer {
public:
    /// @brief Serialize a trivially-copyable concrete event into a fixed-size binary WirePacket.
    /// @tparam Event Concrete event type to serialize.
    /// @tparam EventVariant Target variant list to determine type index.
    /// @tparam MaxPayload Payload size limit.
    /// @param event Event instance to serialize.
    /// @return Serialized and CRC-validated WirePacket.
    template <
        typename Event,
        typename EventVariant,
        size_t MaxPayload = 64
    >
    [[nodiscard]] static WirePacket<MaxPayload> serialize(const Event& event) noexcept {
        static_assert(std::is_trivially_copyable_v<Event>, "Wire events must be trivially copyable for zero-copy binary transport.");
        static_assert(sizeof(Event) <= MaxPayload, "Event size exceeds WirePacket MaxPayload capacity.");

        constexpr size_t typeIdx = corium::variant_index_v<Event, EventVariant>;
        static_assert(typeIdx != static_cast<size_t>(-1), "Event type is not part of the specified EventVariant list.");

        WirePacket<MaxPayload> packet;
        std::memcpy(packet.payload.data(), &event, sizeof(Event));
        packet.finalize(static_cast<uint8_t>(typeIdx), static_cast<uint16_t>(sizeof(Event)));
        return packet;
    }

    /// @brief Deserialize a validated binary WirePacket directly into an event sink.
    /// @tparam EventVariant Variant list of all supported events.
    /// @tparam MaxPayload Payload size limit.
    /// @tparam Sink Target EventSink or EventBus.
    /// @param packet Incoming packet to validate and deserialize.
    /// @param sink Event sink to receive the deserialized event.
    /// @param priority Priority to assign to the deserialized event.
    /// @return true if packet was valid and successfully dispatched into sink; false on validation/type error.
    template <
        typename EventVariant,
        size_t MaxPayload = 64,
        typename Sink
    >
    static bool deserializeAndPush(
        const WirePacket<MaxPayload>& packet,
        Sink& sink,
        EventPriority priority = EventPriority::Normal
    ) noexcept {
        if (!packet.isValid()) {
            return false;
        }

        constexpr size_t numTypes = std::variant_size_v<EventVariant>;
        if (packet.header.typeIndex >= numTypes) {
            return false;
        }

        return deserializeIndex<EventVariant, MaxPayload, Sink>(
            packet,
            sink,
            priority,
            std::make_index_sequence<numTypes>{}
        );
    }

private:
    template <typename EventVariant, size_t MaxPayload, typename Sink, size_t... Is>
    static bool deserializeIndex(
        const WirePacket<MaxPayload>& packet,
        Sink& sink,
        EventPriority priority,
        std::index_sequence<Is...>
    ) noexcept {
        bool handled = false;
        (void)((packet.header.typeIndex == Is ? (handled = deserializeExact<Is, EventVariant, MaxPayload, Sink>(packet, sink, priority), true) : false) || ...);
        return handled;
    }

    template <size_t Index, typename EventVariant, size_t MaxPayload, typename Sink>
    static bool deserializeExact(
        const WirePacket<MaxPayload>& packet,
        Sink& sink,
        EventPriority priority
    ) noexcept {
        using TargetEvent = std::variant_alternative_t<Index, EventVariant>;
        if (packet.header.payloadLength != sizeof(TargetEvent)) {
            return false;
        }

        TargetEvent evt{};
        std::memcpy(&evt, packet.payload.data(), sizeof(TargetEvent));
        sink.post(EventVariant{std::move(evt)}, priority);
        return true;
    }
};

} // namespace corium::wire

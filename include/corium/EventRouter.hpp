/**
 * @file EventRouter.hpp
 * @ingroup core
 * @brief Zero-heap topic-based multi-subscriber event routing and fan-out dispatcher.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "corium/internal/FastDelegate.hpp"

namespace corium {

/// @ingroup core
/// @brief Zero-heap static topic-based publish/subscribe router.
/// Fans out events to multiple registered delegate subscribers per topic ID without dynamic memory allocation.
/// @tparam EventVariant Variant containing all supported event types.
/// @tparam MaxSubscribersPerTopic Maximum subscribers registered per topic (default: 8).
/// @tparam MaxTopics Maximum distinct topics supported (default: 8).
template <
    typename EventVariant,
    size_t MaxSubscribersPerTopic = 8,
    size_t MaxTopics = 8
>
class EventRouter {
public:
    using DelegateType = internal::EventHandlerDelegate<EventVariant, 32>;

    constexpr EventRouter() noexcept = default;

    /// @brief Subscribe a delegate callback to a specific topic ID.
    /// @param topicId Topic identifier.
    /// @param subscriber Delegate callback to execute when an event is published to this topic.
    /// @return true if subscriber was registered; false if topic or subscriber slots are full.
    bool subscribe(uint32_t topicId, DelegateType subscriber) noexcept {
        // Find existing topic slot
        for (size_t i = 0; i < m_numTopics; ++i) {
            if (m_topics[i].topicId == topicId) {
                if (m_topics[i].subscriberCount >= MaxSubscribersPerTopic) {
                    return false; // Topic subscriber capacity reached
                }
                m_topics[i].subscribers[m_topics[i].subscriberCount++] = std::move(subscriber);
                return true;
            }
        }

        // Allocate new topic slot
        if (m_numTopics >= MaxTopics) {
            return false; // Max topics reached
        }

        auto& newTopic = m_topics[m_numTopics++];
        newTopic.topicId = topicId;
        newTopic.subscriberCount = 1;
        newTopic.subscribers[0] = std::move(subscriber);
        return true;
    }

    /// @brief Subscribe a typed event handler lambda to a specific topic ID.
    /// @tparam Event Concrete event type to filter on.
    /// @tparam Callable Lambda/Functor accepting `const Event&`.
    /// @param topicId Topic identifier.
    /// @param callable Handler function.
    /// @return true if subscribed successfully.
    template <typename Event, typename Callable>
    bool subscribeEvent(uint32_t topicId, Callable callable) noexcept {
        return subscribe(topicId, DelegateType([c = std::move(callable)](const EventVariant& var) {
            if (std::holds_alternative<Event>(var)) {
                c(std::get<Event>(var));
            }
        }));
    }

    /// @brief Publish an event variant to all subscribers of a specific topic.
    /// @param topicId Target topic ID.
    /// @param event Event instance to dispatch.
    /// @return Number of subscribers invoked.
    size_t publish(uint32_t topicId, const EventVariant& event) const noexcept {
        for (size_t i = 0; i < m_numTopics; ++i) {
            if (m_topics[i].topicId == topicId) {
                for (size_t j = 0; j < m_topics[i].subscriberCount; ++j) {
                    m_topics[i].subscribers[j](event);
                }
                return m_topics[i].subscriberCount;
            }
        }
        return 0;
    }

    /// @brief Publish a concrete event to all subscribers of a specific topic.
    /// @tparam Event Concrete event type.
    /// @param topicId Target topic ID.
    /// @param event Event instance to dispatch.
    /// @return Number of subscribers invoked.
    template <typename Event>
    size_t publishEvent(uint32_t topicId, const Event& event) const noexcept {
        return publish(topicId, EventVariant{event});
    }

    /// @brief Reset all topic subscriptions.
    void clear() noexcept {
        m_numTopics = 0;
        for (auto& topic : m_topics) {
            topic.subscriberCount = 0;
        }
    }

    /// @brief Total active topics currently configured.
    [[nodiscard]] size_t topicCount() const noexcept {
        return m_numTopics;
    }

private:
    struct TopicSlot {
        uint32_t topicId{0};
        size_t subscriberCount{0};
        std::array<DelegateType, MaxSubscribersPerTopic> subscribers{};
    };

    std::array<TopicSlot, MaxTopics> m_topics{};
    size_t m_numTopics{0};
};

} // namespace corium

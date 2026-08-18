#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <variant>
#include <vector>

#include "corium/Events.hpp"
#include "corium/wire/Serializer.hpp"
#include "corium/wire/WirePacket.hpp"

namespace {

struct FuzzPayloadA {
    uint32_t id;
    float value;
    uint8_t flags;
};

struct FuzzPayloadB {
    uint64_t timestamp;
    double coordinates[3];
};

using FuzzEvents = std::variant<
    corium::QuitEvent,
    FuzzPayloadA,
    FuzzPayloadB
>;

// Mock event sink
struct MockFuzzSink {
    std::size_t receivedCount{0};

    void post(const FuzzEvents&, corium::EventPriority = corium::EventPriority::Normal) noexcept {
        ++receivedCount;
    }
};

void runFuzzIteration(const uint8_t* data, size_t size, MockFuzzSink& sink) {
    if (size < sizeof(corium::wire::WireHeader)) {
        return;
    }

    corium::wire::WirePacket<128> packet;
    const size_t copySize = std::min(size, sizeof(packet));
    std::memcpy(&packet, data, copySize);

    // 1. Verify isValid does not crash or read out-of-bounds
    bool valid = packet.isValid();
    (void)valid;

    // 2. Verify deserializeAndPush handles arbitrary corrupt / malformed data gracefully
    bool pushed = corium::wire::WireSerializer::deserializeAndPush<FuzzEvents, 128>(packet, sink);
    (void)pushed;
}

} // namespace

TEST(WireFuzzTest, RandomizedFuzzBufferCorpus) {
    MockFuzzSink sink;
    std::mt19937_64 rng(0xCAFEF00DULL); // NOLINT(cert-msc51-cpp,cert-msc32-c)
    std::uniform_int_distribution<uint16_t> byteDist(0, 255);
    std::uniform_int_distribution<size_t> lenDist(0, 256);

    // Run 10,000 randomized fuzzing iterations
    for (int iter = 0; iter < 10'000; ++iter) {
        const size_t len = lenDist(rng);
        std::vector<uint8_t> buffer(len);
        for (size_t i = 0; i < len; ++i) {
            buffer[i] = static_cast<uint8_t>(byteDist(rng));
        }

        runFuzzIteration(buffer.data(), buffer.size(), sink);
    }

    // Run structured mutations on a valid packet
    FuzzPayloadA validEvent{.id = 42, .value = 100.0f, .flags = 0x07};
    auto validPacket = corium::wire::WireSerializer::serialize<FuzzPayloadA, FuzzEvents, 128>(validEvent);

    EXPECT_TRUE(validPacket.isValid());
    EXPECT_TRUE((corium::wire::WireSerializer::deserializeAndPush<FuzzEvents, 128>(validPacket, sink)));

    // Bit-flip fuzzing
    auto* rawBytes = reinterpret_cast<uint8_t*>(&validPacket);
    for (size_t byteIdx = 0; byteIdx < sizeof(validPacket); ++byteIdx) {
        for (int bit = 0; bit < 8; ++bit) {
            rawBytes[byteIdx] ^= (1u << bit);
            runFuzzIteration(rawBytes, sizeof(validPacket), sink);
            rawBytes[byteIdx] ^= (1u << bit); // restore
        }
    }
}

// ──────────────────────────────────────────────────────────────
// test_codec.cpp — CRC-32C, serialize/deserialize, click→clack
// codec tests — header packing + CRC32C roundtrip
//           should_fail_decode_when_crc_mismatch
// ──────────────────────────────────────────────────────────────

#include <click_clack/core/codec.hpp>
#include <gtest/gtest.h>
#include <chrono>

namespace cc::test {

TEST(Codec, CRC32CDeterministic) {
    std::string buf = "Hello, click-clack!";
    auto a = crc32c(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
    auto b = crc32c(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
    EXPECT_EQ(a, b);
    EXPECT_NE(a, 0u);
}

TEST(Codec, SerializeRoundTrip) {
    Click click{};
    click.verb     = "Announce";
    click.agent_id = "agent-007";
    click.task_id  = "task-42";
    click.subject  = "Hello world";
    click.flags    = 0x03;
    click.parent   = 0;
    click.payload  = R"({"msg":"hi"})";

    auto clack = click_to_clack(click, 1, 0);
    auto bytes = serialize_clack(clack);
    EXPECT_FALSE(bytes.empty());

    auto decoded = deserialize_clack(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.epoch, clack.header.epoch);
    EXPECT_EQ(decoded->header.verb, clack.header.verb);
    EXPECT_EQ(decoded->agent_id, clack.agent_id);
    EXPECT_EQ(decoded->task_id, clack.task_id);
    EXPECT_EQ(decoded->subject, clack.subject);
    EXPECT_EQ(decoded->payload, clack.payload);
}

TEST(Codec, CorruptedCRCDetected) {
    Click click{};
    click.verb     = "Claim";
    click.agent_id = "a1";
    click.task_id  = "";
    click.payload  = "{}";

    auto clack = click_to_clack(click, 2, 0);
    auto bytes = serialize_clack(clack);
    ASSERT_GT(bytes.size(), 4u);

    // Flip a byte in the CRC region
    bytes[0] ^= 0xFF;
    auto decoded = deserialize_clack(bytes);
    EXPECT_FALSE(decoded.has_value());
}

TEST(Codec, EpochKeyBigEndian) {
    auto k = epoch_key(0x0102030400000000ULL);
    EXPECT_EQ(k[0], 0x01);
    EXPECT_EQ(k[1], 0x02);
    EXPECT_EQ(k[2], 0x03);
    EXPECT_EQ(k[3], 0x04);
}

TEST(Codec, SerializePerformanceUnder50ms) {
    Click click{};
    click.verb     = "Progress";
    click.agent_id = "benchmark";
    click.task_id  = "perf-1";
    click.payload  = R"({"step":1,"total":100})";

    auto clack = click_to_clack(click, 1, 0);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10'000; ++i) {
        auto bytes = serialize_clack(clack);
        auto dc    = deserialize_clack(bytes);
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    EXPECT_LE(elapsed.count(), 50);
}

} // namespace cc::test

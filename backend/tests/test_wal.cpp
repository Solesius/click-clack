// ──────────────────────────────────────────────────────────────
// test_wal.cpp — Write-ahead log append + read
// wal tests — append / read-back / durability
//           should_reject_invalid_verb_when_unknown_verb_string
// ──────────────────────────────────────────────────────────────

#include <click_clack/core/wal.hpp>
#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace cc::test {

class WalTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/cc_test_wal_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
        config_.wal_path = test_dir_;
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
    cc::HubConfig config_;
};

TEST_F(WalTest, AppendAndReadBack) {
    cc::Wal wal;
    auto open_res = wal.open(config_);
    ASSERT_TRUE(open_res.has_value());

    cc::Click click{};
    click.verb     = "Announce";
    click.agent_id = "test-agent";
    click.task_id  = "task-1";
    click.subject  = "test subject";
    click.payload  = R"({"key":"value"})";

    auto res = wal.append(click);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->header.epoch, 1u);
    EXPECT_EQ(res->header.verb, cc::Verb::Announce);

    auto read = wal.read_one(1);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->agent_id, "test-agent");
    EXPECT_EQ(read->payload, click.payload);
}

TEST_F(WalTest, RangeRead) {
    cc::Wal wal;
    wal.open(config_);

    for (int i = 0; i < 10; ++i) {
        cc::Click c{};
        c.verb     = "Progress";
        c.agent_id = "a1";
        c.task_id  = "t-" + std::to_string(i);
        c.payload  = "{}";
        wal.append(c);
    }

    auto range = wal.read_range(3, 7);
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->size(), 5u); // epochs 3,4,5,6,7
}

TEST_F(WalTest, ListenerNotified) {
    cc::Wal wal;
    wal.open(config_);

    int notified = 0;
    wal.on_clack([&](const cc::Clack&) { ++notified; });

    cc::Click c{};
    c.verb = "Heartbeat";
    c.agent_id = "a1";
    c.payload = "{}";
    wal.append(c);
    wal.append(c);

    EXPECT_EQ(notified, 2);
}

TEST_F(WalTest, RecoverRestoresEpoch) {
    {
        cc::Wal wal;
        wal.open(config_);
        cc::Click c{};
        c.verb = "Announce";
        c.agent_id = "a1";
        c.payload = "{}";
        for (int i = 0; i < 5; ++i) wal.append(c);
    }

    cc::Wal wal2;
    wal2.open(config_);
    auto rec = wal2.recover();
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(wal2.current_epoch(), 5u);

    // Next append should be epoch 6
    cc::Click c{};
    c.verb = "Claim";
    c.agent_id = "a2";
    c.payload = "{}";
    auto res = wal2.append(c);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->header.epoch, 6u);
}

} // namespace cc::test

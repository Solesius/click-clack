// ──────────────────────────────────────────────────────────────
// test_mcp.cpp — MCP tool dispatch
// mcp server tests — tool discovery + dispatch
//           should_dispatch_post_clack_when_valid_tool_call
// ──────────────────────────────────────────────────────────────

#include <click_clack/core/wal.hpp>
#include <click_clack/views/materializer.hpp>
#include <click_clack/mcp/mcp_server.hpp>
#include <gtest/gtest.h>
#include <filesystem>

namespace cc::test {

class McpTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_ = "/tmp/cc_test_mcp_" + std::to_string(::getpid());
        std::filesystem::create_directories(base_ + "/wal");
        std::filesystem::create_directories(base_ + "/views");

        config_.wal_path   = base_ + "/wal";
        config_.views_path = base_ + "/views";

        wal_.open(config_);
        mat_.open(config_);
        wal_.on_clack([this](const cc::Clack& c) { mat_.on_clack(c); });

        mcp_ = std::make_unique<cc::McpServer>(wal_, mat_);
        mcp_->register_tools();
    }

    void TearDown() override {
        std::filesystem::remove_all(base_);
    }

    std::string                    base_;
    cc::HubConfig                  config_;
    cc::Wal                        wal_;
    cc::Materializer               mat_;
    std::unique_ptr<cc::McpServer> mcp_;
};

TEST_F(McpTest, ListTools) {
    auto tools = mcp_->list_tools();
    EXPECT_TRUE(tools.is_array());
    EXPECT_GE(tools.size(), 12u);

    bool found_post = false;
    for (const auto& t : tools) {
        if (t.value("name", "") == "cc.post_clack") found_post = true;
    }
    EXPECT_TRUE(found_post);
}

TEST_F(McpTest, DispatchPostClack) {
    nlohmann::json args = {
        {"verb", "Announce"},
        {"task_id", "t-mcp-1"},
        {"subject", "MCP test"},
        {"payload", nlohmann::json{{"foo", "bar"}}},
    };
    auto result = mcp_->dispatch("cc.post_clack", args, "mcp-agent");
    EXPECT_TRUE(result.contains("epoch"));
    EXPECT_EQ(result.value("epoch", 0), 1);
}

TEST_F(McpTest, DISABLED_DispatchQueryTimeline) {
    // Insert a few clacks first
    for (int i = 0; i < 3; ++i) {
        nlohmann::json a = {{"verb", "Progress"}, {"task_id", "t1"}, {"payload", nlohmann::json{}}};
        mcp_->dispatch("cc.post_clack", a, "a1");
    }

    auto result = mcp_->dispatch("cc.query_timeline", {{"from", 1}, {"to", 3}}, "reader");
    EXPECT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 3u);
}

TEST_F(McpTest, DispatchUnknownTool) {
    auto result = mcp_->dispatch("cc.nonexistent", {}, "a1");
    EXPECT_TRUE(result.contains("error"));
}

TEST_F(McpTest, DISABLED_ClaimTask) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "Announce"}, {"task_id", "t-claim"}, {"subject", "test"}, {"payload", {}}},
        "a1");

    auto result = mcp_->dispatch("cc.claim_task", {{"task_id", "t-claim"}}, "a2");
    EXPECT_TRUE(result.contains("epoch"));

    auto task = mcp_->dispatch("cc.query_task", {{"task_id", "t-claim"}}, "a2");
    EXPECT_EQ(task.value("status", ""), "Claimed");
    EXPECT_EQ(task.value("assignee", ""), "a2");
}

TEST_F(McpTest, PerformanceDispatchUnder50ms) {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        nlohmann::json a = {{"verb", "Heartbeat"}, {"payload", nlohmann::json{{"i", i}}}};
        mcp_->dispatch("cc.post_clack", a, "bench");
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    // 1000 dispatches should complete well under 5 seconds
    EXPECT_LE(elapsed.count(), 5000);
}

} // namespace cc::test

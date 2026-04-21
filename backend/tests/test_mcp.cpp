// ──────────────────────────────────────────────────────────────
// test_mcp.cpp — MCP tool dispatch
// mcp server tests — tool discovery + dispatch
//           should_dispatch_post_clack_when_valid_tool_call
// ──────────────────────────────────────────────────────────────

#include <click_clack/core/wal.hpp>
#include <click_clack/views/materializer.hpp>
#include <click_clack/mcp/mcp_server.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <unordered_set>

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
        {"verb", "ANNOUNCE"},
        {"task_id", "t-mcp-1"},
        {"subject", "MCP test"},
        {"payload", nlohmann::json{{"foo", "bar"}}},
    };
    auto result = mcp_->dispatch("cc.post_clack", args, "mcp-agent");
    EXPECT_TRUE(result.contains("epoch"));
    EXPECT_EQ(result.value("epoch", 0), 1);
}

TEST_F(McpTest, DispatchQueryTimeline) {
    // Insert a few clacks first
    for (int i = 0; i < 3; ++i) {
        nlohmann::json a = {
            {"verb", "PROGRESS"},
            {"task_id", "t1"},
            {"payload", nlohmann::json{{"pct", i * 30}}},
        };
        mcp_->dispatch("cc.post_clack", a, "a1");
    }

    auto result = mcp_->dispatch("cc.query_timeline",
        nlohmann::json{{"since_epoch", 0}, {"limit", 10}}, "reader");
    ASSERT_TRUE(result.is_array()) << result.dump();
    EXPECT_EQ(result.size(), 3u);
    for (const auto& c : result) {
        EXPECT_EQ(c.value("verb", ""), "PROGRESS");
        EXPECT_EQ(c.value("task_id", ""), "t1");
    }
}

TEST_F(McpTest, DispatchUnknownTool) {
    auto result = mcp_->dispatch("cc.nonexistent", {}, "a1");
    EXPECT_TRUE(result.contains("error"));
}

TEST_F(McpTest, ClaimTask) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "t-claim"}, {"subject", "test"}, {"payload", nlohmann::json::object()}},
        "a1");

    auto result = mcp_->dispatch("cc.claim_task", {{"task_id", "t-claim"}}, "a2");
    EXPECT_TRUE(result.contains("epoch")) << result.dump();

    auto task = mcp_->dispatch("cc.query_task", {{"task_id", "t-claim"}}, "a2");
    EXPECT_EQ(task.value("status", ""), "claimed");
    EXPECT_EQ(task.value("owner_agent", ""), "a2");
}

TEST_F(McpTest, ClaimWarnsWhenAnotherAgentHolds) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "t-busy"}, {"payload", nlohmann::json::object()}}, "a1");
    auto first = mcp_->dispatch("cc.claim_task", {{"task_id", "t-busy"}}, "a1");
    ASSERT_TRUE(first.contains("epoch")) << first.dump();
    EXPECT_FALSE(first.contains("warning"));

    // Different agent claims same task — still succeeds, but advisory warning.
    auto second = mcp_->dispatch("cc.claim_task", {{"task_id", "t-busy"}}, "a2");
    ASSERT_TRUE(second.contains("epoch")) << second.dump();
    ASSERT_TRUE(second.contains("warning"));
    EXPECT_EQ(second["warning"].value("code", ""), "already_claimed");
    EXPECT_EQ(second["warning"].value("by", ""), "a1");
}

TEST_F(McpTest, PerformanceDispatchUnder50ms) {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        nlohmann::json a = {{"verb", "HEARTBEAT"}, {"payload", nlohmann::json{{"i", i}}}};
        mcp_->dispatch("cc.post_clack", a, "bench");
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    // 1000 dispatches should complete well under 5 seconds
    EXPECT_LE(elapsed.count(), 5000);
}

// ── Extra coverage ──────────────────────────────────────────────

TEST_F(McpTest, UnknownToolReturnsError) {
    auto res = mcp_->dispatch("cc.bogus_tool", nlohmann::json::object(), "x");
    EXPECT_EQ(res.value("error", ""), "unknown_tool");
    EXPECT_EQ(res.value("tool", ""), "cc.bogus_tool");
}

TEST_F(McpTest, Whoami) {
    auto r1 = mcp_->dispatch("cc.whoami", nlohmann::json::object(), "caller-a");
    EXPECT_EQ(r1.value("agent_id", ""), "caller-a");
    EXPECT_EQ(r1.value("overridden", true), false);

    auto r2 = mcp_->dispatch("cc.whoami", {{"as_agent", "override-b"}}, "caller-a");
    EXPECT_EQ(r2.value("agent_id", ""), "override-b");
    EXPECT_EQ(r2.value("transport_caller", ""), "caller-a");
    EXPECT_EQ(r2.value("overridden", false), true);
}

TEST_F(McpTest, TaskIdAutoGenerated) {
    auto r = mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"subject", "auto"}, {"payload", nlohmann::json::object()}},
        "a1");
    ASSERT_TRUE(r.contains("task_id")) << r.dump();
    EXPECT_TRUE(r.value("task_id_generated", false));
    EXPECT_EQ(r.value("task_id", "").substr(0, 2), "t-");
}

TEST_F(McpTest, HeartbeatAndPresence) {
    mcp_->dispatch("cc.heartbeat", {{"load", 0.5}}, "hb-agent");
    auto p = mcp_->dispatch("cc.query_presence", nlohmann::json::object(), "");
    ASSERT_TRUE(p.is_array());
    bool found = false;
    for (const auto& a : p) {
        if (a.value("agent_id", "") == "hb-agent") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(McpTest, ReportProgressAndCompleteFlow) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "t-flow"}, {"payload", nlohmann::json::object()}}, "a1");
    mcp_->dispatch("cc.claim_task", {{"task_id", "t-flow"}}, "a1");
    mcp_->dispatch("cc.report_progress",
        {{"task_id", "t-flow"}, {"pct", 42}, {"summary", "quartered"}}, "a1");
    auto mid = mcp_->dispatch("cc.query_task", {{"task_id", "t-flow"}}, "a1");
    EXPECT_EQ(mid.value("pct", 0), 42);
    EXPECT_EQ(mid.value("status", ""), "in_progress");

    mcp_->dispatch("cc.complete_task",
        {{"task_id", "t-flow"}, {"summary", "done"}}, "a1");
    auto done = mcp_->dispatch("cc.query_task", {{"task_id", "t-flow"}}, "a1");
    EXPECT_EQ(done.value("status", ""), "completed");
    EXPECT_EQ(done.value("pct", 0), 100);
    EXPECT_EQ(done.value("summary", ""), "done");
}

TEST_F(McpTest, PostArtifactAndListed) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "t-art"}, {"payload", nlohmann::json::object()}}, "a1");
    auto r = mcp_->dispatch("cc.post_artifact",
        {{"task_id", "t-art"}, {"path", "/tmp/x.txt"}, {"kind", "file"}, {"sha256", "deadbeef"}},
        "a1");
    ASSERT_TRUE(r.contains("epoch")) << r.dump();

    auto snapshot = mcp_->dispatch("cc.get_task", {{"task_id", "t-art"}}, "a1");
    ASSERT_TRUE(snapshot["artifacts"].is_array());
    ASSERT_EQ(snapshot["artifacts"].size(), 1u);
    EXPECT_EQ(snapshot["artifacts"][0].value("path", ""), "/tmp/x.txt");
    EXPECT_EQ(snapshot["artifacts"][0].value("sha256", ""), "deadbeef");
}

TEST_F(McpTest, QueryTasksFiltersByStatus) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "t-a"}, {"payload", nlohmann::json::object()}}, "a1");
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "t-b"}, {"payload", nlohmann::json::object()}}, "a1");
    mcp_->dispatch("cc.claim_task", {{"task_id", "t-b"}}, "a1");

    auto claimed = mcp_->dispatch("cc.query_tasks", {{"status_filter", "claimed"}}, "");
    ASSERT_TRUE(claimed.is_array());
    bool saw_b = false, saw_a = false;
    for (const auto& t : claimed) {
        if (t.value("task_id", "") == "t-b") saw_b = true;
        if (t.value("task_id", "") == "t-a") saw_a = true;
    }
    EXPECT_TRUE(saw_b);
    EXPECT_FALSE(saw_a);
}

TEST_F(McpTest, AckLinksBackToOriginalEpoch) {
    auto ann = mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "t-ack"}, {"payload", nlohmann::json::object()}}, "a1");
    auto epoch = ann.value("epoch", std::uint64_t{0});
    ASSERT_GT(epoch, 0u);

    auto ack = mcp_->dispatch("cc.ack", {{"epoch", epoch}, {"note", "seen"}}, "a2");
    EXPECT_TRUE(ack.contains("epoch"));
    EXPECT_EQ(ack.value("acked", std::uint64_t{0}), epoch);

    auto ack_missing = mcp_->dispatch("cc.ack", nlohmann::json::object(), "a2");
    EXPECT_EQ(ack_missing.value("error", ""), "epoch required");
}

TEST_F(McpTest, AskAnswerThreads) {
    auto ask = mcp_->dispatch("cc.ask",
        {{"task_id", "t-dlg"}, {"subject", "why?"}, {"body", {{"q", "because"}}}}, "asker");
    auto ask_epoch = ask.value("epoch", std::uint64_t{0});
    ASSERT_GT(ask_epoch, 0u);

    auto answer = mcp_->dispatch("cc.answer",
        {{"ask_epoch", ask_epoch}, {"body", {{"a", "yes"}}}}, "answerer");
    EXPECT_TRUE(answer.contains("epoch"));

    auto no_epoch = mcp_->dispatch("cc.answer", nlohmann::json::object(), "answerer");
    EXPECT_EQ(no_epoch.value("error", ""), "ask_epoch required");
}

TEST_F(McpTest, ReservePathConflictAndRelease) {
    auto r1 = mcp_->dispatch("cc.reserve_path",
        {{"path", "/src/foo.cpp"}, {"task_id", "t-res"}, {"ttl_ms", 10000}}, "a1");
    EXPECT_TRUE(r1.contains("epoch"));
    EXPECT_FALSE(r1.contains("conflict"));

    auto r2 = mcp_->dispatch("cc.reserve_path",
        {{"path", "/src/foo.cpp"}, {"task_id", "t-res2"}, {"ttl_ms", 10000}}, "a2");
    ASSERT_TRUE(r2.contains("conflict")) << r2.dump();
    EXPECT_EQ(r2["conflict"].value("by", ""), "a1");

    auto list = mcp_->dispatch("cc.query_reservations",
        {{"path_prefix", "/src/"}}, "");
    ASSERT_TRUE(list.is_array());
    EXPECT_GE(list.size(), 1u);

    auto rel = mcp_->dispatch("cc.release_path",
        {{"path", "/src/foo.cpp"}, {"task_id", "t-res"}}, "a1");
    EXPECT_EQ(rel.value("released", ""), "/src/foo.cpp");

    auto list2 = mcp_->dispatch("cc.query_reservations",
        {{"path_prefix", "/src/"}}, "");
    EXPECT_EQ(list2.size(), 0u);

    auto missing = mcp_->dispatch("cc.reserve_path", nlohmann::json::object(), "a1");
    EXPECT_EQ(missing.value("error", ""), "path required");
    auto missing2 = mcp_->dispatch("cc.release_path", nlohmann::json::object(), "a1");
    EXPECT_EQ(missing2.value("error", ""), "path required");
}

TEST_F(McpTest, WaitShortCircuitsWhenMatchAlreadyPresent) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "PROGRESS"}, {"task_id", "t-wait"},
         {"payload", nlohmann::json{{"pct", 10}}}}, "worker");

    auto result = mcp_->dispatch("cc.wait",
        {{"task_id", "t-wait"}, {"timeout_ms", 100}}, "observer");
    EXPECT_FALSE(result.value("timed_out", true));
    ASSERT_TRUE(result["clacks"].is_array());
    EXPECT_GE(result["clacks"].size(), 1u);
}

TEST_F(McpTest, WaitTimesOutWhenNothingMatches) {
    auto result = mcp_->dispatch("cc.wait",
        {{"task_id", "t-never"}, {"timeout_ms", 50}}, "observer");
    EXPECT_TRUE(result.value("timed_out", false));
    EXPECT_EQ(result["clacks"].size(), 0u);
}

TEST_F(McpTest, QueryAgentLogFiltersByAgent) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "tl-1"}, {"payload", nlohmann::json::object()}}, "alpha");
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "tl-2"}, {"payload", nlohmann::json::object()}}, "beta");

    auto alpha_log = mcp_->dispatch("cc.query_agent_log",
        {{"agent_id", "alpha"}, {"limit", 100}}, "");
    ASSERT_TRUE(alpha_log.is_array());
    for (const auto& c : alpha_log) {
        EXPECT_EQ(c.value("agent_id", ""), "alpha");
    }
}

TEST_F(McpTest, QueryTimelineWithVerbFilter) {
    mcp_->dispatch("cc.post_clack",
        {{"verb", "ANNOUNCE"}, {"task_id", "q-1"}, {"payload", nlohmann::json::object()}}, "x");
    mcp_->dispatch("cc.post_clack",
        {{"verb", "PROGRESS"}, {"task_id", "q-1"},
         {"payload", nlohmann::json{{"pct", 25}}}}, "x");

    auto only_progress = mcp_->dispatch("cc.query_timeline",
        {{"since_epoch", 0}, {"limit", 100}, {"verbs", nlohmann::json::array({"PROGRESS"})}}, "");
    ASSERT_TRUE(only_progress.is_array());
    for (const auto& c : only_progress) {
        EXPECT_EQ(c.value("verb", ""), "PROGRESS");
    }
}

TEST_F(McpTest, QueryTaskNotFound) {
    auto r = mcp_->dispatch("cc.query_task", {{"task_id", "nope"}}, "");
    EXPECT_EQ(r.value("error", ""), "not_found");
}

TEST_F(McpTest, ListToolsIncludesAllCategories) {
    auto tools = mcp_->list_tools();
    ASSERT_TRUE(tools.is_array());
    std::unordered_set<std::string> names;
    for (const auto& t : tools) names.insert(t.value("name", ""));

    for (const char* expected : {
        "cc.post_clack", "cc.query_timeline", "cc.query_agent_log",
        "cc.query_task", "cc.query_tasks", "cc.query_presence",
        "cc.query_hitl_queue", "cc.claim_task", "cc.report_progress",
        "cc.complete_task", "cc.post_artifact", "cc.heartbeat",
        "cc.ack", "cc.ask", "cc.answer", "cc.reserve_path",
        "cc.release_path", "cc.query_reservations", "cc.get_task",
        "cc.wait", "cc.whoami"
    }) {
        EXPECT_TRUE(names.contains(expected)) << "missing tool: " << expected;
    }
}

} // namespace cc::test

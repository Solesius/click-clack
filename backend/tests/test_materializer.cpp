// ──────────────────────────────────────────────────────────────
// test_materializer.cpp — CQRS view projections
// materializer tests — view projection from clack stream
//           should_track_agent_presence_when_heartbeat_received
// ──────────────────────────────────────────────────────────────

#include <click_clack/core/wal.hpp>
#include <click_clack/views/materializer.hpp>
#include <gtest/gtest.h>
#include <filesystem>

namespace cc::test {

class MaterializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_ = "/tmp/cc_test_mat_" + std::to_string(::getpid());
        std::filesystem::create_directories(base_ + "/wal");
        std::filesystem::create_directories(base_ + "/views");

        config_.wal_path   = base_ + "/wal";
        config_.views_path = base_ + "/views";

        wal_.open(config_);
        mat_.open(config_);
        wal_.on_clack([this](const cc::Clack& c) { mat_.on_clack(c); });
    }

    void TearDown() override {
        std::filesystem::remove_all(base_);
    }

    cc::Click make_click(const char* verb, const char* agent, const char* task = "",
                         const char* payload = "{}") {
        cc::Click c{};
        c.verb     = verb;
        c.agent_id = agent;
        c.task_id  = task;
        c.payload  = payload;
        return c;
    }

    std::string      base_;
    cc::HubConfig    config_;
    cc::Wal          wal_;
    cc::Materializer mat_;
};

TEST_F(MaterializerTest, TaskAnnounceCreatesUnclaimed) {
    wal_.append(make_click("Announce", "a1", "task-1", R"({"name":"Build thing"})"));

    auto task = mat_.query_task("task-1");
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->task_id, "task-1");
    EXPECT_EQ(task->status, cc::TaskStatus::Unclaimed);
}

TEST_F(MaterializerTest, ClaimUpdatesStatus) {
    wal_.append(make_click("Announce", "a1", "t2"));
    wal_.append(make_click("Claim", "a2", "t2"));

    auto task = mat_.query_task("t2");
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, cc::TaskStatus::Claimed);
    EXPECT_EQ(task->assignee, "a2");
}

TEST_F(MaterializerTest, CompleteMarksCompleted) {
    wal_.append(make_click("Announce", "a1", "t3"));
    wal_.append(make_click("Claim", "a1", "t3"));
    wal_.append(make_click("Progress", "a1", "t3"));
    wal_.append(make_click("Complete", "a1", "t3"));

    auto task = mat_.query_task("t3");
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, cc::TaskStatus::Completed);
}

TEST_F(MaterializerTest, HeartbeatTracksPresence) {
    wal_.append(make_click("Heartbeat", "agent-x", "", R"({"uptime":120})"));

    auto agents = mat_.query_presence();
    ASSERT_TRUE(agents.has_value());
    EXPECT_GE(agents->size(), 1u);

    bool found = false;
    for (const auto& a : *agents) {
        if (a.agent_id == "agent-x") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(MaterializerTest, ArtifactTracked) {
    wal_.append(make_click("Artifact", "a1", "t1", R"({"path":"/out/result.json","mime":"application/json"})"));

    auto arts = mat_.query_artifacts("t1");
    ASSERT_TRUE(arts.has_value());
    EXPECT_GE(arts->size(), 1u);
}

TEST_F(MaterializerTest, HITLFlagQueued) {
    cc::Click c = make_click("Approve", "a1", "t5");
    c.flags = 0x04; // hitl_req bit
    wal_.append(c);

    auto queue = mat_.query_hitl_queue();
    ASSERT_TRUE(queue.has_value());
    EXPECT_GE(queue->size(), 1u);
}

TEST_F(MaterializerTest, AgentLogFiltered) {
    wal_.append(make_click("Announce", "bob", "t1"));
    wal_.append(make_click("Claim", "alice", "t1"));
    wal_.append(make_click("Progress", "bob", "t2"));

    auto bob_log = mat_.query_agent_log("bob");
    ASSERT_TRUE(bob_log.has_value());
    for (const auto& c : *bob_log) {
        EXPECT_EQ(c.agent_id, "bob");
    }
}

} // namespace cc::test

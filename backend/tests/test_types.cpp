// ──────────────────────────────────────────────────────────────
// test_types.cpp — Verb/Flags/ClackHeader compile-time correctness
// type tests — clack header layout + size guarantees
// ──────────────────────────────────────────────────────────────

#include <click_clack/core/types.hpp>
#include <gtest/gtest.h>

namespace cc::test {

TEST(Types, VerbRoundTrip) {
    for (auto v : {Verb::Announce, Verb::Claim, Verb::Yield, Verb::Progress,
                   Verb::Artifact, Verb::Complete, Verb::Error, Verb::Query,
                   Verb::Respond, Verb::Observe, Verb::Direct, Verb::Approve,
                   Verb::Reject, Verb::Halt, Verb::Resume, Verb::Heartbeat}) {
        EXPECT_EQ(str_to_verb(verb_to_str(v)), v);
    }
}

TEST(Types, FlagsBitmask) {
    Flags f{};
    EXPECT_FALSE(f.urgent());
    EXPECT_FALSE(f.blocking());

    f.set_urgent(true);
    f.set_hitl_req(true);
    EXPECT_TRUE(f.urgent());
    EXPECT_TRUE(f.hitl_req());
    EXPECT_FALSE(f.blocking());
    EXPECT_FALSE(f.ephemeral());
    EXPECT_EQ(f.raw, 0x05);

    f.set_urgent(false);
    EXPECT_FALSE(f.urgent());
    EXPECT_TRUE(f.hitl_req());
}

TEST(Types, ClackHeaderSize) {
    static_assert(sizeof(ClackHeader) == 64);
    EXPECT_EQ(sizeof(ClackHeader), 64u);
}

TEST(Types, TaskStatusRoundTrip) {
    for (auto s : {TaskStatus::Unclaimed, TaskStatus::Claimed,
                   TaskStatus::InProgress, TaskStatus::Completed,
                   TaskStatus::Errored, TaskStatus::Halted}) {
        EXPECT_EQ(str_to_task_status(task_status_to_str(s)), s);
    }
}

} // namespace cc::test

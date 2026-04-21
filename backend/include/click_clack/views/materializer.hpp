#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / views / materializer.hpp
// WAL → View projection engine (CQRS materialized views)
// Materializer: projects append-only clacks into live views.
// ──────────────────────────────────────────────────────────────

#include "../core/codec.hpp"
#include "../core/types.hpp"

#include <celer/celer.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace cc {

using json = nlohmann::json;

// ── JSON round-trip helpers ─────────────────────────────────

[[nodiscard]] inline auto clack_to_json(const Clack& c) -> json {
    // Payload is stored as a raw JSON string. If it parses, embed it; otherwise
    // fall back to the raw string so the enclosing frame stays valid JSON.
    json payload = json::object();
    if (!c.payload.empty()) {
        auto parsed = json::parse(c.payload, nullptr, /*allow_exceptions=*/false);
        payload = parsed.is_discarded() ? json(c.payload) : std::move(parsed);
    }
    return json{
        {"epoch",        c.header.epoch},
        {"timestamp_us", c.header.timestamp_us},
        {"verb",         std::string{verb_to_str(c.header.verb)}},
        {"flags", json{
            {"urgent",    c.header.flags.urgent()},
            {"blocking",  c.header.flags.blocking()},
            {"hitl_req",  c.header.flags.hitl_req()},
            {"ephemeral", c.header.flags.ephemeral()},
        }},
        {"agent_id",     c.agent_id},
        {"task_id",      c.task_id},
        {"subject",      c.subject},
        {"payload",      std::move(payload)},
        {"parent_epoch", c.header.parent_epoch},
    };
}

[[nodiscard]] inline auto task_state_to_json(const TaskState& t) -> json {
    json j{
        {"task_id",        t.task_id},
        {"status",         std::string{task_status_to_str(t.status)}},
        {"last_epoch",     t.last_epoch},
        {"last_verb",      t.last_verb},
        {"pct",            t.pct},
        {"summary",        t.summary},
        {"artifact_count", t.artifact_count},
        {"created_epoch",  t.created_epoch},
        {"updated_us",     t.updated_us},
    };
    if (t.owner_agent) j["owner_agent"] = *t.owner_agent;
    return j;
}

[[nodiscard]] inline auto presence_to_json(const AgentPresenceState& a) -> json {
    return json{
        {"agent_id",     a.agent_id},
        {"status",       std::string{agent_status_to_str(a.status)}},
        {"last_epoch",   a.last_epoch},
        {"last_seen_us", a.last_seen_us},
        {"capabilities", a.capabilities},
        {"model",        a.model},
        {"load",         a.load},
    };
}

// ── Materializer ────────────────────────────────────────────

class Materializer {
public:
    Materializer() = default;
    ~Materializer() = default;

    Materializer(const Materializer&) = delete;
    Materializer& operator=(const Materializer&) = delete;

    [[nodiscard]] auto open(const HubConfig& config) -> celer::VoidResult {
        auto factory = celer::backends::rocksdb::factory({.path = config.views_path});
        std::vector<celer::TableDescriptor> schema{
            {"views", "agent_log"},
            {"views", "task_board"},
            {"views", "thread_tree"},
            {"views", "agent_presence"},
            {"views", "artifact_index"},
            {"views", "hitl_queue"},
        };

        auto res = celer::build_tree(factory, schema);
        if (!res) return celer::VoidResult{std::unexpected(res.error())};

        store_ = std::make_unique<celer::Store>(std::move(*res), celer::ResourceStack{});

        auto db = store_->db("views");
        if (!db) return celer::VoidResult{std::unexpected(db.error())};

        auto load = [&](const char* name) { return db->table(name); };

        auto t1 = load("agent_log");      if (!t1) return celer::VoidResult{std::unexpected(t1.error())}; agent_log_.emplace(std::move(*t1));
        auto t2 = load("task_board");      if (!t2) return celer::VoidResult{std::unexpected(t2.error())}; task_board_.emplace(std::move(*t2));
        auto t3 = load("thread_tree");     if (!t3) return celer::VoidResult{std::unexpected(t3.error())}; thread_tree_.emplace(std::move(*t3));
        auto t4 = load("agent_presence");  if (!t4) return celer::VoidResult{std::unexpected(t4.error())}; presence_.emplace(std::move(*t4));
        auto t5 = load("artifact_index");  if (!t5) return celer::VoidResult{std::unexpected(t5.error())}; artifact_idx_.emplace(std::move(*t5));
        auto t6 = load("hitl_queue");      if (!t6) return celer::VoidResult{std::unexpected(t6.error())}; hitl_queue_.emplace(std::move(*t6));

        return {};
    }

    // ── Projection entry point (called per Clack) ───────────

    void on_clack(const Clack& clack) {
        project_agent_log(clack);
        project_task_board(clack);
        project_thread_tree(clack);
        project_presence(clack);
        project_artifact_index(clack);
        project_hitl_queue(clack);
    }

    // ── Query methods (read side of CQRS) ───────────────────

    [[nodiscard]] auto query_agent_log(std::string_view agent_id,
                                       std::uint64_t since_epoch,
                                       int limit)
        -> celer::Result<std::vector<Clack>>
    {
        auto prefix = std::string{agent_id} + "/" + std::string{epoch_key(since_epoch)};
        auto pairs = agent_log_->handle()->prefix_scan(prefix);
        if (!pairs) return celer::Result<std::vector<Clack>>{std::unexpected(pairs.error())};

        std::vector<Clack> out;
        out.reserve(static_cast<std::size_t>(limit));
        for (const auto& kv : *pairs) {
            if (static_cast<int>(out.size()) >= limit) break;
            auto c = deserialize_clack(kv.value);
            if (c) out.push_back(std::move(*c));
        }
        return out;
    }

    [[nodiscard]] auto query_task(std::string_view task_id)
        -> celer::Result<std::optional<TaskState>>
    {
        auto raw = task_board_->get_raw(std::string{task_id});
        if (!raw) return celer::Result<std::optional<TaskState>>{std::unexpected(raw.error())};
        if (!*raw) return std::optional<TaskState>{std::nullopt};

        auto j = json::parse(**raw, nullptr, false);
        if (j.is_discarded()) {
            return celer::Result<std::optional<TaskState>>{
                std::unexpected(celer::Error{"views", "invalid task_board JSON"})};
        }

        TaskState ts{};
        ts.task_id        = j.value("task_id", "");
        ts.status         = str_to_task_status(j.value("status", "unclaimed"));
        if (j.contains("owner_agent")) ts.owner_agent = j["owner_agent"].get<std::string>();
        ts.last_epoch     = j.value("last_epoch", std::uint64_t{0});
        ts.last_verb      = j.value("last_verb", "");
        ts.pct            = j.value("pct", 0);
        ts.summary        = j.value("summary", "");
        ts.artifact_count = j.value("artifact_count", 0);
        ts.created_epoch  = j.value("created_epoch", std::uint64_t{0});
        ts.updated_us     = j.value("updated_us", std::uint64_t{0});
        return std::optional<TaskState>{std::move(ts)};
    }

    [[nodiscard]] auto query_tasks(std::optional<std::string_view> status_filter, int limit)
        -> celer::Result<std::vector<TaskState>>
    {
        auto pairs = task_board_->handle()->prefix_scan("");
        if (!pairs) return celer::Result<std::vector<TaskState>>{std::unexpected(pairs.error())};

        std::vector<TaskState> out;
        for (const auto& kv : *pairs) {
            if (static_cast<int>(out.size()) >= limit) break;
            auto j = json::parse(kv.value, nullptr, false);
            if (j.is_discarded()) continue;

            TaskState ts{};
            ts.task_id = j.value("task_id", "");
            ts.status  = str_to_task_status(j.value("status", "unclaimed"));
            if (j.contains("owner_agent")) ts.owner_agent = j["owner_agent"].get<std::string>();
            ts.last_epoch     = j.value("last_epoch", std::uint64_t{0});
            ts.last_verb      = j.value("last_verb", "");
            ts.pct            = j.value("pct", 0);
            ts.summary        = j.value("summary", "");
            ts.artifact_count = j.value("artifact_count", 0);
            ts.created_epoch  = j.value("created_epoch", std::uint64_t{0});
            ts.updated_us     = j.value("updated_us", std::uint64_t{0});

            if (status_filter && task_status_to_str(ts.status) != *status_filter) continue;
            out.push_back(std::move(ts));
        }
        return out;
    }

    [[nodiscard]] auto query_thread(std::uint64_t root_epoch)
        -> celer::Result<std::vector<ThreadNode>>
    {
        auto prefix = epoch_key(root_epoch) + "/";
        auto pairs = thread_tree_->handle()->prefix_scan(prefix);
        if (!pairs) return celer::Result<std::vector<ThreadNode>>{std::unexpected(pairs.error())};

        std::vector<ThreadNode> out;
        for (const auto& kv : *pairs) {
            auto c = deserialize_clack(kv.value);
            if (!c) continue;
            // Key format: root_epoch/depth/epoch — parse depth from key
            auto key = kv.key;
            auto slash1 = key.find('/');
            auto slash2 = key.find('/', slash1 + 1);
            int depth = 0;
            if (slash1 != std::string::npos && slash2 != std::string::npos) {
                depth = std::stoi(key.substr(slash1 + 1, slash2 - slash1 - 1));
            }
            out.push_back(ThreadNode{root_epoch, depth, std::move(*c)});
        }
        return out;
    }

    [[nodiscard]] auto query_presence()
        -> celer::Result<std::vector<AgentPresenceState>>
    {
        auto pairs = presence_->handle()->prefix_scan("");
        if (!pairs) return celer::Result<std::vector<AgentPresenceState>>{std::unexpected(pairs.error())};

        std::vector<AgentPresenceState> out;
        for (const auto& kv : *pairs) {
            auto j = json::parse(kv.value, nullptr, false);
            if (j.is_discarded()) continue;
            AgentPresenceState ap{};
            ap.agent_id    = j.value("agent_id", "");
            ap.status      = j.value("status", "") == "online" ? AgentStatus::Online : AgentStatus::Offline;
            ap.last_epoch  = j.value("last_epoch", std::uint64_t{0});
            ap.last_seen_us = j.value("last_seen_us", std::uint64_t{0});
            ap.capabilities = j.value("capabilities", std::vector<std::string>{});
            ap.model       = j.value("model", "");
            ap.load        = j.value("load", 0.0f);
            out.push_back(std::move(ap));
        }
        return out;
    }

    [[nodiscard]] auto query_artifacts(std::string_view task_id)
        -> celer::Result<std::vector<ArtifactEntry>>
    {
        auto prefix = std::string{task_id} + "/";
        auto pairs = artifact_idx_->handle()->prefix_scan(prefix);
        if (!pairs) return celer::Result<std::vector<ArtifactEntry>>{std::unexpected(pairs.error())};

        std::vector<ArtifactEntry> out;
        for (const auto& kv : *pairs) {
            auto j = json::parse(kv.value, nullptr, false);
            if (j.is_discarded()) continue;
            ArtifactEntry ae{};
            ae.task_id       = j.value("task_id", "");
            ae.epoch         = j.value("epoch", std::uint64_t{0});
            ae.kind          = j.value("kind", "");
            ae.path          = j.value("path", "");
            if (j.contains("content")) ae.content = j["content"].get<std::string>();
            ae.sha256        = j.value("sha256", "");
            ae.review_status = j.value("review_status", "pending");
            if (j.contains("review_epoch")) ae.review_epoch = j["review_epoch"].get<std::uint64_t>();
            out.push_back(std::move(ae));
        }
        return out;
    }

    [[nodiscard]] auto query_hitl_queue()
        -> celer::Result<std::vector<HITLItem>>
    {
        auto pairs = hitl_queue_->handle()->prefix_scan("");
        if (!pairs) return celer::Result<std::vector<HITLItem>>{std::unexpected(pairs.error())};

        std::vector<HITLItem> out;
        for (const auto& kv : *pairs) {
            auto c = deserialize_clack(kv.value);
            if (!c) continue;
            HITLItem item{};
            item.epoch    = c->header.epoch;
            item.clack    = std::move(*c);
            item.resolved = false;
            out.push_back(std::move(item));
        }
        return out;
    }

private:
    // ── Projection functions (write side) ───────────────────

    void project_agent_log(const Clack& clack) {
        auto key = clack.agent_id + "/" + epoch_key(clack.header.epoch);
        (void)agent_log_->put_raw(key, serialize_clack(clack));
    }

    void project_task_board(const Clack& clack) {
        if (clack.task_id.empty()) return;
        const auto v = clack.header.verb;
        if (v != Verb::Claim && v != Verb::Progress &&
            v != Verb::Complete && v != Verb::Error && v != Verb::Halt) return;

        auto existing_raw = task_board_->get_raw(clack.task_id);
        TaskState ts{};
        if (existing_raw && *existing_raw) {
            auto j = json::parse(**existing_raw, nullptr, false);
            if (!j.is_discarded()) {
                ts.task_id        = j.value("task_id", clack.task_id);
                ts.created_epoch  = j.value("created_epoch", clack.header.epoch);
                ts.artifact_count = j.value("artifact_count", 0);
                if (j.contains("owner_agent")) ts.owner_agent = j["owner_agent"].get<std::string>();
            }
        } else {
            ts.task_id       = clack.task_id;
            ts.created_epoch = clack.header.epoch;
        }

        ts.last_epoch = clack.header.epoch;
        ts.last_verb  = std::string{verb_to_str(v)};
        ts.updated_us = clack.header.timestamp_us;

        switch (v) {
            case Verb::Claim:
                ts.status      = TaskStatus::Claimed;
                ts.owner_agent = clack.agent_id;
                ts.pct         = 0;
                break;
            case Verb::Progress: {
                ts.status = TaskStatus::InProgress;
                auto pj = json::parse(clack.payload, nullptr, false);
                ts.pct     = pj.value("pct", ts.pct);
                ts.summary = pj.value("summary", ts.summary);
                break;
            }
            case Verb::Complete:
                ts.status = TaskStatus::Completed;
                ts.pct    = 100;
                ts.summary = json::parse(clack.payload, nullptr, false).value("summary", "");
                break;
            case Verb::Error:
                ts.status  = TaskStatus::Errored;
                ts.summary = json::parse(clack.payload, nullptr, false).value("message", "");
                break;
            case Verb::Halt:
                ts.status = TaskStatus::Halted;
                break;
            default:
                break;
        }

        (void)task_board_->put_raw(clack.task_id, task_state_to_json(ts).dump());
    }

    void project_thread_tree(const Clack& clack) {
        if (clack.header.parent_epoch == 0) return;

        // Resolve root: walk parent chain (simple 1-level for now)
        auto root = clack.header.parent_epoch;
        int depth = 1;

        // Check if parent itself has a parent (2-level)
        auto parent_key = epoch_key(clack.header.parent_epoch);
        // For simplicity, use the parent_epoch as root and depth=1
        // A production impl would walk the chain

        auto key = epoch_key(root) + "/" + std::to_string(depth) + "/" + epoch_key(clack.header.epoch);
        (void)thread_tree_->put_raw(key, serialize_clack(clack));
    }

    void project_presence(const Clack& clack) {
        const auto v = clack.header.verb;
        if (v != Verb::Announce && v != Verb::Heartbeat) return;

        auto pj = json::parse(clack.payload, nullptr, false);
        AgentPresenceState ap{};
        ap.agent_id    = clack.agent_id;
        ap.status      = AgentStatus::Online;
        ap.last_epoch  = clack.header.epoch;
        ap.last_seen_us = clack.header.timestamp_us;

        if (v == Verb::Announce) {
            ap.capabilities = pj.value("capabilities", std::vector<std::string>{});
            ap.model        = pj.value("model", "");
            ap.load         = 0.0f;
        } else {
            // Heartbeat — merge with existing
            auto existing_raw = presence_->get_raw(clack.agent_id);
            if (existing_raw && *existing_raw) {
                auto ej = json::parse(**existing_raw, nullptr, false);
                ap.capabilities = ej.value("capabilities", std::vector<std::string>{});
                ap.model        = ej.value("model", "");
            }
            ap.load = pj.value("load", 0.0f);
        }

        (void)presence_->put_raw(clack.agent_id, presence_to_json(ap).dump());
    }

    void project_artifact_index(const Clack& clack) {
        const auto v = clack.header.verb;

        if (v == Verb::Artifact) {
            auto pj = json::parse(clack.payload, nullptr, false);
            json entry{
                {"task_id",       clack.task_id},
                {"epoch",         clack.header.epoch},
                {"kind",          pj.value("kind", "")},
                {"path",          pj.value("path", "")},
                {"sha256",        pj.value("sha256", "")},
                {"review_status", "pending"},
            };
            if (pj.contains("content")) entry["content"] = pj["content"];
            auto key = clack.task_id + "/" + epoch_key(clack.header.epoch);
            (void)artifact_idx_->put_raw(key, entry.dump());
        }

        if (v == Verb::Approve || v == Verb::Reject) {
            auto pj = json::parse(clack.payload, nullptr, false);
            auto ref_epoch = pj.value("epoch_ref", std::uint64_t{0});
            if (ref_epoch == 0) return;

            // Scan artifact_index for this epoch to update review_status
            auto apairs = artifact_idx_->handle()->prefix_scan("");
            if (!apairs) return;
            for (const auto& kv : *apairs) {
                auto aj = json::parse(kv.value, nullptr, false);
                if (aj.value("epoch", std::uint64_t{0}) == ref_epoch) {
                    aj["review_status"] = (v == Verb::Approve) ? "approved" : "rejected";
                    aj["review_epoch"]  = clack.header.epoch;
                    (void)artifact_idx_->put_raw(kv.key, aj.dump());
                }
            }
        }
    }

    void project_hitl_queue(const Clack& clack) {
        // Add to queue if HITL_REQ or BLOCKING
        if (clack.header.flags.hitl_req() || clack.header.flags.blocking()) {
            auto key = epoch_key(clack.header.epoch);
            (void)hitl_queue_->put_raw(key, serialize_clack(clack));
        }

        // Remove from queue on APPROVE/REJECT
        if (clack.header.verb == Verb::Approve || clack.header.verb == Verb::Reject) {
            auto pj = json::parse(clack.payload, nullptr, false);
            auto ref_epoch = pj.value("epoch_ref", std::uint64_t{0});
            if (ref_epoch != 0) {
                (void)hitl_queue_->del(epoch_key(ref_epoch));
            }
        }
    }

    std::unique_ptr<celer::Store> store_;
    std::optional<celer::TableRef> agent_log_;
    std::optional<celer::TableRef> task_board_;
    std::optional<celer::TableRef> thread_tree_;
    std::optional<celer::TableRef> presence_;
    std::optional<celer::TableRef> artifact_idx_;
    std::optional<celer::TableRef> hitl_queue_;
};

} // namespace cc

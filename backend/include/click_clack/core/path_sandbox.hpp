#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / core / path_sandbox.hpp
// Canonicalise + root-confine untrusted filesystem paths.
//
// Closes F-02 (path traversal) by resolving any user-supplied path
// under a fixed data root and rejecting anything that, after symlink
// resolution, escapes it. Used by the FFI config parser and by the
// CLI env-var path.
// ──────────────────────────────────────────────────────────────

#include <celer/celer.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace cc {

[[nodiscard]] inline auto resolve_under_root(const std::filesystem::path& root,
                                             std::string_view user_path)
    -> celer::Result<std::string>
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto canonical_root = fs::weakly_canonical(root, ec);
    if (ec) {
        return celer::Result<std::string>{std::unexpected(celer::Error{
            "config",
            "cannot canonicalise data root: " + ec.message()})};
    }

    fs::path requested{user_path};
    if (!requested.is_absolute()) {
        requested = canonical_root / requested;
    }

    const auto resolved = fs::weakly_canonical(requested, ec);
    if (ec) {
        return celer::Result<std::string>{std::unexpected(celer::Error{
            "config",
            "cannot canonicalise path: " + ec.message()})};
    }

    // Enforce prefix containment. Compare canonical path components to
    // reject `../` tricks and symlink escapes.
    auto rel = fs::relative(resolved, canonical_root, ec);
    if (ec || rel.empty() || rel.native().starts_with("..")) {
        return celer::Result<std::string>{std::unexpected(celer::Error{
            "config",
            "path escapes data root"})};
    }

    return celer::Result<std::string>{resolved.string()};
}

} // namespace cc

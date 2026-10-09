module;
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../samu/truss_samu.h"

module truss;
import :api;
import :impl;

namespace truss::detail {

Result<void> samu_build(std::string_view manifest, int jobs, bool verbose,
                        bool dry_run, bool explain, std::span<const std::string> targets) {
    std::vector<const char*> ctargets;
    ctargets.reserve(targets.size());
    for (const auto& t : targets)
        ctargets.push_back(t.c_str());

    const std::string manifest_str(manifest);
    truss_samu_options opts {};
    opts.manifest = manifest_str.c_str();
    opts.statusfmt = nullptr; /* default "[%s/%t] " */
    opts.maxjobs = jobs;
    opts.maxfail = 0; /* samu default: stop at first failure */
    opts.verbose = verbose ? 1 : 0;
    opts.dryrun = dry_run ? 1 : 0;
    opts.explain = explain ? 1 : 0;
    opts.ntargets = static_cast<int>(ctargets.size());
    opts.targets = ctargets.empty() ? nullptr : ctargets.data();

    /* NOTE: samurai's fatal() exits the process on hard errors (unknown
     * target, corrupt logs). Build *job* failures make build() report and
     * return; truss treats a non-zero truss_samu_build as failure. */
    if (const int rc = truss_samu_build(&opts); rc != 0)
        return std::unexpected(Error {Error::Kind::Command, "samurai build failed", std::filesystem::path(manifest_str)});
    return {};
}

Result<void> samu_compdb(std::string_view manifest, std::string_view outpath) {
    const std::string m(manifest), o(outpath);
    if (const int rc = truss_samu_compdb(m.c_str(), o.c_str()); rc != 0)
        return std::unexpected(Error {Error::Kind::Command, "compdb generation failed", std::filesystem::path(m)});
    return {};
}

} // namespace truss::detail

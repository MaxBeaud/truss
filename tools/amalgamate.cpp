// tools/amalgamate — generate build/dist/truss_all.hpp: a mate.h-style single-header
// drop-in of truss (library + vendored samurai + shim in one C++ TU).
//
// Build & run from the repo root:
//   clang++ -std=c++26 tools/amalgamate.cpp -o build-bootstrap/amalgamate
//   ./build/bootstrap/amalgamate            # writes build/dist/truss_all.hpp
//
// The tool concatenates, in order: vendored samurai (C sources, already
// C++-compatible via the truss patches), the samu shim, then truss's module
// units (api -> impl -> core). Module directives and partition imports are
// stripped; `export namespace` becomes `namespace`; local #include "x.h" are
// inlined once (deduped); system #include <x> are hoisted to the top.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::set<std::string> emitted_headers;
std::vector<std::string> system_includes; /* first-encounter order: some C
                          * headers are order-dependent */
std::set<std::string> seen_system_includes;
std::string body;

std::string read_all(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "amalgamate: cannot read %s\n", p.string().c_str());
        std::exit(1);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* process one source file; `path` is used to resolve local includes */
void process(const std::filesystem::path& path) {
    static const std::regex module_decl(R"(^\s*(export\s+)?module\s+[A-Za-z_][A-Za-z0-9_:.]*\s*;)");
    static const std::regex partition_import(R"(^\s*(export\s+)?import\s+:[A-Za-z_][A-Za-z0-9_:.,\s]*;)");

    std::istringstream in(read_all(path));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line == "module;" || std::regex_search(line, module_decl) || std::regex_search(line, partition_import))
            continue;
        if (line.starts_with("#pragma once"))
            continue;

        if (line.starts_with("#include \"")) {
            const auto end = line.find('"', 10);
            if (end == std::string::npos) {
                std::fprintf(stderr, "amalgamate: malformed #include in %s: %s\n",
                             path.string().c_str(), line.c_str());
                std::exit(1);
            }
            const std::filesystem::path inc = line.substr(10, end - 10);
            /* search: including file's dir, then the repo's include roots
             * (mirrors -Ivendor/samurai used for the shim) */
            std::filesystem::path full;
            for (const auto& dir : {path.parent_path(),
                                    std::filesystem::path("vendor/samurai"),
                                    std::filesystem::path("src/samu")}) {
                std::error_code ec;
                auto cand = std::filesystem::weakly_canonical(dir / inc, ec);
                if (std::filesystem::exists(cand)) {
                    full = cand;
                    break;
                }
            }
            if (full.empty()) {
                std::fprintf(stderr, "amalgamate: cannot resolve include '%s' from %s\n",
                             inc.string().c_str(), path.string().c_str());
                std::exit(1);
            }
            if (!emitted_headers.insert(full.string()).second)
                continue; /* deduped (the sources have no include guards) */
            process(full);
            continue;
        }
        if (line.starts_with("#include <")) {
            if (seen_system_includes.insert(line).second)
                system_includes.push_back(line);
            continue;
        }
        if (line.starts_with("export namespace "))
            line = line.substr(7);
        body += line;
        body += '\n';
    }
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::current_path();
    if (!std::filesystem::exists(root / "vendor/samurai/graph.c")) {
        std::fprintf(stderr, "amalgamate: run from the truss repo root\n");
        return 1;
    }

    /* vendored samurai (samu.c excluded: shim.c replaces its main()).
     * The C sources use C-isms (string literals into char*) that only warn
     * when compiled as C++; silence them for the vendored section only. */
    body +=
        "#if defined(__clang__)\n"
        "#pragma clang diagnostic push\n"
        "#pragma clang diagnostic ignored \"-Wwritable-strings\"\n"
        "#pragma clang diagnostic ignored \"-Wmissing-designated-field-initializers\"\n"
        "#pragma clang diagnostic ignored \"-Wmissing-field-initializers\"\n"
        "#pragma clang diagnostic ignored \"-Wc99-extensions\"\n"
        "#elif defined(__GNUC__)\n"
        "#pragma GCC diagnostic push\n"
        "#pragma GCC diagnostic ignored \"-Wwrite-strings\"\n"
        "#pragma GCC diagnostic ignored \"-Wmissing-field-initializers\"\n"
        "#endif\n";
    std::vector<std::filesystem::path> samu;
    for (const auto& e : std::filesystem::directory_iterator(root / "vendor/samurai"))
        if (e.path().extension() == ".c" && e.path().filename() != "samu.c")
            samu.push_back(e.path());
    std::ranges::sort(samu);
    for (const auto& f : samu)
        process(f);
    process(root / "src/samu/shim.c");
    body +=
        "#if defined(__clang__)\n"
        "#pragma clang diagnostic pop\n"
        "#elif defined(__GNUC__)\n"
        "#pragma GCC diagnostic pop\n"
        "#endif\n";

    /* truss: public types, internal decls, then implementation units */
    process(root / "src/api.cppm");
    process(root / "src/impl.cppm");
    std::vector<std::filesystem::path> core;
    for (const auto& e : std::filesystem::directory_iterator(root / "src/core"))
        if (e.path().extension() == ".cpp")
            core.push_back(e.path());
    std::ranges::sort(core);
    for (const auto& f : core)
        process(f);

    std::error_code ec;
    std::filesystem::create_directories(root / "build/dist", ec);
    std::ofstream out(root / "build/dist/truss_all.hpp", std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "amalgamate: cannot write build/dist/truss_all.hpp\n");
        return 1;
    }
    out << "// truss_all.hpp — amalgamated truss (library + samurai + shim).\n"
           "// GENERATED by tools/amalgamate.cpp — do not edit; regenerate instead.\n"
           "// Usage: #include \"truss_all.hpp\" in one TU, then use truss:: directly.\n"
           "#ifndef TRUSS_ALL_HPP\n"
           "#define TRUSS_ALL_HPP\n\n";
    for (const auto& inc : system_includes)
        out << inc << '\n';
    out << '\n' << body << "#endif // TRUSS_ALL_HPP\n";
    out.flush();
    std::printf("amalgamate: wrote build/dist/truss_all.hpp (%lld bytes)\n",
                static_cast<long long>(out.tellp()));
    return 0;
}

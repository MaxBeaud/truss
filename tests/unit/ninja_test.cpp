module;
#include <doctest/doctest.h>

#include <string>

module truss;
import :api;
import :impl;

using namespace truss;

TEST_CASE("ninja: golden manifest") {
    detail::NinjaWriter w;
    w.comment("generated");
    w.var("ninja_required_version", "1.3");
    w.rule("compile_cxx", {
        {"command", "$cxx $flags -c $in -o $out"},
        {"deps", "gcc"},
    });
    detail::NinjaWriter::Edge e;
    e.outputs = {"out/app.o"};
    e.rule = "compile_cxx";
    e.inputs = {"src/main.cpp"};
    e.vars = {{"cxx", "clang++"}, {"flags", "-O2 -DFOO=1"}};
    w.build(std::move(e));
    w.phony("all", {"out/app.o"});
    w.default_targets({"all"});

    const std::string expected =
        "# generated\n"
        "ninja_required_version = 1.3\n"
        "rule compile_cxx\n"
        "  command = $cxx $flags -c $in -o $out\n"
        "  deps = gcc\n"
        "\n"
        "build out/app.o: compile_cxx src/main.cpp\n"
        "  cxx = clang++\n"
        "  flags = -O2 -DFOO=1\n"
        "\n"
        "build all: phony out/app.o\n"
        "\n"
        "default all\n";
    CHECK(w.str() == expected);
}

TEST_CASE("ninja: escaping") {
    CHECK(detail::esc_value("a$$b") == "a$$$$b");
    CHECK(detail::esc_value("-Wl,-rpath,'$ORIGIN'/lib") == "-Wl,-rpath,'$$ORIGIN'/lib");
    CHECK(detail::esc_path("a b:c$d") == "a$ b$:c$$d");
}

TEST_CASE("ninja: implicit inputs render after |") {
    detail::NinjaWriter w;
    detail::NinjaWriter::Edge e;
    e.outputs = {"bin/app"};
    e.rule = "link_exe";
    e.inputs = {"a.o"};
    e.implicit_ins = {"libcore.a"};
    w.build(std::move(e));
    CHECK(w.str() == "build bin/app: link_exe a.o | libcore.a\n\n");
}

#!/usr/bin/env bash
# truss — single entry point for building, testing and installing truss.
# Plain clang++/clang only; no build system required (truss builds itself via
# ./truss once built). All generated output lives under build/.
#
#   ./setup.sh build              bootstrap libtruss.a + BMI, compile ./truss (self-host CLI)
#   ./setup.sh bootstrap          libtruss.a + truss BMIs -> build/bootstrap/
#   ./setup.sh unit               build + run the doctest unit tests (ASan/UBSan)
#   ./setup.sh examples           compile + run every example flow end-to-end
#   ./setup.sh installed          installed-tool flow against a staged prefix
#   ./setup.sh amalgam            generate build/dist/truss_all.hpp + compile/run fixture
#   ./setup.sh test               unit + examples + installed + amalgam
#   ./setup.sh install [prefix]   stage libtruss.a + BMIs (default ~/.local)
#   ./setup.sh docs               generate the Doxygen reference (build/docs/)
#   ./setup.sh clean              delete all generated output
#   ./setup.sh add-package <n>    symlink a local package dir into vendor/ (PKG_ROOT)
#
# Env overrides: CXX (default clang++), CC (default clang),
#                VCPKG_ROOT (required for the vcpkg example), PKG_ROOT (add-package).
set -euo pipefail
cd "$(dirname "$0")"

usage() { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; }

CXX="${CXX:-clang++}"
CC="${CC:-clang}"
OUT="build/bootstrap"
BMI="$OUT/bmi"
OBJ="$OUT/obj"

# Same language settings as truss's own self-host build: C++26 against libc++.
CXXFLAGS=(-std=c++26 -stdlib=libc++ -g -O1 -Wall -Wextra -Qunused-arguments)
# samurai is vendored (documented patches only): C17, no warnings, POSIX feature macros.
CFLAGS=(-std=gnu17 -g -O1 -w -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L)

# consumer flags: a project compiles its build script against the bootstrap BMI
# (the primary BMI does not embed the :api partition — pass both)
consumer_flags() {
    printf '%s\n' \
        "-fmodule-file=truss=$BMI/truss.pcm" \
        "-fmodule-file=truss:api=$BMI/truss_api.pcm"
}

need_tools() {
    command -v "$CXX" >/dev/null 2>&1 || { echo "setup: '$CXX' not found (clang++ required)" >&2; exit 1; }
    command -v "$CC"  >/dev/null 2>&1 || { echo "setup: '$CC' not found (clang required)"  >&2; exit 1; }
}

job() { printf '  %s\n' "$*"; "$@"; }

# --- bootstrap: libtruss.a + module BMIs with plain clang++ -------------------
cmd_bootstrap() {
    need_tools
    rm -rf "$OUT"
    mkdir -p "$BMI" "$OBJ"

    # module interface units (order: api -> impl -> primary)
    job "$CXX" -c src/api.cppm   "${CXXFLAGS[@]}" \
        -fmodule-output="$BMI/truss_api.pcm" -o "$OBJ/api.o"
    job "$CXX" -c src/impl.cppm  "${CXXFLAGS[@]}" \
        -fmodule-file="truss:api=$BMI/truss_api.pcm" \
        -fmodule-output="$BMI/truss_impl.pcm" -o "$OBJ/impl.o"
    job "$CXX" -c src/truss.cppm "${CXXFLAGS[@]}" \
        -fmodule-file="truss:api=$BMI/truss_api.pcm" \
        -fmodule-file="truss:impl=$BMI/truss_impl.pcm" \
        -fmodule-output="$BMI/truss.pcm" -o "$OBJ/truss.o"

    # module implementation units
    for src in src/core/*.cpp; do
        job "$CXX" -c "$src" "${CXXFLAGS[@]}" -Ivendor/samurai -Ivendor \
            -fmodule-file="truss=$BMI/truss.pcm" \
            -fmodule-file="truss:api=$BMI/truss_api.pcm" \
            -fmodule-file="truss:impl=$BMI/truss_impl.pcm" \
            -o "$OBJ/$(basename "${src%.cpp}").o"
    done

    # vendored samurai (samu.c excluded: its main() is replaced by the shim)
    for src in vendor/samurai/*.c src/samu/shim.c; do
        [ "$(basename "$src")" = samu.c ] && continue
        job "$CC" -c "$src" "${CFLAGS[@]}" -Ivendor/samurai -o "$OBJ/samu_$(basename "${src%.c}").o"
    done

    ar rcs "$OUT/libtruss.a" "$OBJ"/*.o
    echo "setup: built $OUT/libtruss.a + $OUT/bmi/truss.pcm"
}

# --- self-host CLI: compile truss.cpp (truss's own build script) --------------
cmd_build() {
    cmd_bootstrap
    mapfile -t flags < <(consumer_flags)
    job "$CXX" -std=c++26 -stdlib=libc++ "${flags[@]}" truss.cpp "$OUT/libtruss.a" -o truss
    echo "setup: built ./truss — run './truss debug' to build truss with truss"
}

# --- unit tests (doctest; truss lib compiled with ASan/UBSan) -----------------
cmd_unit() {
    need_tools
    cmd_bootstrap
    local TOBJ="build/unit-obj"
    rm -rf "$TOBJ"; mkdir -p "$TOBJ"
    local SAN=(-fsanitize=address,undefined)
    # truss implementation units recompiled with sanitizers (BMIs are shared)
    for src in src/core/*.cpp; do
        "$CXX" -c "$src" "${CXXFLAGS[@]}" "${SAN[@]}" -fno-sanitize-recover=all -Ivendor/samurai -Ivendor \
            -fmodule-file="truss=$BMI/truss.pcm" \
            -fmodule-file="truss:api=$BMI/truss_api.pcm" \
            -fmodule-file="truss:impl=$BMI/truss_impl.pcm" \
            -o "$TOBJ/$(basename "${src%.cpp}").o"
    done
    # test units (module truss; implementation units + doctest main TU)
    for src in tests/unit/*.cpp; do
        "$CXX" -c "$src" "${CXXFLAGS[@]}" "${SAN[@]}" -fno-sanitize-recover=all \
            -Itests -Ivendor/samurai -Ivendor \
            -fmodule-file="truss=$BMI/truss.pcm" \
            -fmodule-file="truss:api=$BMI/truss_api.pcm" \
            -fmodule-file="truss:impl=$BMI/truss_impl.pcm" \
            -o "$TOBJ/test_$(basename "${src%.cpp}").o"
    done
    # link: interface units (from bootstrap) + sanitized core + samu + tests
    "$CXX" "${CXXFLAGS[@]}" "$OBJ/api.o" "$OBJ/impl.o" "$OBJ/truss.o" \
        "$OBJ"/samu_*.o "$TOBJ"/*.o -fsanitize=address,undefined -o build/truss-tests
    ./build/truss-tests
}

# --- examples: compile each example's build script, run its flows -------------
run_example() {
    local ex="$1"; shift
    mapfile -t flags < <(consumer_flags)
    job "$CXX" -std=c++26 -stdlib=libc++ "${flags[@]}" \
        "examples/$ex/truss.cpp" "$OUT/libtruss.a" -o "examples/$ex/truss"
    ( cd "examples/$ex" && ./truss "$@" )
}

cmd_examples() {
    cmd_bootstrap
    run_example hello test
    run_example shared test
    run_example modules test
    run_example fetch test
    run_example conan test
    VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}" run_example vcpkg test
}

# --- installed-tool flow: stage a prefix, build against it --------------------
cmd_install() {
    local prefix="${1:-$HOME/.local}"
    cmd_bootstrap
    local dest="$prefix/lib/truss"
    mkdir -p "$dest"
    cp "$OUT/libtruss.a" "$dest/libtruss.a"
    cp "$OUT/bmi/truss.pcm" "$dest/truss.pcm"
    cp "$OUT/bmi/truss_api.pcm" "$dest/truss_api.pcm"
    echo "setup: staged libtruss.a + truss.pcm + truss_api.pcm into $dest"
}

cmd_installed() {
    local prefix="$OUT/prefix"
    cmd_install "$prefix"
    job "$CXX" -std=c++26 -stdlib=libc++ \
        -fmodule-file=truss="$prefix/lib/truss/truss.pcm" \
        -fmodule-file=truss:api="$prefix/lib/truss/truss_api.pcm" \
        examples/installed/truss.cpp "$prefix/lib/truss/libtruss.a" \
        -o examples/installed/truss
    ( cd examples/installed && ./truss test )
}

# --- amalgamation: single-header drop-in --------------------------------------
cmd_amalgam() {
    need_tools
    mkdir -p "$OUT"
    job "$CXX" -std=c++26 tools/amalgamate.cpp -o "$OUT/amalgamate"
    "./$OUT/amalgamate"
    job "$CXX" -std=c++26 -Wall -Wextra -Ibuild/dist examples/amalgam/truss.cpp -o examples/amalgam/truss
    ( cd examples/amalgam && ./truss test )
}

cmd_test() {
    cmd_unit
    cmd_examples
    cmd_installed
    cmd_amalgam
    echo "setup: all tests passed"
}

cmd_docs() {
    command -v doxygen >/dev/null 2>&1 || { echo "setup: doxygen not found" >&2; exit 1; }
    doxygen Doxyfile
    echo "setup: docs generated in build/docs/html"
}

cmd_clean() {
    rm -rf build/ .xmake/ dist/ build-bootstrap/ compile_commands.json truss
    rm -rf examples/*/build examples/*/truss examples/fetch/truss.lock \
           examples/conan/conan.lock examples/vcpkg/vcpkg.json
    echo "setup: cleaned"
}

cmd_add_package() {
    local pkg_root="${PKG_ROOT:-$HOME/Documents/libs}"
    if [ $# -eq 0 ]; then
        command -v fzf >/dev/null 2>&1 || { echo "setup: fzf required for interactive add-package" >&2; exit 1; }
        local selected
        selected=$(ls -1 "$pkg_root" | fzf --multi --prompt="Select packages: ") || true
        [ -z "$selected" ] && { echo "No packages selected."; exit 0; }
        while IFS= read -r p; do cmd_add_package "$p"; done <<< "$selected"
        return
    fi
    local package="$1"
    if [ ! -d "$pkg_root/$package" ]; then
        echo "setup: '$pkg_root/$package' not found." >&2
        ls -1 "$pkg_root" | grep -i "$package" >&2 || true
        exit 1
    fi
    mkdir -p vendor
    ln -sf "$pkg_root/$package" "vendor/$package"
    echo "setup: linked vendor/$package"
}

case "${1:-build}" in
    build)     cmd_build ;;
    bootstrap) cmd_bootstrap ;;
    unit)      cmd_unit ;;
    examples)  cmd_examples ;;
    installed) cmd_installed ;;
    amalgam)   cmd_amalgam ;;
    test)      cmd_test ;;
    install)   shift; cmd_install "${1:-}" ;;
    docs)      cmd_docs ;;
    clean)     cmd_clean ;;
    add-package) shift; cmd_add_package "$@" ;;
    -h|--help|help) usage ;;
    *) echo "setup: unknown command '$1'" >&2; usage >&2; exit 1 ;;
esac

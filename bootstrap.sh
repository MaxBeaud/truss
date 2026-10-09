#!/usr/bin/env bash
# truss bootstrap — build libtruss.a + the truss module BMI with plain clang++.
# No xmake required: this is the "clone and go" path (PLAN.MD phase 7).
#
#   ./bootstrap.sh                       build into build-bootstrap/
#   ./bootstrap.sh --install [prefix]    also stage libtruss.a + BMI (default ~/.local)
#
# Env overrides: CXX (default clang++), CC (default clang).
#
# After bootstrapping, a project compiles its build script against the BMI
# (the primary BMI does not embed the :api partition — pass both):
#   $CXX -std=c++26 -stdlib=libc++ \
#        -fmodule-file=truss=build-bootstrap/bmi/truss.pcm \
#        -fmodule-file=truss:api=build-bootstrap/bmi/truss_api.pcm \
#        truss.cpp build-bootstrap/libtruss.a -o truss
set -euo pipefail
cd "$(dirname "$0")"

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
}

install=0
prefix="$HOME/.local"
while [ $# -gt 0 ]; do
    case "$1" in
        --install)
            install=1
            if [ $# -gt 1 ] && [ "${2#-}" = "$2" ]; then prefix="$2"; shift; fi
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "bootstrap: unknown argument '$1'" >&2; usage >&2; exit 1 ;;
    esac
    shift
done

CXX="${CXX:-clang++}"
CC="${CC:-clang}"
command -v "$CXX" >/dev/null 2>&1 || { echo "bootstrap: '$CXX' not found (clang++ required)" >&2; exit 1; }
command -v "$CC"  >/dev/null 2>&1 || { echo "bootstrap: '$CC' not found (clang required)"  >&2; exit 1; }

OUT="build-bootstrap"
BMI="$OUT/bmi"
OBJ="$OUT/obj"

# Same language settings as the xmake build: C++26 module against libc++.
CXXFLAGS=(-std=c++26 -stdlib=libc++ -g -O1 -Wall -Wextra -Qunused-arguments)
# samurai is vendored unmodified: C17, no warnings, POSIX feature macros.
CFLAGS=(-std=gnu17 -g -O1 -w -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L)

rm -rf "$OUT"
mkdir -p "$BMI" "$OBJ"

job() { printf '  %s\n' "$*"; "$@"; }

# --- module interface units (order: api -> impl -> primary) -----------------
job "$CXX" -c src/api.cppm   "${CXXFLAGS[@]}" \
    -fmodule-output="$BMI/truss_api.pcm" -o "$OBJ/api.o"
job "$CXX" -c src/impl.cppm  "${CXXFLAGS[@]}" \
    -fmodule-file="truss:api=$BMI/truss_api.pcm" \
    -fmodule-output="$BMI/truss_impl.pcm" -o "$OBJ/impl.o"
job "$CXX" -c src/truss.cppm "${CXXFLAGS[@]}" \
    -fmodule-file="truss:api=$BMI/truss_api.pcm" \
    -fmodule-file="truss:impl=$BMI/truss_impl.pcm" \
    -fmodule-output="$BMI/truss.pcm" -o "$OBJ/truss.o"

# --- module implementation units ---------------------------------------------
for src in src/core/*.cpp; do
    job "$CXX" -c "$src" "${CXXFLAGS[@]}" -Ivendor/samurai -Ivendor \
        -fmodule-file="truss=$BMI/truss.pcm" \
        -fmodule-file="truss:api=$BMI/truss_api.pcm" \
        -fmodule-file="truss:impl=$BMI/truss_impl.pcm" \
        -o "$OBJ/$(basename "${src%.cpp}").o"
done

# --- vendored samurai (samu.c excluded: its main() is replaced by the shim) --
for src in vendor/samurai/*.c src/samu/shim.c; do
    [ "$(basename "$src")" = samu.c ] && continue
    job "$CC" -c "$src" "${CFLAGS[@]}" -Ivendor/samurai -o "$OBJ/samu_$(basename "${src%.c}").o"
done

# --- archive -----------------------------------------------------------------
ar rcs "$OUT/libtruss.a" "$OBJ"/*.o
echo "bootstrap: built $OUT/libtruss.a + $OUT/bmi/truss.pcm"

# --- optional staging (installed-tool flow, PLAN.MD phase 8) ------------------
if [ "$install" = 1 ]; then
    dest="$prefix/lib/truss"
    mkdir -p "$dest"
    cp "$OUT/libtruss.a" "$dest/libtruss.a"
    cp "$OUT/bmi/truss.pcm" "$dest/truss.pcm"
    cp "$OUT/bmi/truss_api.pcm" "$dest/truss_api.pcm"
    echo "bootstrap: staged libtruss.a + truss.pcm + truss_api.pcm into $dest"
fi

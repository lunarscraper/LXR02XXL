#!/usr/bin/env bash
# Host (x86) build of the plugin for the offline tests (run under valgrind; SAN=-fsanitize=... where clang has the runtimes). Usage: test/build_host.sh [extra cflags]
set -euo pipefail
cd "$(dirname "$0")/.."
. ./build_srcs.sh
MPC_VST=third_party/mpc-vst-plugins
python3 tools/gen_params.py >/dev/null
python3 $MPC_VST/tools/gen_vst.py vst.json --params-h >/dev/null
mkdir -p build/host
CC="${CC:-clang}"
SAN="${SAN:-}"   # clang here has no sanitizer runtimes: the tests run under valgrind
FL="-O1 -g -w $SAN -Ibuild -I$MPC_VST/wrapper $INCS $*"
OBJS=""
for f in $ENGINE_SRCS; do o="build/host/$(echo $f | tr / _).o"; $CC -c $FL -include src/shim/lxr_state.h $f -o $o; OBJS="$OBJS $o"; done
for f in $PLUGIN_SRCS src/wrapper/vst2_wrap.c; do o="build/host/$(echo $f | tr / _).o"; $CC -c $FL $f -o $o; OBJS="$OBJS $o"; done
echo "$OBJS" > build/host/objs.txt

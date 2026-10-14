#!/usr/bin/env bash
# Offline tests: the framework's host test (the VST2 wrapper around the engine) and LXRXXL's engine tests, under valgrind.
set -euo pipefail
cd "$(dirname "$0")/.."
. ./build_srcs.sh
bash test/build_host.sh
CC="${CC:-clang}"
OBJS=$(cat build/host/objs.txt)
ENG=$(echo $OBJS | tr ' ' '\n' | grep -v vst2_wrap | tr '\n' ' ')
$CC -O1 -g -w -Ibuild third_party/mpc-vst-plugins/tools/host_test.c $OBJS -lm -lpthread -ldl -o build/host/host_test
$CC -O1 -g -w -Ibuild -Ithird_party/mpc-vst-plugins/wrapper $INCS test/engine_test.c $ENG -lm -lpthread -ldl -o build/host/engine_test
$CC -O1 -g -w -Ibuild -Isrc test/mpc_sim.c $OBJS -lm -lpthread -ldl -o build/host/mpc_sim
$CC -O1 -g -w -Ibuild -Ithird_party/mpc-vst-plugins/wrapper $INCS test/stems_test.c $ENG -lm -lpthread -ldl -o build/host/stems_test
VG="valgrind -q --error-exitcode=9"
[ "${NOVG:-}" ] && VG=""
# a fresh device: no pad layout learned yet (the data folder is MODULE_DIR, else next to the test binaries)
rm -f /sdcard/LXRXXL/settings.cfg "build/host/LXRXXL Data/settings.cfg"
(cd build/host && $VG ./host_test && $VG ./engine_test && rm -f /sdcard/LXRXXL/settings.cfg "LXRXXL Data/settings.cfg" && $VG ./mpc_sim)
$VG build/host/stems_test

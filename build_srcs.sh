# The source lists, shared by build.sh and test/run_tests.sh.
L=third_party/lxr02open/Core
# the LXR engine (its globals become per-instance state: compiled with -include src/shim/lxr_state.h)
ENGINE_SRCS="$(ls $L/DSPAudio/*.c | grep -v -e '/mixer.c$' -e '/random.c$') src/shim/random.c $L/Preset/ParameterArray.c $L/Menu/Cc2Text.c src/lxr/lxr_dsp.c"
# the plugin
PLUGIN_SRCS="src/lxr/lxr_fx.c src/meta.c src/lxr.c src/seq.c src/ui.c src/random.c src/factory_data.c src/store.c src/lxr_state.c src/engine.c"
INCS="-Isrc -Isrc/shim -Isrc/lxr -Ithird_party/lxr02open -I$L -I$L/DSPAudio -I$L/Preset -I$L/MIDI -I$L/Menu -I$L/SampleRom"

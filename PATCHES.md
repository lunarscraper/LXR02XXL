# Changes to the vendored LXR code

`third_party/lxr02open/` is [LXR02Open](https://github.com/brendanclarke/LXR02Open) at the commit in
`VENDORED_COMMIT`, only the files the engine needs. Everything else (the sequencer, the front panel, the FX unit,
storage, the plugin) is LXRXXL's own code under `src/`. Changed files:

- `Core/DSPAudio/Oscillator.h`, `Oscillator.c`: the LXR-02's **PWM** waveform (`OSC_PWM` = 5, where 0.37 had the
  cymbal wave, which moves to `CRASH` = 6 and the samples to 7, as in the LXR-02's waveform list), a pulse made of
  two band-limited saws a duty cycle apart (`pwmOffset` per oscillator), in the plain, block and FM paths and the
  frequency calculation. `calcNoise()` read the ARM's overflow flag (APSR V) to see the phase wrap; it compares the
  phase with the previous one instead.
- `Core/DSPAudio/modulationNode.c`: the waveform range for the modulation targets ends at PWM when there are no
  samples; `modNode_setOriginalValueChanged()` skips a destination without a parameter (slot 0, whose pointer is NULL;
  reading address 0 is harmless on the STM32, a crash here).
- `Core/DSPAudio/random.c` is not built: it reads the STM32's hardware RNG registers. `src/shim/random.c` gives the
  same functions from a xorshift generator.
- `Core/DSPAudio/mixer.c` is not built: the mixer is `lxr_dsp_render32()` in `src/lxr/lxr_dsp.c`, the same
  arithmetic plus the LXR-02's FX bus.
- `Core/MIDI/MidiParser.c`: its CC handler is extracted verbatim by `tools/extract_cc.py` into
  `src/lxr/cc_handler.inc` (the parser's own state and the hardware MIDI ports left out), and the menu's tables
  (`parameter_dtypes`, value names) from `Core/Menu/menu.c.ref` into `src/lxr/menu_tables.inc`.

Build-level substitutions, no source change: `src/shim/` replaces the STM32 headers (`stm32f4xx.h`, the CMSIS
intrinsics `__SSAT`, `__QADD16`... as portable C), `config.h` (REAL_FS 44100, no TCM sections), `adcPots.h`,
`AudioCodecManager.h` and `sequencer.h` (the tempo comes from the plugin). Every engine file is compiled with
`-include src/shim/lxr_state.h`, which puts its globals in sections `src/lxr_state.c` swaps per plugin instance.

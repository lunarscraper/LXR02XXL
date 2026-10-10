#!/usr/bin/env python3
"""Import an LXR-02 project folder (the machine's SD card files) as LXRXXL's factory content.

    python3 tools/import_lxr02.py "presets/Hrtl LXR-02 DLC/ProjectFiles/DLCprjct (w demo patterns)"

writes src/factory_data.c: the project's 64 kits and 64 patterns in LXRXXL's own format. A new LXRXXL, and the
blue RESET key, start from it.

The LXR-02 (Erica Synths firmware 1.70) files, as read here (checked against the files themselves and the parameter
tables in the 1.70 firmware image; nothing of the firmware is copied, only these layouts):
  NN-NAME.SND  255 bytes: the kit's name (8), then 247 parameter bytes. 0..227 are the LXR 0.37 sound parameters in
               their order (LXR02Open Core/Preset/ParameterArray.h), 228..233 the six voices' pulse widths, then the FX
               unit: 234 type, 235 output, 236 dry/wet, 237 drive type, 238..241 its four values, 242 compressor
               ratio, 243 ring modulator wave, 245 delay type (its menu pages list exactly these).
               The modulation targets (velocity destinations, LFO targets) are indices into the LXR-02's own target
               list (ERICA_TARGETS below: the 0.37 list with each voice's pulse width added after its waveform, the
               MIDI note targets gone and the FX unit's four values at the end of each voice); they are converted to
               the 0.37 list the engine uses. Targets the engine has no modulation for (pulse width, the FX values)
               become "off".
  NN-.PAT      3663 bytes: name (8), "PatV03", then 7 tracks of 64 steps x 7 bytes (velocity with bit 7 set by the
               firmware, probability 0-100, note, automation 1 target + value, automation 2 target + value; target
               0xff = none, else the automation node's destination: 1..127 = parameter n - 1, from 128 = parameter n)
               and 64 bytes the firmware fills with 0x70; then per track a 64-bit mask of the steps that are on
               (LSB first), one byte, the 7 track lengths, and the kit the pattern was made with.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "third_party", "lxr02open", "Core")

# the LXR-02 1.70 modulation target list: parameter numbers (0.37 numbering; 228+ = the LXR-02's own)
ERICA_TARGETS = [
    0, 114,
    8, 9, 1, 228, 49, 50, 62, 70, 78, 74, 149, 143, 137, 82, 83, 20, 134, 203, 197, 209, 37, 43, 191, 128, 115, 179,
    121, 155, 173, 185, 88, 94, 108, 102, 238, 239, 240, 241,
    10, 11, 2, 229, 51, 52, 63, 71, 79, 75, 150, 144, 138, 84, 85, 21, 135, 204, 198, 210, 38, 44, 192, 129, 116, 180,
    122, 156, 174, 186, 89, 95, 109, 103, 238, 239, 240, 241,
    12, 13, 3, 230, 53, 54, 64, 72, 80, 76, 151, 145, 139, 86, 87, 22, 136, 205, 199, 211, 39, 45, 193, 130, 117, 181,
    123, 157, 175, 187, 90, 96, 110, 104, 238, 239, 240, 241,
    14, 15, 27, 28, 4, 231, 55, 56, 68, 65, 73, 81, 77, 152, 146, 140, 206, 200, 212, 40, 46, 194, 131, 118, 182, 124,
    158, 176, 188, 91, 99, 111, 105, 238, 239, 240, 241,
    16, 17, 6, 232, 57, 58, 69, 66, 153, 147, 141, 29, 30, 31, 32, 23, 24, 207, 201, 213, 41, 47, 195, 132, 119, 183,
    125, 159, 177, 189, 92, 100, 112, 106, 238, 239, 240, 241,
    18, 19, 7, 233, 59, 60, 61, 67, 154, 148, 142, 33, 34, 35, 36, 25, 26, 208, 202, 214, 42, 48, 196, 133, 120, 184,
    126, 160, 178, 190, 93, 101, 113, 107, 238, 239, 240, 241,
]
NUM_SOUND = 228
# LXR-02 kit byte -> LXRXXL extension index (src/lxr/lxr_dsp.h: PWM1..6, FX type, out, dry/wet, drive type, P1..P4,
# ratio, ring wave, delay type)
EXT_FROM_FILE = [228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 245]


def param_index():
    s = open(os.path.join(CORE, "Preset", "ParameterArray.h")).read()
    a = s.index("PAR_NONE = 0")
    body = s[a:s.index("}", a)]
    idx, i = {}, 0
    for line in body.split("\n"):
        line = line.split("//")[0].strip()
        m = re.match(r"(PAR_\w+|END_OF_SOUND_PARAMETERS)\s*(=\s*(\w+))?", line)
        if not m:
            continue
        n, v = m.group(1), m.group(3)
        if v is not None:
            i = int(v) if v.isdigit() else idx.get(v, i)
        idx[n] = i
        if n != "PAR_MOD_WHEEL":
            i += 1
    assert idx["END_OF_SOUND_PARAMETERS"] == NUM_SOUND
    return idx


def targets_037(idx):
    src = open(os.path.join(CORE, "Menu", "Cc2Text.c")).read()
    return [idx[p] for p in re.findall(r"\{TEXT_\w+,(PAR_\w+)\}", src)]


def main(folder):
    idx = param_index()
    t037 = targets_037(idx)
    to037 = {}
    for i, p in enumerate(t037):
        to037.setdefault(p, i)
    conv_targets = [idx["PAR_VEL_DEST_%d" % v] for v in range(1, 7)] + [idx["PAR_TARGET_LFO%d" % v] for v in range(1, 7)]

    def target(t):
        p = ERICA_TARGETS[t] if t < len(ERICA_TARGETS) else 0
        return to037.get(p, 0) if p < NUM_SOUND else 0

    files = os.listdir(folder)

    def slot(ext, n):
        for f in files:
            if f.upper().endswith(ext) and f[:2] == "%02d" % n:
                return os.path.join(folder, f)
        return None

    kits, dropped = [], 0
    for n in range(64):
        b = open(slot(".SND", n), "rb").read()
        assert len(b) == 255, "kit %d: %d bytes" % (n, len(b))
        name = "".join(c if 32 <= ord(c) < 127 else " " for c in b[:8].decode("latin-1"))
        p = list(b[8:])
        sound = p[:NUM_SOUND]
        for par in conv_targets:
            t = target(sound[par])
            if sound[par] and not t:
                dropped += 1
            sound[par] = t
        kits.append((name, sound + [p[i] for i in EXT_FROM_FILE]))
    pats = []
    for n in range(64):
        b = open(slot(".PAT", n), "rb").read()
        assert len(b) == 3663 and b[8:14] == b"PatV03", "pattern %d" % n
        tail = b[14 + 7 * 512:]
        steps, lens = [], list(tail[57:64])
        for t in range(7):
            mask = int.from_bytes(tail[t * 8:t * 8 + 8], "little")
            for s in range(64):
                o = 14 + t * 512 + s * 7
                vel, prob, note, a1, v1, a2, v2 = b[o:o + 7]

                def autom(d):
                    if d == 0xFF or d == 0:
                        return 0
                    par = d - 1 if d <= 127 else d
                    return par + 1 if par < NUM_SOUND else 0
                steps.append(((mask >> s) & 1, vel & 0x7F, note & 0x7F, min(prob, 100), autom(a1), v1 & 0x7F,
                              autom(a2), v2 & 0x7F))  # LXRXXL's Step order
        pats.append((steps, lens, tail[64] & 63, any(st[0] for st in steps)))

    out = ["/* GENERATED by tools/import_lxr02.py from \"%s\": do not edit.\n"
           " * LXRXXL's factory content: the LXR-02 project's 64 kits and 64 patterns. */" % os.path.relpath(folder, ROOT),
           '#include "lxr.h"', "",
           "const Kit lxr_factoryKits[NUM_KITS] = {"]
    for name, bytes_ in kits:
        out.append('    {"%s", {%s}},' % (name.replace("\\", " ").replace('"', " "), ",".join(map(str, bytes_))))
    out += ["};", "",
            "/* the patterns with steps: (pattern, kit, 7 lengths, then 7 x 64 steps: on, vel, note, prob, p1, v1, p2, v2) */",
            "const LxrFactoryPattern lxr_factoryPatterns[] = {"]
    used = 0
    for n, (steps, lens, kit, nonempty) in enumerate(pats):
        if not nonempty:
            continue
        used += 1
        rows = ",".join("{" + ",".join("{%d,%d,%d,%d,%d,%d,%d,%d}" % st for st in steps[t * 64:(t + 1) * 64]) + "}"
                        for t in range(7))
        out.append("    {%d, %d, {%s}, {%s}}," % (n, kit, ",".join(str(l if l else 16) for l in lens), rows))
    out += ["};", "const int lxr_numFactoryPatterns = %d;" % used, ""]
    open(os.path.join(ROOT, "src", "factory_data.c"), "w").write("\n".join(out))
    print("factory: %d kits (%s), %d patterns; %d modulation targets without an engine target set off"
          % (len(kits), ", ".join(k[0].strip() for k in kits[:16]), used, dropped))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         os.path.join(ROOT, "presets", "Hrtl LXR-02 DLC", "ProjectFiles", "DLCprjct (w demo patterns)"))

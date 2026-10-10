#!/usr/bin/env python3
"""A picture of the built page, as MPC would draw it (offline check of the skin, not part of the build).

    python3 tools/preview.py "<skin dir>/Plugin Skins" out.png [state.json]

state.json (optional): {"lit": ["play", "s1", ...], "values": {"fader1": 0.8, ...}, "text": {"lcd1": "...", ...},
"names": {"slot1": "FRQ", ...}, "hidden": ["slot8", ...]}. Without it a demo state is drawn."""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import panel as P  # noqa: E402

FONT = os.path.join(P.ROOT, "art", "fonts", "TitilliumWeb-SemiBold.ttf")
DEMO = {
    "lit": ["play", "osc", "v1", "rnd", "s1", "s5", "s9", "s13", "s3", "s11"],
    "values": {"fader1": 1, "fader2": 1, "fader3": 1, "fader4": 1, "fader5": 1, "fader6": 1, "fader7": 0.8,
               "slot1": 0.35, "slot2": 0.5, "slot3": 0.0, "slot4": 0.2, "slot5": 0.6, "slot6": 0.5, "slot7": 0.0,
               "slot8": 0.4, "rnd_amt": 0.5, "srnd_amt": 0.3},
    "text": {"lcd1": "DRUM1 OSC P01", "lcd2": "Coarse 44",
             "slot1": "SIN", "slot2": "44", "slot3": "0", "slot4": "0", "slot5": "64", "slot6": "-", "slot7": "0",
             "slot8": "51", "rnd_amt": "50%", "srnd_amt": "30%", "pot1": "SIN", "pot2": "44", "pot3": "0", "pot4": "0"},
    "names": {"slot1": "WAV", "slot2": "COA", "slot3": "FIN", "slot4": "MOD", "slot5": "PWM", "slot6": "-",
              "slot7": "TRA", "slot8": "DEC"},
    "hidden": [],
}


def main(skin, out, state_path=None):
    st = json.load(open(state_path)) if state_path else DEMO
    tui = json.load(open(os.path.join(skin, "TUI.json")))
    defs = {e["key"]: e["value"] for e in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    params = json.load(open(os.path.join(P.ROOT, "params.json")))["params"]
    page = defs["LXR-02|LXR-02"]["componentsData"]
    img = Image.new("RGB", (P.W, P.H))
    d = ImageDraw.Draw(img)
    for c in page:
        typ = c["componentData"]["type"]
        x, y, w, h = map(int, c["bounds"]["bounds"].split())
        if typ == "Image":
            img.paste(Image.open(os.path.join(skin, c["componentData"]["data"]["image"])).convert("RGB").resize((w, h)), (x, y))
            continue
        m = {e["key"]: e["value"] for e in c["handle remapping"]["map"]}
        pi = int(m.get("Data", "Parameter -1").split()[1])
        key = params[pi]["key"] if pi >= 0 else ""
        if key in st.get("hidden", []):
            continue
        for sub in defs[typ]["componentsData"]:
            cd, sx, sy, sw, sh = sub["componentData"], *map(int, sub["bounds"]["bounds"].split())
            if sub["bounds"]["whenVisible"] == "WhenFocussed":
                continue
            t = cd["type"]
            if t == "Button":
                on = key in st["lit"]
                im = Image.open(os.path.join(skin, cd["data"]["onImage" if on else "offImage"])).convert("RGB")
                img.paste(im.resize((sw, sh)), (x + sx, y + sy))
            elif t == "Knob":
                strip = Image.open(os.path.join(skin, cd["data"]["filmStrip"])).convert("RGB")
                n = cd["data"]["numFrames"]
                fh = strip.size[1] // (n + 1) if strip.size[1] % n else strip.size[1] // n
                fh = sh if strip.size[1] % sh == 0 else fh
                frames = strip.size[1] // fh
                v = st["values"].get(key, 0.5)
                k = min(frames - 1, int(round(v * (frames - 1))))
                img.paste(strip.crop((0, k * fh, strip.size[0], (k + 1) * fh)).resize((sw, sh)), (x + sx, y + sy))
            elif t == "Label":
                f = cd["data"]["textStyle"]["font"]["height"]
                if f <= 0:
                    continue
                kind = cd["data"]["type"]
                s = (st["names"].get(key) or params[pi]["name"]) if kind == "Name" else st["text"].get(key, "")
                col = "#" + cd["data"]["textStyle"]["colour"][2:]
                fnt = ImageFont.truetype(FONT, int(f))
                b = d.textbbox((0, 0), s, font=fnt)
                just = cd["data"]["textStyle"]["justification"]
                tx = x + sx + (sw - (b[2] - b[0])) / 2 if "horizontallyCentred" in just else \
                    x + sx + sw - (b[2] - b[0]) if "right" in just else x + sx
                d.text((tx, y + sy + (sh - (b[3] - b[1])) / 2 - b[1]), s, font=fnt, fill=col)
    img.save(out)
    print(out)


if __name__ == "__main__":
    main(*sys.argv[1:])

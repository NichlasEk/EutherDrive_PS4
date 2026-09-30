#!/usr/bin/env python3
"""Stage an explicitly supplied private ROM library; never discover commercial data."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import struct

def input_roms():
    if os.environ.get("ED_CONSOLE_LIBRARY"):
        items = json.loads(Path(os.environ["ED_CONSOLE_LIBRARY"]).read_text())
    elif os.environ.get("ED_CONSOLE_ROM"):
        items = [os.environ["ED_CONSOLE_ROM"]]
    else:
        raise SystemExit("Set ED_CONSOLE_LIBRARY to a JSON list of local ROM paths, or ED_CONSOLE_ROM")
    if not isinstance(items, list) or not 1 <= len(items) <= 32:
        raise SystemExit("Expected 1..32 ROM paths")
    result = []
    for item in items:
        path = Path(item).resolve(strict=True)
        if path.suffix.lower() not in (".sms", ".md", ".gen", ".smd", ".sfc", ".smc") or not 8192 <= path.stat().st_size <= 16*1024*1024:
            raise SystemExit("Invalid console ROM: " + str(path))
        result.append(path)
    return result

if __name__ == "__main__":
    stage = Path(sys.argv[1])
    entries, provenance = [], []
    for i, path in enumerate(input_roms()):
        name = f"rom{i:02d}{path.suffix.lower()}"
        label = path.stem.replace("\t", " ").replace("\n", " ").replace("\r", " ")
        shutil.copyfile(path, stage / name)
        entries.append(f"{label}\t{name}\n")
        provenance.append(dict(name=name, title=label, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        # Native preview asset from this build's validated framebuffer capture.
        from PIL import Image
        capture = Path(__file__).resolve().parent.parent / "build/console-player" / f"rom{i:02d}" / "final.ppm"
        picture = Image.open(capture).convert("RGB")
        picture.thumbnail((320, 240), Image.Resampling.NEAREST)
        with (stage / f"rom{i:02d}.preview").open("wb") as preview:
            preview.write(struct.pack("<ii", *picture.size))
            for r, g, b in picture.get_flattened_data():
                preview.write(struct.pack("<I", 0xff000000 | r << 16 | g << 8 | b))
    (stage / "library.tsv").write_text("".join(entries))
    (stage / "library-private.json").write_text(json.dumps(provenance, indent=2) + "\n")

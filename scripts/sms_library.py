#!/usr/bin/env python3
"""Stage an explicitly supplied private ROM library; never discover commercial data."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

def input_roms():
    if os.environ.get("ED_SMS_LIBRARY"):
        items = json.loads(Path(os.environ["ED_SMS_LIBRARY"]).read_text())
    elif os.environ.get("ED_SMS_ROM"):
        items = [os.environ["ED_SMS_ROM"]]
    else:
        raise SystemExit("Set ED_SMS_LIBRARY to a JSON list of local ROM paths, or ED_SMS_ROM")
    if not isinstance(items, list) or not 1 <= len(items) <= 32:
        raise SystemExit("Expected 1..32 ROM paths")
    result = []
    for item in items:
        path = Path(item).resolve(strict=True)
        if path.suffix.lower() not in (".sms",) or not 8192 <= path.stat().st_size <= 16*1024*1024:
            raise SystemExit("Invalid SMS ROM: " + str(path))
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
    (stage / "library.tsv").write_text("".join(entries))
    (stage / "library-private.json").write_text(json.dumps(provenance, indent=2) + "\n")

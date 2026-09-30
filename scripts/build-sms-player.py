#!/usr/bin/env python3
"""Build the pinned EutherDrive GB slice against the already-tested Mono profile."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from sms_library import input_roms

root = Path(__file__).resolve().parent.parent
source = root.parent / "EutherDrive_Android"
revision = "7771ae7f736caef7f20399a6315d3140217bfbd2"
prefix = "EutherDrive.Core/"
player = True
out = root / "build/sms-player"
out.mkdir(parents=True, exist_ok=True)
paths = subprocess.check_output(["git", "-C", str(source), "ls-tree", "-r", "--name-only", revision, prefix], text=True).splitlines()
files, hashes = [], {}
for path in paths:
    if not path.endswith(".cs") or not ("/SmsGg/" in path or "/Cpu/Z80Emu/" in path or path.endswith("MdTracerCore/md_music_sn76489_jgenesis.cs")) or path.endswith(("SmsGgPortSession.cs", "SmsGgRomLoader.cs")):
        continue
    raw = subprocess.check_output(["git", "-C", str(source), "show", f"{revision}:{path}"])
    hashes[path] = hashlib.sha256(raw).hexdigest()
    code = raw.decode("utf-8-sig")
    if re.search(r"Reflection\.Emit|DynamicMethod|Expression\.Compile", code):
        raise SystemExit("Unexpected dynamic-code dependency: " + path)
    # Only framework compatibility; preserve core algorithms and instructions.
    code = re.sub(r"Array.Empty<([^>]+)>\(\)", r"new \1[0]", code)
    code = re.sub(r"Array.Clear\((\w+)\);", r"Array.Clear(\1, 0, \1.Length);", code)
    code = code.replace("byte InterruptVector() => 0xff;", "byte InterruptVector();")
    if path.endswith("SmsGgZ80BusAdapter.cs"):
        code = code.replace("public bool BusReq()", "public byte InterruptVector() => 0xff;\n    public bool BusReq()")
    code = code.replace("Math.Clamp(", "Orbis.Framework.Clamp(")
    code = code.replace("BitOperations.PopCount(value)", "Orbis.Framework.PopCount(value)")
    code = code.replace('"TMR SEGA"u8', 'System.Text.Encoding.ASCII.GetBytes("TMR SEGA")')
    if path.endswith("SmsGgSeedCore.cs"):
        code = code.replace("using EutherDrive.Core.Savestates;", "")
        # Savestates are not exposed by this player. Exclude the serializer dependency.
        start = code.index("    public void SaveState(")
        end = code.index("    private int GetMasterClocksPerFrame", start)
        code = code[:start] + code[end:]
    code = re.sub(r"ArgumentNullException\.ThrowIfNull\((\w+)\);",
                  lambda m: f'if ({m[1]} == null) throw new ArgumentNullException("{m[1]}");', code)
    target = out / "core" / path.removeprefix(prefix)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(code)
    files.append(str(target))
if len(files) != 30:
    raise SystemExit(f"Unexpected source slice: {len(files)} files")
(out / "source-manifest.json").write_text(json.dumps({"revision": revision, "sha256": hashes}, indent=2) + "\n")
reference = Path(subprocess.check_output([str(root / "scripts/prepare-mono-reference.sh")], text=True).strip())
bcl = reference / "mono/4.5"
sdk = subprocess.check_output(["dotnet", "--list-sdks"], text=True).splitlines()[-1]
version, sdk_root = re.match(r"(\S+) \[(.+)\]", sdk).groups()
csc = Path(sdk_root) / version / "Roslyn/bincore/csc.dll"
refs = [Path("/usr/lib/mono/4.5-api") / name for name in ["mscorlib.dll", "System.dll", "System.Core.dll"]]
refs += [bcl / name for name in ["System.ValueTuple.dll", "System.Memory.dll", "System.Runtime.dll", "System.Runtime.CompilerServices.Unsafe.dll", "System.Buffers.dll", "System.Numerics.Vectors.dll"]]
if player:
    refs.append(bcl / "System.Runtime.InteropServices.dll")
host_dir = out / "host"
host_dir.mkdir(exist_ok=True)
executable = host_dir / "main.exe"
subprocess.run(["dotnet", str(csc), "-nologo", "-nostdlib+", "-nullable:annotations", "-langversion:latest", "-target:exe", "-platform:x64", "-optimize+", "-out:" + str(executable)] + ["-r:" + str(p) for p in refs] + files + [str(root / "probes/gb/Compat.cs"), str(root / "probes/gb/Player.cs"), str(root / "probes/sms/Backend.cs"), "-define:SMS_PLAYER"], check=True)
subprocess.run(["python3", str(root / "scripts/runtime-dependencies.py"), str(executable), str(bcl), "--copy-to", str(out / "package-bcl")], check=True)
for name in ["System.ValueTuple.dll", "System.Memory.dll", "System.Runtime.dll", "System.Runtime.Extensions.dll", "System.Runtime.InteropServices.dll", "System.Runtime.CompilerServices.Unsafe.dll", "System.Buffers.dll", "System.Numerics.Vectors.dll"]:
    shutil.copy2(bcl / name, host_dir / name)
env = dict(os.environ, EUTHERDRIVE_RUNTIME_PROBE_HOST="1", MONO_PATH=str(host_dir))
logs = []
for i, rom in enumerate(input_roms() if player else [None]):
    if player:
        capture = out / f"rom{i:02d}"
        capture.mkdir(exist_ok=True)
        env.update(ED_GB_ROM=str(rom), ED_GB_CAPTURE=str(capture))
    result = subprocess.run(["mono", str(executable)], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    logs.append(result.stdout)
    (out / "host-validation.log").write_text("\n".join(logs))
    print(result.stdout)
    if result.returncode or "RESULT PASS\n" not in result.stdout:
        raise SystemExit("GB desktop validation failed")

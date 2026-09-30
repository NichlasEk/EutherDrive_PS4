#!/usr/bin/env python3
"""Benchmark the built PS4 managed player on desktop Mono, not shadPS4."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('roms', nargs='+', type=Path)
parser.add_argument('--host', type=Path, default=ROOT/'build/console-player/desktop-host')
parser.add_argument('--repeats', type=int, default=3)
parser.add_argument('--warmup', type=int, default=300)
parser.add_argument('--frames', type=int, default=900)
parser.add_argument('--mono-option', action='append', default=[], help='e.g. --mono-option=--optimize=all')
args = parser.parse_args()
if args.repeats < 1 or args.warmup < 300 or args.frames < 1:
    parser.error('positive repeats/frames and >=300 warmup frames required')
host = args.host.resolve()
for path in [host/'main.exe', *args.roms]:
    if not path.is_file():
        parser.error(f'Missing file: {path}; build the console player first')
out = ROOT/'build/benchmarks'/time.strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True, exist_ok=False)
sdk = subprocess.check_output(['dotnet','--list-sdks'], text=True).splitlines()[-1]
version, sdkroot = re.match(r'(\S+) \[(.+)\]', sdk).groups()
refs = [Path('/usr/lib/mono/4.5-api')/n for n in ['mscorlib.dll','System.dll','System.Core.dll']]
refs += [host/n for n in ['main.exe','System.Memory.dll','System.Runtime.dll','System.Runtime.InteropServices.dll']]
exe = out/'benchmark.exe'
subprocess.run(['dotnet',str(Path(sdkroot)/version/'Roslyn/bincore/csc.dll'),
    '-nologo','-nostdlib+','-langversion:latest','-optimize+','-target:exe',
    '-out:'+str(exe), *['-r:'+str(p) for p in refs],
    str(ROOT/'probes/consoles/Benchmark.cs')], check=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
manifest = dict(kind='desktop Mono; no PS4 native runtime/GPU/audio queue',
    mono=subprocess.check_output(['mono','--version'], text=True),
    harness_sha256=sha(ROOT/'probes/consoles/Benchmark.cs'),
    mono_options=args.mono_option,
    environment={k:v for k,v in os.environ.items() if k.startswith('EUTHERDRIVE_') or k in {'MONO_ENV_OPTIONS','MONO_THREADS_SUSPEND'}},
    cpu=next((l for l in Path('/proc/cpuinfo').read_text().splitlines() if l.startswith('model name')), ''),
    assemblies={p.name:sha(p) for p in sorted(host.iterdir()) if p.suffix in {'.dll','.exe'}},
    warmup=args.warmup, frames=args.frames, runs=[])
print(manifest['kind'], flush=True)
for rom in args.roms:
    signatures = []
    for repeat in range(args.repeats):
        run_dir = out/f'run-{len(manifest["runs"]):02d}'
        run_dir.mkdir()
        env = dict(os.environ, EUTHERDRIVE_RUNTIME_PROBE_HOST='1', ED_GB_CAPTURE=str(run_dir),
                   EUTHERDRIVE_TRACE_CONSOLE='0', MONO_PATH=str(host))
        load_before=os.getloadavg()
        run = subprocess.run(['mono',*args.mono_option,str(exe),str(rom.resolve()),str(args.warmup),str(args.frames)],
            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        (run_dir/'run.log').write_text(run.stdout)
        lines = [l for l in run.stdout.splitlines() if l.startswith(('BENCH ', 'PROFILE '))]
        print(f'{rom.name} repeat {repeat+1}\n'+'\n'.join(lines), flush=True)
        if run.returncode or not any(l.startswith('BENCH ') for l in lines):
            raise SystemExit(f'Benchmark failed: {run_dir}/run.log')
        signature = re.search(r'video_hash=(\w+) audio_hash=(\w+) samples=(\d+) nonzero=(\d+)', lines[0]).groups()
        signatures.append(signature)
        manifest['runs'].append(dict(rom=str(rom.resolve()), rom_sha256=sha(rom), repeat=repeat+1,
                                    load_average_before=load_before, results=lines))
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if len(set(signatures)) != 1:
        raise SystemExit(f'Non-deterministic output between repeats for {rom}; see {out}')
print('PASS repeated output hashes/sample counts match; evidence:',out)

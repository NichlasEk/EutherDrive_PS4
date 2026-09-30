#!/usr/bin/env python3
"""Compare candidate SMS reads with the retained original mapper, including writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('host', type=Path, help='candidate desktop-host directory')
args = parser.parse_args()
host = args.host.resolve()
out = ROOT/'build/sms-read-tests'/time.strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True)
exe = out/'read-equivalence.exe'
subprocess.run(['mcs', '-out:'+str(exe), str(ROOT/'probes/consoles/SmsReadEquivalence.cs')], check=True)
for diagnostics in ('0', '1'):
    env = dict(os.environ, MONO_PATH=str(host), EUTHERDRIVE_RUNTIME_PROBE_HOST='1',
               ED_GB_CAPTURE=str(out), EUTHERDRIVE_TRACE_CONSOLE='0',
               EUTHERDRIVE_PS4_SMS_DIAGNOSTICS=diagnostics)
    run = subprocess.run(['mono', str(exe), str(host/'main.exe')], env=env,
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    (out/('diagnostics-'+diagnostics+'.log')).write_text(run.stdout)
    if run.returncode or 'PASS 9437184 reads equal original mapper' not in run.stdout:
        raise SystemExit('FAIL: '+str(out))
(out/'result.json').write_text(json.dumps(dict(passed=True,
    player_sha256=hashlib.sha256((host/'main.exe').read_bytes()).hexdigest(),
    comparisons_per_mode=9437184, diagnostics_modes=[False, True]), indent=2)+'\n')
print('PASS fast and diagnostic paths; evidence:', out)

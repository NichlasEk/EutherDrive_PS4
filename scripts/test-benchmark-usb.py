#!/usr/bin/env python3
"""Run upstream storage/export failure tests against our renamed USB adapter."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
UP=ROOT.parent/'ut99-orbis'
OUT=ROOT/'build/benchmark-tests';OUT.mkdir(parents=True,exist_ok=True)
for name in ['storage','log-export']:
    s=(UP/f'tests/ps4/{name}.cpp').read_text().replace('../../platform/ps4/storage.h','probes/benchmark/usb/storage.h').replace('platform/ps4/log_export.h','probes/benchmark/usb/log_export.h').replace('UT99-logs','EutherDriveBench').replace('ut99_logs','eutherbench_logs')
    test=OUT/(name+'.cpp');test.write_text(s)
    source=ROOT/'probes/benchmark/usb'/('storage.cpp' if name=='storage' else 'log_export.cpp')
    binary=OUT/(name+'-test')
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-g','-I'+str(ROOT),'-I'+str(UP/'tests/ps4/include'),'-I'+str(UP/'build/ps4-usb'),str(source),str(test),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS storage/export tests; mocked kernel, not a physical USB claim')

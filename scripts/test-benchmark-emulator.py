#!/usr/bin/env python3
"""Functional suite/USB export check; generated host skips physical credentials."""
import os,shutil,signal,subprocess,time,json,hashlib,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=(ROOT/'build/hardware-benchmark/current').resolve()
OUT=ROOT/'build/benchmark-tests'/time.strftime('emulator-%Y%m%d-%H%M%S');OUT.mkdir(parents=True)
s=(BASE/'host.c').read_text();assert s.count('credential_probe(run_mono);')==1
s=s.replace('credential_probe(run_mono);','int okay=check_jit() && run_mono();\n    report(okay ? "RESULT PASS EMULATOR SUITE" : "RESULT FAIL EMULATOR SUITE");')
(OUT/'host.c').write_text(s)
b=(BASE/'build.sh').read_text().replace('output='+str(BASE),'output='+str(OUT));(OUT/'build.sh').write_text(b)
env=dict(os.environ,ED_CONSOLE_PLAYER='1',ED_VULKAN_PLAYER='1',ED_JBC_DIR=str(ROOT.parent/'ut99-orbis/build/ps4-usb'),SHADPS4_EXPERIMENTAL_MONO='1')
with (OUT/'build.log').open('w') as log:subprocess.run(['sh',str(OUT/'build.sh')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
stage=OUT/'stage';shutil.copytree(BASE/'stage',stage,ignore=shutil.ignore_patterns('*.pkg','pkg.gp4'))
shutil.copy2(OUT/'eboot.bin',stage/'eboot.bin')
usb=OUT/'usb';usb.mkdir();profile=OUT/'profile';profile.mkdir()
env.update(XDG_DATA_HOME=str(profile),XDG_CONFIG_HOME=str(OUT/'config'),XDG_CACHE_HOME=str(OUT/'cache'),SDL_VIDEODRIVER='x11',PATH=str(ROOT/'build/runtime-probe/fake-bin')+':'+env['PATH'])
guest=profile/'shadPS4/data/eutherdrive-bench/native-probe.log'
command=['xvfb-run','-a',str(ROOT/'build/shadps4-dev/shadps4'),'--fullscreen','false',str(stage/'eboot.bin'),'--mount',str(usb),'/mnt/usb0']
print('Evidence:',OUT,flush=True)
with (OUT/'emulator.log').open('w') as log:
 p=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 deadline=time.monotonic()+1200
 completed=False
 while p.poll() is None and time.monotonic()<deadline:
  text=guest.read_text(errors='replace') if guest.exists() else ''
  if 'RESULT PASS EMULATOR SUITE' in text or 'RESULT FAIL EMULATOR SUITE' in text:
   # Allow the post-cleanup USB export to finish before stopping the process.
   if 'Probe stopped.' in text and 'USB snapshot' in text.split('Probe stopped.',1)[1]:
    completed=True;break
  time.sleep(1)
 if p.poll() is None:
  os.killpg(p.pid,signal.SIGTERM)
  try:p.wait(timeout=3)
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
# Kill any surviving emulator children even if xvfb-run already exited.
try:os.killpg(p.pid,signal.SIGKILL)
except ProcessLookupError:pass
text=guest.read_text(errors='replace') if guest.exists() else ''
assert completed and 'RESULT PASS EMULATOR SUITE' in text, 'Suite failed; inspect '+str(OUT)
local=profile/'shadPS4/data/eutherdrive-bench/run-0001'
exports=sorted((usb/'EutherDriveBench').glob('run-*'));assert exports
last=exports[-1]
assert 'FAILED' not in (last/'export.txt').read_text()
assert (local/'complete.txt').read_text()=='cases=10 errors=0 reference_differences=0\n'
for file in local.iterdir():
 if file.is_file():assert file.read_bytes()==(last/file.name).read_bytes(),file.name
assert 'RESULT PASS EMULATOR SUITE' in (last/'native-probe.log').read_text()
assert not list(last.glob('*.partial'))
(OUT/'validation.json').write_text(json.dumps(dict(passed=True,emulator_sha256=hashlib.sha256((ROOT/'build/shadps4-dev/shadps4').read_bytes()).hexdigest(),managed_sha256=hashlib.sha256((stage/'benchmark.exe').read_bytes()).hexdigest(),generated_host_sha256=hashlib.sha256((OUT/'host.c').read_bytes()).hexdigest(),local=str(local),usb=str(last),export_count=len(exports),package_sha256=hashlib.sha256(next((BASE/'stage').glob('*.pkg')).read_bytes()).hexdigest()),indent=2)+'\n')
print('PASS all ten cases match reference; final USB files equal internal files:',last)

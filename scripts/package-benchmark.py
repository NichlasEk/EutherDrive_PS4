#!/usr/bin/env python3
"""Build an isolated physical PS4 benchmark package from the validated player."""
import hashlib,json,os,re,shlex,shutil,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SDK=Path(os.environ.get('OO_PS4_TOOLCHAIN','/opt/openorbis/OpenOrbis/PS4Toolchain'))
BASE=(ROOT/'build/runtime-probe/current-pkgroot').resolve()
OUT=ROOT/'build/hardware-benchmark'/time.strftime('%Y%m%d-%H%M%S')
OUT.mkdir(parents=True)
STAGE=OUT/'stage'; STAGE.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
player_sha='ea9c9340a4c7ae20bc30da58216ef4b042d429fcdd2c35dcb23fc70b0ea333d9'
assert sha(BASE/'main.exe')==player_sha, 'Revalidate the baseline player before building a suite'
for p in BASE.iterdir():
    if p.suffix=='.pkg' or p.name=='pkg.gp4':continue
    if p.is_dir():shutil.copytree(p,STAGE/p.name)
    else:shutil.copy2(p,STAGE/p.name)
cases=[('black-belt',0,'sms',1200,'c5accc40','d57eecf7',441000,440010),
       ('alex-kidd',1,'sms',1200,'0af48aea','387bb807',441000,417560),
       ('sonic1',2,'md',1200,'90b13094','45502552',441000,440636),
       ('streets2',3,'md',1800,'b909bbda','af9b43c7',441000,440325),
       ('zelda',4,'sfc',3000,'f513d40d','eadfe306',440278,439744)]
rows=[]
for name,i,ext,warmup,v,a,n,z in cases:
    rom=ROOT/f'build/console-player/rom{i:02d}/game.{ext}';inp=ROOT/f'probes/consoles/inputs/{name}.txt'
    romname=f'bench-{i}.{ext}';inputname=f'bench-{i}.txt'
    shutil.copy2(rom,STAGE/romname);shutil.copy2(inp,STAGE/inputname)
    rows.append('\t'.join(map(str,[name,romname,inputname,warmup,sha(rom),sha(inp),v,a,n,z])))
(STAGE/'suite.tsv').write_text('\n'.join(rows)+'\n')
source=(ROOT/'probes/consoles/Benchmark.cs').read_text().replace('static void Main(string[] args)','public static void Run(string[] args)').replace('Console.WriteLine(', 'MonoBenchmark.Entry.Record(')
source=source.replace('if (frame==warmup+1) {', 'if (frame<=warmup && frame%300==0) MonoBenchmark.Entry.Record("WARMUP frame={0}/{1}",frame,warmup); if (frame==warmup+1) {')
(OUT/'Benchmark.cs').write_text(source)
version,sdkroot=re.match(r'(\S+) \[(.+)\]',subprocess.check_output(['dotnet','--list-sdks'],text=True).splitlines()[-1]).groups()
refs=[Path('/usr/lib/mono/4.5-api')/n for n in ['mscorlib.dll','System.dll','System.Core.dll']]
refs += [STAGE/'main.exe']+[STAGE/'mono/4.5'/n for n in ['System.Memory.dll','System.Runtime.dll','System.Runtime.InteropServices.dll']]
subprocess.run(['dotnet',str(Path(sdkroot)/version/'Roslyn/bincore/csc.dll'),'-nologo','-nostdlib+','-langversion:latest','-optimize+','-target:exe','-main:MonoBenchmark.Desktop','-out:'+str(STAGE/'benchmark.exe'),*['-r:'+str(p) for p in refs],str(OUT/'Benchmark.cs'),str(ROOT/'probes/benchmark/Suite.cs')],check=True)

def replace_once(s,old,new):
    assert s.count(old)==1, 'Review native source transformation: '+old
    return s.replace(old,new)
s=(ROOT/'probes/runtime/host.c').read_text()
s=replace_once(s,'static int logfile =','#undef PROBE_TITLE\n#define PROBE_TITLE "EutherDrive Auto Benchmark 0.01"\nextern int benchmark_prepare_usb(void);\nextern int benchmark_export(const char*);\nextern void benchmark_finish(void);\nextern void benchmark_release_usb(void);\nstatic int logfile =')
s=replace_once(s,'static void init_video(void) {','void benchmark_note(const char *text) { report("%s",text); }\n\nstatic void init_video(void) {')
s=s.replace('/data/eutherdrive-ps4','/data/eutherdrive-bench')
s=replace_once(s,'class_from_name(image, "Orbis", "Program")','class_from_name(image, "MonoBenchmark", "Entry")')
s=replace_once(s,'add_call("Orbis.Program::NativeReport", (const void *)managed_report);','add_call("MonoBenchmark.Entry::NativeReport", (const void *)managed_report);\n    add_call("MonoBenchmark.Entry::NativeExport", (const void *)benchmark_export);')
assert s.count('"/app0/main.exe"')==2
s=s.replace('"/app0/main.exe"','"/app0/benchmark.exe"')
s=replace_once(s,'    run();\n#ifdef VULKAN_PLAYER','    if (benchmark_prepare_usb() == 0) run();\n    else report("FAIL USB credential restoration; benchmark not started");\n#ifdef VULKAN_PLAYER')
s=replace_once(s,'    if (logfile >= 0) sceKernelFsync(logfile);\n    for (;;)', '    if (logfile >= 0) sceKernelFsync(logfile);\n    benchmark_finish();\n    benchmark_release_usb();\n    for (;;)')
s=replace_once(s,'static int run_mono(void) {','#include "'+str(ROOT/'probes/benchmark/mono_cases.h')+'"\nstatic int run_mono(void) {')
s=replace_once(s,'report("05b Mono domain initialized");','report("05b Mono domain initialized");\n    if (!benchmark_case_init(mono)) { jit_cleanup(domain); return 0; }')
s=replace_once(s,'add_call("MonoBenchmark.Entry::NativeExport", (const void *)benchmark_export);','add_call("MonoBenchmark.Entry::NativeExport", (const void *)benchmark_export);\n    add_call("MonoBenchmark.Entry::NativeRunCase", (const void *)benchmark_run_case);\n    add_call("MonoBenchmark.Entry::NativeCaseConfig", (const void *)benchmark_case_config);')
(OUT/'host.c').write_text(s)
subprocess.run(['python3',str(ROOT.parent/'ut99-orbis/scripts/build-ps4-usb.py')],check=True)
jbc=ROOT.parent/'ut99-orbis/build/ps4-usb'
objects=[]
for src in [ROOT/'probes/benchmark/usb_bridge.cpp',ROOT/'probes/benchmark/usb/storage.cpp',ROOT/'probes/benchmark/usb/log_export.cpp']:
    obj=OUT/(src.stem+'.o');objects.append(obj)
    subprocess.run(['clang++','--target=x86_64-pc-freebsd12-elf','-std=c++17','-fPIC','-O2','-Wall','-Wextra','-Werror','-nostdinc++','-isystem',str(SDK/'include/c++/v1'),'-isysroot',str(SDK),'-isystem',str(SDK/'include'),'-I'+str(jbc),'-c',str(src),'-o',str(obj)],check=True)
b=(ROOT/'scripts/build-runtime-host.sh').read_text()
b=replace_once(b,'project_dir=$(CDPATH=\'\' cd -- "$(dirname -- "$0")/.." && pwd)','project_dir='+shlex.quote(str(ROOT)))
b=replace_once(b,'output=$project_dir/build/runtime-probe','output='+shlex.quote(str(OUT)))
b=replace_once(b,'"$project_dir/probes/runtime/host.c"','"$output/host.c"')
b=replace_once(b,'-DMONO_CREDENTIAL_PROBE $core_define','-I"$project_dir/probes/runtime" -DMONO_CREDENTIAL_PROBE $core_define')
b=replace_once(b,'"$output/host.o" "$jbc/libut99-jbc.a"','"$output/host.o" '+' '.join(shlex.quote(str(p)) for p in objects)+' "$jbc/libut99-jbc.a"')
(OUT/'build.sh').write_text(b)
env=dict(os.environ,ED_CONSOLE_PLAYER='1',ED_VULKAN_PLAYER='1',ED_JBC_DIR=str(jbc))
with (OUT/'native-build.log').open('w') as log:subprocess.run(['sh',str(OUT/'build.sh')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
shutil.copy2(OUT/'eboot.bin',STAGE/'eboot.bin')
info=dict(version='0.01',kind='physical PS4, timed core/audio generation/framebuffer; untimed status screen',git_base=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),player_sha256=sha(STAGE/'main.exe'),mono_sha256=sha(STAGE/'sce_module/libmonosgen-2.0.prx'),suite_sha256=sha(STAGE/'suite.tsv'),sources={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'probes/consoles/Benchmark.cs',ROOT/'probes/benchmark/Suite.cs',ROOT/'probes/benchmark/usb_bridge.cpp',ROOT/'probes/benchmark/mono_cases.h',ROOT/'scripts/package-benchmark.py']},files={str(p.relative_to(STAGE)):sha(p) for p in STAGE.rglob('*') if p.is_file() and p.name!='param.sfo'})
(STAGE/'build-info.json').write_text(json.dumps(info,indent=2)+'\n')
shutil.copy2(ROOT/'probes/benchmark/usb/LICENSE',STAGE/'USB-LICENSE.txt')
pkgtool=SDK/'bin/linux/PkgTool.Core';sfo=STAGE/'sce_sys/param.sfo'
content='IV0000-EDBM00001_00-EUTHERBENCHMARK1'
for key,value,size in [('CONTENT_ID',content,48),('TITLE_ID','EDBM00001',12),('TITLE','EutherDrive Auto Benchmark',128),('APP_VER','0.01',8),('VERSION','0.01',8)]:
    subprocess.run([str(pkgtool),'sfo_setentry',str(sfo),key,'--type','Utf8','--maxsize',str(size),'--value',value],check=True,stdout=subprocess.DEVNULL)
files=' '.join(str(p.relative_to(STAGE)) for p in STAGE.rglob('*') if p.is_file())
subprocess.run([str(SDK/'bin/linux/create-gp4'),'-out','pkg.gp4','--content-id='+content,'--files',files],cwd=STAGE,check=True,stdout=subprocess.DEVNULL)
p=STAGE/'pkg.gp4';s=p.read_text();s=re.sub(r'<dir targ_name="assets">.*?</dir>','',s,flags=re.S);s=s.replace('<dir targ_name="sce_sys">','<dir targ_name="mono"><dir targ_name="4.5" /></dir><dir targ_name="sce_sys">');p.write_text(s)
with (OUT/'package.log').open('w') as log:
    subprocess.run([str(pkgtool),'pkg_build','pkg.gp4','.'],cwd=STAGE,check=True,stdout=log,stderr=subprocess.STDOUT)
    subprocess.run([str(pkgtool),'pkg_validate','--verbose',str(STAGE/(content+'.pkg'))],check=True,stdout=log,stderr=subprocess.STDOUT)
dist=ROOT/'dist/eutherdrive-auto-benchmark-0.01.pkg';shutil.copy2(STAGE/(content+'.pkg'),dist)
(dist.with_suffix('.pkg.sha256')).write_text(sha(dist)+'  '+dist.name+'\n')
link=OUT.parent/'current';link.unlink(missing_ok=True);link.symlink_to(OUT)
print('Benchmark stage:',STAGE);print('Validated package:',dist);print('SHA256:',sha(dist))

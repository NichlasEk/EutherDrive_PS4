#!/usr/bin/env python3
"""Isolated shadPS4 Mono bring-up; never packages or changes the PS4 player.

Skips hardware credentials ONLY in a generated diagnostic host. Optional
--jit-preflight verifies the emulator JIT APIs with executable shared aliases.
The probe itself does not implement those APIs or claim full Mono support.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sys-modules', type=Path, help='directory containing your dumped libSceLibcInternal.sprx')
parser.add_argument('--timeout', type=int, default=25)
parser.add_argument('--jit-preflight', action='store_true', help='test direct HLE JIT imports, aliasing and execution before Mono')
args = parser.parse_args()
if not 1 <= args.timeout <= 300:
    parser.error('timeout must be 1..300 seconds')
stage_source = (ROOT/'build/runtime-probe/current-pkgroot').resolve()
if not (stage_source/'main.exe').is_file():
    parser.error('Build the normal console package first')
module = args.sys_modules.resolve()/'libSceLibcInternal.sprx' if args.sys_modules else None
if module and not module.is_file():
    parser.error(f'Missing module: {module}')
out = ROOT/'build/shadps4-mono'/time.strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True, exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Source layout changed; review diagnostic transformation: '+old[:90])
    return text.replace(old, new)

source = (ROOT/'probes/runtime/host.c').read_text()
source = replace_once(source, 'static int logfile =',
    '#undef PROBE_TITLE\n#define PROBE_TITLE "EMULATOR ONLY Mono compatibility probe"\n\nstatic int logfile =')
source = replace_once(source,
    'if (kernel_handle < 0) { report("FAIL existing libkernel not found"); return; }',
    'if (kernel_handle < 0) report("DIAGNOSTIC: no guest libkernel module; native fallback unavailable");')
start = source.index('#ifdef NATIVE_CREDENTIAL_PROBE\n    credential_probe(NULL);')
end = source.index('\n#endif', start)+len('\n#endif')
source = source[:start]+'''report("EMULATOR ONLY: hardware preflight skipped; no credential changes");
    report(run_mono() ? "RESULT PASS EMULATOR MONO=managed PREFLIGHT=skipped RIGHTS=unchanged"
                      : "RESULT FAIL EMULATOR MONO");'''+source[end:]
source = replace_once(source, '02 kernel found; loading Mono dependencies', '02 loading Mono dependencies (emulator diagnostic)')
source = replace_once(source, '05 entering mono_jit_init; native JIT preflight passed',
                      '05 entering mono_jit_init; hardware JIT preflight SKIPPED')
if args.jit_preflight:
    source = replace_once(source, 'int (*create)(int, size_t, int, int *);',
                          'int (*create)(const char *, size_t, int, int *);')
    source = replace_once(source,
        'if (!resolve(kernel_handle, "sceKernelJitCreateSharedMemory", (void **)&create) ||\n'
        '        !resolve(kernel_handle, "sceKernelJitCreateAliasOfSharedMemory", (void **)&alias)) return 0;',
        'create = (void *)sceKernelJitCreateSharedMemory;\n'
        '    alias = (void *)sceKernelJitCreateAliasOfSharedMemory;')
    source = replace_once(source,
        'rc = sceKernelMmap(NULL, size, PROT_READ | PROT_EXEC, MAP_SHARED, executable, 0, &rx);',
        'int (*map_jit)(int, int, void **) = (void *)sceKernelJitMapSharedMemory;\n'
        '    rc = map_jit(executable, PROT_READ | PROT_EXEC, &rx);')
    source = replace_once(source, 'mapped_rw = 1;', '''mapped_rw = 1;
    int denied = -1;
    if (alias(writable, 7, &denied) == 0 || denied != -1) {
        report("FAIL JIT alias allowed permission escalation"); goto done;
    }
    if (alias(-1, 3, &denied) == 0 || denied != -1) {
        report("FAIL JIT alias accepted invalid descriptor"); goto done;
    }
    void *forbidden = NULL;
    if (map_jit(writable, 5, &forbidden) == 0 || forbidden != NULL) {
        report("FAIL JIT map allowed permission escalation"); goto done;
    }
    if (sceKernelClose(writable) != 0 || sceKernelClose(executable) != 0) {
        report("FAIL JIT descriptor close before execution"); goto done;
    }
    writable = executable = -1;
    report("PASS JIT invalid descriptors/protections rejected; handles closed before execution");''')
    source = replace_once(source,
        'report("EMULATOR ONLY: hardware preflight skipped; no credential changes");',
        'report("EMULATOR ONLY: direct HLE JIT preflight; no credential changes");\n'
        '    if (!check_jit()) return;')
    source = source.replace('PREFLIGHT=skipped', 'PREFLIGHT=JIT-verified')
    source = source.replace('hardware JIT preflight SKIPPED', 'emulator JIT alias/execution verified')
(out/'host.c').write_text(source)
build = (ROOT/'scripts/build-runtime-host.sh').read_text()
build = replace_once(build, 'project_dir=$(CDPATH=\'\' cd -- "$(dirname -- "$0")/.." && pwd)',
                     'project_dir='+shlex.quote(str(ROOT)))
build = replace_once(build, 'output=$project_dir/build/runtime-probe', 'output='+shlex.quote(str(out)))
build = replace_once(build, '-DMONO_CREDENTIAL_PROBE $core_define', '-I"$project_dir/probes/runtime" $core_define')
build = replace_once(build, '"$project_dir/probes/runtime/host.c"', '"$output/host.c"')
if args.jit_preflight:
    # SDK headers declare these APIs but this SDK's import library omits them.
    # Generate link-only import definitions. No stub code enters the package;
    # shadPS4 must resolve the resulting NIDs to actual HLE implementations.
    toolchain=Path(os.environ.get('OO_PS4_TOOLCHAIN','/opt/openorbis/OpenOrbis/PS4Toolchain'))
    symbols={}
    for line in subprocess.check_output(['readelf','--dyn-syms','--wide',str(toolchain/'lib/libkernel.so')],text=True).splitlines():
        cols=line.split()
        if len(cols)==8 and cols[3] in {'FUNC','OBJECT'} and cols[4] in {'GLOBAL','WEAK'} and cols[6]!='UND':
            name=cols[7]
            if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*',name):
                raise RuntimeError('Unexpected SDK import name: '+name)
            symbols[name]=cols[3]
    if not symbols:raise RuntimeError('No SDK libkernel exports found')
    for name in ['sceKernelJitCreateSharedMemory','sceKernelJitCreateAliasOfSharedMemory','sceKernelJitMapSharedMemory']:
        symbols[name]='FUNC'
    imports=out/'jit-imports';imports.mkdir()
    asm='\n'.join(f'.{"text" if kind=="FUNC" else "data"}\n.globl {name}\n.type {name},@{"function" if kind=="FUNC" else "object"}\n{name}:\n'+('ret' if kind=='FUNC' else '.quad 0') for name,kind in sorted(symbols.items()))+'\n'
    (imports/'kernel.S').write_text(asm)
    subprocess.run(['clang','--target=x86_64-pc-freebsd12-elf','-fPIC','-c',str(imports/'kernel.S'),'-o',str(imports/'kernel.o')],check=True)
    subprocess.run(['ld.lld','-shared','-o',str(imports/'libkernel.so'),str(imports/'kernel.o')],check=True)
    # The SELF converter also reads imports from OO_PS4_TOOLCHAIN/lib.
    # Use an isolated SDK overlay so both linker and converter see the same exports.
    overlay=out/'sdk';overlay.mkdir();(overlay/'lib').mkdir()
    for entry in toolchain.iterdir():
        if entry.name!='lib':(overlay/entry.name).symlink_to(entry)
    for entry in (toolchain/'lib').iterdir():
        (overlay/'lib'/entry.name).symlink_to(imports/'libkernel.so' if entry.name=='libkernel.so' else entry)
    build=replace_once(build,'toolchain=${OO_PS4_TOOLCHAIN:-/opt/openorbis/OpenOrbis/PS4Toolchain}',
                       'toolchain='+shlex.quote(str(overlay)))
(out/'build.sh').write_text(build)
env = dict(os.environ, ED_CONSOLE_PLAYER='1', ED_VULKAN_PLAYER='1',
           ED_JBC_DIR=os.environ.get('ED_JBC_DIR',str(ROOT.parent/'ut99-orbis/build/ps4-usb')))
print('Diagnostic directory:',out,flush=True)
with (out/'build.log').open('w') as log:
    subprocess.run(['sh',str(out/'build.sh')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
stage=out/'stage'; stage.mkdir()
for p in stage_source.iterdir():
    if p.name in {'main.exe','eboot.bin'} or p.suffix=='.pkg':
        continue
    (stage/p.name).symlink_to(p)
shutil.copy2(out/'eboot.bin',stage/'eboot.bin')
reference=Path(subprocess.check_output([str(ROOT/'scripts/prepare-mono-reference.sh')],text=True).strip())
bcl=reference/'mono/4.5'
subprocess.run(['mcs','-sdk:4.5','-platform:x64','-optimize+', '-out:'+str(stage/'main.exe'),
    *['-r:'+str(bcl/n) for n in ['System.Memory.dll','System.Runtime.dll',
      'System.Runtime.CompilerServices.Unsafe.dll','System.Buffers.dll','System.Numerics.Vectors.dll']],
    str(ROOT/'probes/runtime/Program.cs')],check=True)
profile=out/'profile'
modules=profile/'shadPS4/sys_modules'; modules.mkdir(parents=True)
if module:
    (modules/module.name).symlink_to(module)
fake_bin=ROOT/'build/runtime-probe/fake-bin'; fake_bin.mkdir(parents=True,exist_ok=True)
wrapper=fake_bin/'zenity'
if not wrapper.exists():
    wrapper.symlink_to(ROOT/'scripts/zenity-noninteractive-wrapper.sh')
emulator=Path(os.environ.get('SHADPS4',str(ROOT.parent/'ScummVM-PS4/.tools/shadps4/Shadps4-sdl.AppImage'))).resolve()
env.update(XDG_DATA_HOME=str(profile),XDG_CONFIG_HOME=str(out/'config'),
           XDG_CACHE_HOME=str(out/'cache'),SDL_VIDEODRIVER='x11',PATH=str(fake_bin)+':'+env['PATH'])
command=['timeout','--kill-after=3s',str(args.timeout)+'s','xvfb-run','-a',str(emulator),
         '--fullscreen','false',str(stage/'eboot.bin')]
# No core dumps from an expected unsupported-runtime crash.
import resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with (out/'emulator.log').open('w') as log:
    run=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
log=(out/'emulator.log').read_text(errors='replace')
guest=profile/'shadPS4/data/eutherdrive-ps4/native-probe.log'
native=guest.read_text(errors='replace') if guest.exists() else ''
managed_log=profile/'shadPS4/data/eutherdrive-ps4/runtime-probe.log'
managed=managed_log.read_text(errors='replace') if managed_log.exists() else ''
preflight='JIT-verified' if args.jit_preflight else 'skipped'
passed=(f'RESULT PASS EMULATOR MONO=managed PREFLIGHT={preflight} RIGHTS=unchanged' in native
        and 'RESULT PASS\n' in managed)
result=dict(emulator_exit=run.returncode,managed_pass=passed,
    jit_preflight_requested=args.jit_preflight,
    jit_execution_pass='PASS native JIT returned 42' in native,
    experimental_mono=env.get('SHADPS4_EXPERIMENTAL_MONO','0'),
    first_gpu_frame='VULKAN ready: first GPU frame and flip completed' in native,
    module=str(module) if module else None,module_sha256=sha(module) if module else None,
    emulator_sha256=sha(emulator),mono_sha256=sha(stage/'sce_module/libmonosgen-2.0.prx'),
    diagnostic_main_sha256=sha(stage/'main.exe'),
    called_stubs=sorted(set(re.findall(r'Stub: (\S+) \(nid:',log))),
    unresolved=sorted(set(re.findall(r'Stub resolved \S+ as (\S+) \(lib: ([^,]+), mod: ([^)]+)\)',log))),
    checkpoints=[l for l in native.splitlines() if l.startswith(('0','FAIL','RESULT','EMULATOR','DIAGNOSTIC','LOAD'))])
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='unresolved'},indent=2))
print('Full evidence:',out,flush=True)
raise SystemExit(0 if passed else 3)

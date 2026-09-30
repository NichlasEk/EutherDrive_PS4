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
import signal
import shutil
import subprocess
import time
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sys-modules', type=Path, help='directory containing your dumped libSceLibcInternal.sprx')
parser.add_argument('--timeout', type=int, default=25)
parser.add_argument('--benchmark-rom', type=Path, help='benchmark the packaged managed player with a local ROM')
parser.add_argument('--benchmark-host', type=Path, help='isolated candidate desktop-host directory; native player staging remains unchanged')
parser.add_argument('--benchmark-warmup', type=int, default=300)
parser.add_argument('--benchmark-input', type=Path, help='frame ranges and button masks for reproducible input')
parser.add_argument('--benchmark-frames', type=int, default=300)
parser.add_argument('--stress', action='store_true', help='also test concurrent GC and directory enumeration')
parser.add_argument('--jit-preflight', action='store_true', help='test direct HLE JIT imports, aliasing and execution before Mono')
args = parser.parse_args()
if args.benchmark_host and not args.benchmark_rom:
    parser.error('--benchmark-host requires --benchmark-rom')
benchmark_host = args.benchmark_host.resolve() if args.benchmark_host else ROOT/'build/console-player/desktop-host'
if args.benchmark_rom:
    args.benchmark_rom = args.benchmark_rom.resolve()
    if not args.benchmark_rom.is_file() or args.benchmark_frames < 1 or args.benchmark_warmup < 300 or args.stress:
        parser.error('benchmark needs an existing ROM, >=300 warmup frames, positive frame count, and no --stress')

if args.benchmark_input:
    args.benchmark_input = args.benchmark_input.resolve()
    if not args.benchmark_rom or not args.benchmark_input.is_file():
        parser.error('benchmark input requires a ROM and an existing replay file')

if not 1 <= args.timeout <= 300:
    parser.error('timeout must be 1..300 seconds')
stage_source = (ROOT/'build/runtime-probe/current-pkgroot').resolve()
if not (stage_source/'main.exe').is_file():
    parser.error('Build the normal console package first')
module = args.sys_modules.resolve()/'libSceLibcInternal.sprx' if args.sys_modules else None
if module and not module.is_file():
    parser.error(f'Missing module: {module}')
evidence_root = ROOT/'build/shadps4-mono'
evidence_root.mkdir(parents=True, exist_ok=True)
out = Path(tempfile.mkdtemp(prefix=time.strftime('%Y%m%d-%H%M%S-'), dir=evidence_root))
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
    source = replace_once(source, 'static int check_jit(void) {',
        '#include "shadps4-preflight.h"\n\nstatic int check_jit(void) {')
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
        '    if (!check_mono_signals() || !check_jit()) return;')
    source = replace_once(source, 'report("PASS native JIT returned 42");', r'''report("PASS native JIT returned 42");
    ((unsigned char *)rw)[1] = 43;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (((int (*)(void))rx)() != 43) { report("FAIL JIT rewrite"); goto done; }
    void *expected_tcb_thread;
    __asm__ volatile ("movq %%fs:0x10,%0" : "=r" (expected_tcb_thread));
    const unsigned char tls_code[] = {0x64,0x48,0x8b,0x04,0x25,0x10,0,0,0,0xc3};
    memcpy(rw, tls_code, sizeof(tls_code));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (!expected_tcb_thread || ((void *(*)(void))rx)() != expected_tcb_thread) {
        report("FAIL JIT TLS differs from ELF TLS"); goto done;
    }
    memcpy(rw, tls_code, sizeof(tls_code));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (((void *(*)(void))rx)() != expected_tcb_thread) {
        report("FAIL JIT rewritten TLS"); goto done;
    }
    memcpy((char *)rw + 31, tls_code, sizeof(tls_code));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    // Enter the already executable page through another method first: a page
    // fault at offset zero must also translate unaligned later method entries.
    if (((void *(*)(void))rx)() != expected_tcb_thread ||
        ((void *(*)(void))((char *)rx + 31))() != expected_tcb_thread) {
        report("FAIL JIT unaligned secondary TLS entry"); goto done;
    }
    memcpy((char *)rw + 4160, tls_code, sizeof(tls_code));
    memcpy((char *)rw + 4092, tls_code, sizeof(tls_code));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (((void *(*)(void))((char *)rx + 4160))() != expected_tcb_thread ||
        ((void *(*)(void))((char *)rx + 4092))() != expected_tcb_thread) {
        report("FAIL JIT cross-page TLS"); goto done;
    }
    report("PASS JIT rewrite, TLS, rewritten TLS and cross-page TLS");''')
    source = source.replace('PREFLIGHT=skipped', 'PREFLIGHT=JIT-verified')
    source = source.replace('hardware JIT preflight SKIPPED', 'emulator JIT alias/execution verified')
if args.benchmark_rom:
    source = replace_once(source, 'class_from_name(image, "Orbis", "Program")',
                          'class_from_name(image, "MonoBenchmark", "Entry")')
    source = replace_once(source, '"Orbis.Program::NativeReport"', '"MonoBenchmark.Entry::NativeReport"')
    if source.count('"/app0/main.exe"') != 2:
        raise RuntimeError('Review benchmark entry path transformation')
    source = source.replace('"/app0/main.exe"', '"/app0/benchmark.exe"')
    source = source.replace('06 loading main.exe', '06 loading benchmark.exe')
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
if args.benchmark_rom:
    shutil.copy2(benchmark_host/'main.exe' if args.benchmark_host else stage_source/'main.exe', stage/'main.exe')
    guest_rom = '/app0/benchmark' + args.benchmark_rom.suffix.lower()
    (stage/Path(guest_rom).name).symlink_to(args.benchmark_rom)
    guest_input = ''
    if args.benchmark_input:
        shutil.copy2(args.benchmark_input, stage/'benchmark-input.txt')
        guest_input = '/app0/benchmark-input.txt'
    benchmark_source = out/'Benchmark.cs'
    benchmark_source_text = (ROOT/'probes/consoles/Benchmark.cs').read_text()
    benchmark_harness_sha256 = hashlib.sha256(benchmark_source_text.encode()).hexdigest()
    benchmark_source.write_text(replace_once(benchmark_source_text,
        '''static void Main(string[] args)''', '''public static void Run(string[] args)''')
        .replace('Bind<Action<string>>(emulator, "LoadRom")(args[0]);',
                 'Console.WriteLine("BENCHPROGRESS loading ROM"); Bind<Action<string>>(emulator, "LoadRom")(args[0]); Console.WriteLine("BENCHPROGRESS ROM loaded");')
        .replace('if (frame==warmup+1) {',
                 'if (frame <= warmup && (frame == 1 || frame % 60 == 0)) Console.WriteLine("BENCHPROGRESS warmup frame=" + frame); if (frame==warmup+1) {')
        .replace('Console.WriteLine(', 'MonoBenchmark.Entry.Log('))
    wrapper_source = out/'BenchmarkEntry.cs'
    wrapper_source.write_text((ROOT/'probes/runtime/BenchmarkEntry.cs').read_text()
        .replace('@ROM@', guest_rom).replace('@FRAMES@', str(args.benchmark_frames))
        .replace('@WARMUP@', str(args.benchmark_warmup)).replace('@INPUT@', guest_input))
    sdk = subprocess.check_output(['dotnet','--list-sdks'], text=True).splitlines()[-1]
    version, sdkroot = re.match(r'(\S+) \[(.+)\]', sdk).groups()
    refs = [Path('/usr/lib/mono/4.5-api')/n for n in ['mscorlib.dll','System.dll','System.Core.dll']]
    refs += [benchmark_host/n for n in
             ['main.exe','System.Memory.dll','System.Runtime.dll','System.Runtime.InteropServices.dll']]
    # The reference and staged production player must be identical.
    if sha(refs[3]) != sha(stage/'main.exe'):
        raise RuntimeError('Desktop reference main.exe differs from the packaged player')
    subprocess.run(['dotnet',str(Path(sdkroot)/version/'Roslyn/bincore/csc.dll'),
        '-nologo','-nostdlib+','-langversion:latest','-optimize+','-target:exe',
        '-out:'+str(stage/'benchmark.exe'), *['-r:'+str(p) for p in refs],
        str(benchmark_source),str(wrapper_source)],check=True)
else:
    managed_source = ROOT/'probes/runtime/Program.cs'
    if args.stress:
        managed_source = out/'Program.cs'
        managed_source.write_text(replace_once((ROOT/'probes/runtime/Program.cs').read_text().replace('exception.GetType().FullName + ": " + exception.Message', 'exception.ToString()'),
            'Run("native-call", TestNativeCall);',
            'Run("math-formatting", MonoEmulatorStress.MathAndFormatting);\n'
            '            Run("gc-threads", MonoEmulatorStress.ConcurrentGc);\n'
            '            Run("directory-io", MonoEmulatorStress.DirectoryIo);\n'
            '            Run("native-call", TestNativeCall);'))
    subprocess.run(['mcs','-sdk:4.5','-platform:x64','-optimize+', '-out:'+str(stage/'main.exe'),
        *['-r:'+str(bcl/n) for n in ['System.Memory.dll','System.Runtime.dll',
          'System.Runtime.CompilerServices.Unsafe.dll','System.Buffers.dll','System.Numerics.Vectors.dll']],
        str(managed_source), *([str(ROOT/'probes/runtime/MonoEmulatorStress.cs')] if args.stress else [])],check=True)
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
guest=profile/'shadPS4/data/eutherdrive-ps4/native-probe.log'
stopped_after_result = False
with (out/'emulator.log').open('w') as log:
    run=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    while run.poll() is None:
        report_text = guest.read_text(errors='replace') if guest.exists() else ''
        if 'RESULT PASS EMULATOR MONO=' in report_text or 'RESULT FAIL EMULATOR MONO' in report_text:
            # Mono cleanup has returned before these final host markers. Stop
            # only this probe's process group instead of idling on its photo UI.
            stopped_after_result = True
            try: os.killpg(run.pid, signal.SIGTERM)
            except ProcessLookupError: pass
            try: run.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(run.pid, signal.SIGKILL)
                run.wait()
            break
        time.sleep(0.2)
log=(out/'emulator.log').read_text(errors='replace')
guest=profile/'shadPS4/data/eutherdrive-ps4/native-probe.log'
native=guest.read_text(errors='replace') if guest.exists() else ''
managed_log=profile/'shadPS4/data/eutherdrive-ps4/runtime-probe.log'
managed=managed_log.read_text(errors='replace') if managed_log.exists() else ''
preflight='JIT-verified' if args.jit_preflight else 'skipped'
benchmark_log=profile/'shadPS4/data/eutherdrive-ps4/benchmark-result.log'
benchmark_lines=[line for line in native.splitlines() if line.startswith(('BENCH system=', 'PROFILE '))]
benchmark_pass=bool(args.benchmark_rom and benchmark_log.exists()
                    and benchmark_log.read_text() == 'RESULT PASS\n'
                    and any('BENCH system=' in line for line in benchmark_lines))
passed=(f'RESULT PASS EMULATOR MONO=managed PREFLIGHT={preflight} RIGHTS=unchanged' in native
        and (benchmark_pass if args.benchmark_rom else 'RESULT PASS\n' in managed))
frame_capture=profile/'shadPS4/data/eutherdrive-ps4/benchmark-last.ppm'
result=dict(benchmark_warmup=args.benchmark_warmup,benchmark_frames=args.benchmark_frames,
    input_sha256=sha(stage/'benchmark-input.txt') if args.benchmark_input else None,
    harness_sha256=benchmark_harness_sha256 if args.benchmark_rom else None,
    frame_capture_sha256=sha(frame_capture) if frame_capture.exists() else None,
    frame_capture=str(frame_capture) if frame_capture.exists() else None,
    kind='shadPS4 PS4 Mono CPU/audio/framebuffer benchmark; no timed GPU presentation' if args.benchmark_rom else 'managed runtime tests',
    benchmark_pass=passed if args.benchmark_rom else False,benchmark_results=benchmark_lines,
    rom_sha256=sha(args.benchmark_rom) if args.benchmark_rom else None,
    player_sha256=sha(stage/'main.exe') if args.benchmark_rom else None,
    stopped_after_result=stopped_after_result,stress_requested=args.stress,emulator_exit=run.returncode,managed_pass=passed and not bool(args.benchmark_rom),
    jit_preflight_requested=args.jit_preflight,
    jit_execution_pass='PASS native JIT returned 42' in native,
    signal_pass='PASS signals: query, mask, delivery, guest arguments, restore' in native,
    jit_tls_pass='PASS JIT rewrite, TLS, rewritten TLS and cross-page TLS' in native,
    guard_heap=env.get('SHADPS4_MONO_GUARD_HEAP','0'),
    experimental_mono=env.get('SHADPS4_EXPERIMENTAL_MONO','0'),
    first_gpu_frame='VULKAN ready: first GPU frame and flip completed' in native,
    module=str(module) if module else None,module_sha256=sha(module) if module else None,
    emulator_sha256=sha(emulator),mono_sha256=sha(stage/'sce_module/libmonosgen-2.0.prx'),
    diagnostic_main_sha256=sha(stage/('benchmark.exe' if args.benchmark_rom else 'main.exe')),
    called_stubs=sorted(set(re.findall(r'Stub: (\S+) \(nid:',log))),
    unresolved=sorted(set(re.findall(r'Stub resolved \S+ as (\S+) \(lib: ([^,]+), mod: ([^)]+)\)',log))),
    checkpoints=[l for l in native.splitlines() if l.startswith(('0','FAIL','RESULT','EMULATOR','DIAGNOSTIC','LOAD'))])
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='unresolved'},indent=2))
print('Full evidence:',out,flush=True)
raise SystemExit(0 if passed else 3)

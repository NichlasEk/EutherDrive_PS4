#!/usr/bin/env python3
"""Compile the complete MD engine and KSNES from a pinned, isolated snapshot."""
import argparse, hashlib, json, os, re, shutil, subprocess
from pathlib import Path
root = Path(__file__).resolve().parent.parent
source = root.parent / 'EutherDrive_Android'
revision = '7771ae7f736caef7f20399a6315d3140217bfbd2'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, default=root/'build/console-player')
parser.add_argument('--sms-fast-read', action='store_true', help='experimental SMS read dispatch without diagnostic hooks')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
paths = subprocess.check_output(['git','-C',str(source),'ls-tree','-r','--name-only',revision],text=True).splitlines()
exclude = {'Program.cs','_shims.cs','MdMainStub.cs','MdTracerMainStub.cs','CpuStubs.cs','md_main_setting.cs','MdBusBridge.cs','md_m68k_missing_globals.cs','md_vdp.IO.cs','VdpHeadless.cs','Injector.cs','SystemMananger.cs','MameM68Ec020.cs'}
tops = {'MdTracerAdapter.cs','SnesAdapter.cs','IEmulatorCore.cs','IExtendedInputHandler.cs','PadType.cs','RomInfo.cs','ConsoleIdentity.cs','MegaDriveRegion.cs','GenesisAudioFilterPort.cs','RomArchiveExtractor.cs','CueSheetResolver.cs','OpticalDiscDetector.cs','PerfHotspots.cs','MegaDriveBus.cs','MdTracerM68kRunner.cs','MdTracerM68kContextRunner.cs'}
def replace_body(code, signature, body):
    start=code.index(signature); brace=code.index('{',start); depth=1; end=brace+1
    while depth:
        depth += (code[end]=='{') - (code[end]=='}'); end+=1
    return code[:brace+1]+'\n'+body+'\n'+code[end-1:]
files, hashes = [], {}
for path in paths:
    if not path.endswith('.cs'): continue
    rel = path.removeprefix('EutherDrive.Core/')
    core = path.startswith('EutherDrive.Core/') and (rel in tops or rel.startswith(('MdTracerCore/','Sega32X/','SegaCd/','Cpu/M68000Emu/','Cpu/Z80Emu/','Savestates/','Diagnostics/')))
    snes = path.startswith('SuperNintendoEmulator/KSNES/') and '/obj/' not in path and '/bin/' not in path
    if not (core or snes) or Path(path).name in exclude or 'md_vdp_renderer_frame_directx' in path: continue
    raw = subprocess.check_output(['git','-C',str(source),'show',f'{revision}:{path}'])
    hashes[path] = hashlib.sha256(raw).hexdigest()
    code = raw.decode('utf-8-sig')
    if path.endswith('md_m68k_initialize2.cs'):
        # PS4 Mono rejects this 47k-statement method as "too complex".
        # Preserve every opcode registration and its order, but bound JIT work.
        statements = code.splitlines()[7:-3]
        if len(statements) != 47383 or any(not line.strip().startswith('opcode_add(') or not line.strip().endswith(');') for line in statements):
            raise SystemExit('Unexpected M68K opcode initialization layout')
        chunks = [statements[i:i+256] for i in range(0, len(statements), 256)]
        code = 'using System;\nnamespace EutherDrive.Core.MdTracerCore {\ninternal partial class md_m68k {\nprivate void initialize2() {\n'
        code += ''.join(f'initialize2_part_{i}();\n' for i in range(len(chunks))) + '}\n'
        for i, chunk in enumerate(chunks):
            code += '[System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]\n'
            code += f'private void initialize2_part_{i}() {{\n' + '\n'.join(chunk) + '\n}\n'
        code += '}\n}\n'
    if path.endswith('/md_z80_memory.cs'):
        signature = '        private byte ReadSmsMemory(ushort a)'
        assert code.count(signature) == 1
        fast = (root/'probes/consoles/SmsReadFastPath.cs.txt').read_text()
        code = code.replace(signature, fast + '        private byte ReadSmsMemoryOriginal(ushort a)')
        if args.sms_fast_read:
            signature = '        public byte read8(uint in_address)'
            assert code.count(signature) == 1
            dispatch = (root/'probes/consoles/SmsReadDispatch.cs.txt').read_text()
            code = code.replace(signature, dispatch + '        private byte Read8WithDiagnostics(uint in_address)')
    if path.endswith('/md_main.cs'):
        old = """                if (g_masterSystemMode)
                {
                    g_md_z80?.ResetLineCycles();"""
        new = """                if (g_masterSystemMode)
                {
                    long ps4CpuStart = Orbis.PerformanceProbe.Start();
                    g_md_z80?.ResetLineCycles();"""
        assert code.count(old) == 1
        code = code.replace(old,new)
        old = """                    g_md_vdp.run(vline);
                    continue;"""
        new = """                    Orbis.PerformanceProbe.Cpu(ps4CpuStart);
                    long ps4VdpStart = Orbis.PerformanceProbe.Start();
                    g_md_vdp.run(vline);
                    Orbis.PerformanceProbe.Vdp(ps4VdpStart);
                    continue;"""
        assert code.count(old) == 1
        code = code.replace(old,new)
    code = re.sub(r'(?:System\.)?Array.Empty<', 'Orbis.Framework.Empty<', code)
    code = re.sub(r'Array.Clear\(([^,;\n]+)\);',r'Array.Clear(\1, 0, \1.Length);',code)
    code = re.sub(r'ArgumentNullException.ThrowIfNull\((\w+)\);',lambda m: f'if ({m[1]} == null) throw new ArgumentNullException("{m[1]}");',code)
    code = code.replace('[JsonIgnore]', '').replace('global using Microsoft.Extensions.DependencyInjection;', '').replace('global using System.Text.Json;', '').replace('global using System.Text.Json.Serialization;', '')
    code = code.replace('Math.Clamp(', 'Orbis.Framework.Clamp(').replace('MathF.', 'Orbis.Framework.')
    code = code.replace('byte InterruptVector() => 0xff;', 'byte InterruptVector();')
    # The MD bus owns the same explicit ff interrupt vector as the default method.
    if 'class' in code and ': IBusInterface' in code and 'InterruptVector()' not in code:
        code = code.replace('public bool BusReq()', 'public byte InterruptVector() => 0xff;\n        public bool BusReq()')
    code = code.replace('System.Array.Fill(', 'Orbis.Framework.Fill(').replace("string.Join(' ',", 'string.Join(" ",')
    code = code.replace('Array.Fill(', 'Orbis.Framework.Fill(').replace('string.Create(', 'Orbis.Framework.Create(')
    code = code.replace('Environment.TickCount64', 'Orbis.Framework.TickCount64').replace('AppContext.BaseDirectory','AppDomain.CurrentDomain.BaseDirectory')
    code = code.replace('double.IsFinite(', 'Orbis.Framework.IsFinite(').replace('Convert.ToHexString(', 'Orbis.Framework.ToHexString(').replace('SHA1.HashData(', 'Orbis.Framework.Sha1(')
    code = code.replace('RuntimeHelpers.GetUninitializedObject(', 'System.Runtime.Serialization.FormatterServices.GetUninitializedObject(')
    code = code.replace('type.IsByRefLike', 'Orbis.Framework.IsByRefLike(type)')
    code = re.sub(r'(\w+)\[2\.\.\]', r'\1.Substring(2)', code)
    code = code.replace('header[..read]', 'header.Slice(0, read)').replace('cue._tracks[^1]', 'cue._tracks[cue._tracks.Count - 1]')
    code = re.sub(r"\.(StartsWith|EndsWith)\('([^']+)'\)", r'.\1("\2")', code)
    code = re.sub(r'new string\((s|mask)\)', r'new string(\1.ToArray())', code)
    code = code.replace('raw.AsSpan(2)', 'raw.Substring(2)')
    # TrimEntries is handled by a compatibility split wrapper, preserving whitespace behavior.
    code = code.replace('StringSplitOptions.TrimEntries', '(StringSplitOptions)2')
    code = re.sub(r'(\w+)\.Split\(([^;\n]+)\)', r'Orbis.Framework.Split(\1, \2)', code)
    code = code.replace('BitOperations.' , 'Orbis.Framework.')
    code = re.sub(r'"/home/nichlas/EutherDrive/logs(?:/([^"\n]+))?"',
                  lambda m: 'Orbis.Framework.LogDirectory' if not m[1] else 'Path.Combine(Orbis.Framework.LogDirectory, "'+m[1]+'")', code)
    code = re.sub(r'"/tmp/([^"\n]+)"', lambda m: 'Path.Combine(Orbis.Framework.LogDirectory, "'+m[1]+'")', code)
    # Persistence is not exposed in this candidate. Keep cartridge RAM emulation,
    # but never import or overwrite saves next to a user's ROM.
    if path.endswith('md_bus.cs'): code=replace_body(code,'private static string? BuildSramPath(', '            return null;')
    if path.endswith('md_z80_memory.cs'): code=replace_body(code,'private static string? BuildSmsSramPath(', '            return null;')
    if path.endswith('KSNES/ROM/ROM.cs'):
        code=re.sub(r'_sRAMTimer \?\?= new Timer\(SaveSRAM,[^;]+;', '{ /* PS4 candidate: persistence disabled */ }', code)
        for name in ['private void SaveSRAM(', 'private void SaveSpc7110Rtc(']: code=replace_body(code,name,'        return;')
        code=replace_body(code,'private string GetSRAMFileName(', '        return Path.Combine(Orbis.Framework.LogDirectory, "disabled-save.srm");')
    if path.endswith('PersistentStoragePath.cs'):
        code=replace_body(code,'private static bool TryGetWritableSiblingDirectory(', '        directory = null; return false;')
    target = out / 'source' / path
    target.parent.mkdir(parents=True,exist_ok=True); target.write_text(code); files.append(str(target))
resources=[]
for path in paths:
    if not path.startswith('EutherDrive.Core/Sega32X/BootRoms/') or not path.endswith('.bin'): continue
    raw=subprocess.check_output(['git','-C',str(source),'show',f'{revision}:{path}'])
    target=out/'source'/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
    hashes[path]=hashlib.sha256(raw).hexdigest()
    resources.append('-resource:'+str(target)+',EutherDrive.Core.Sega32X.BootRoms.'+target.name)
(out/'source-manifest.json').write_text(json.dumps({'revision':revision,'sms_fast_read':args.sms_fast_read,'sha256':hashes},indent=2)+'\n')
reference = Path(subprocess.check_output([str(root/'scripts/prepare-mono-reference.sh')],text=True).strip())
bcl = reference / 'mono/4.5'
sdk = subprocess.check_output(['dotnet','--list-sdks'],text=True).splitlines()[-1]
version,sdkroot = re.match(r'(\S+) \[(.+)\]',sdk).groups()
refs = [Path('/usr/lib/mono/4.5-api')/n for n in ['mscorlib.dll','System.dll','System.Core.dll','System.Xml.dll','System.Xml.Linq.dll','System.Drawing.dll','System.Runtime.Serialization.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll']]
refs += [bcl/n for n in ['System.ValueTuple.dll','System.Memory.dll','System.Runtime.dll','System.Runtime.InteropServices.dll','System.Runtime.CompilerServices.Unsafe.dll','System.Buffers.dll','System.Numerics.Vectors.dll']]
refs += [Path('/usr/lib/mono/4.5-api/Facades/System.IO.dll')]
refs += [Path('/home/nichlas/.nuget/packages/nlayer/1.16.0/lib/netstandard1.3/NLayer.dll'),Path('/home/nichlas/.nuget/packages/sharpcompress/0.36.0/lib/net462/SharpCompress.dll')]
host = out/'host';host.mkdir(exist_ok=True)
cmd = ['dotnet',str(Path(sdkroot)/version/'Roslyn/bincore/csc.dll'),'-nologo','-nostdlib+','-nullable:annotations','-langversion:latest','-target:exe','-unsafe+','-platform:x64','-optimize+','-out:'+str(host/'main.exe')]
result = subprocess.run(cmd+resources+['-r:'+str(p) for p in refs]+files+[str(root/'probes/gb/Compat.cs'),str(root/'probes/consoles/Compat.cs'),str(root/'probes/gb/Player.cs'),str(root/'probes/consoles/Backend.cs'),str(root/'probes/consoles/PerformanceProbe.cs'),'-define:CONSOLE_PLAYER'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/'compile.log').write_text(result.stdout)
print(result.stdout)
if result.returncode: raise SystemExit(result.returncode)
# Keep the runtime closure explicit, including the one additional forwarding facade.
closure_bcl = out/'bcl-source'; closure_bcl.mkdir(exist_ok=True)
for dll in Path('/usr/lib/mono/4.5/Facades').glob('*.dll'): shutil.copy2(dll,closure_bcl/dll.name)
for dll in bcl.glob('*.dll'): shutil.copy2(dll,closure_bcl/dll.name)
for dll in refs[-3:]:
    runtime = Path('/usr/lib/mono/4.5/Facades/System.IO.dll') if dll.name=='System.IO.dll' else dll
    shutil.copy2(runtime,closure_bcl/runtime.name)
shutil.copy2('/home/nichlas/.nuget/packages/zstdsharp.port/0.7.4/lib/net462/ZstdSharp.dll',closure_bcl/'ZstdSharp.dll')
subprocess.run(['python3',str(root/'scripts/runtime-dependencies.py'),str(host/'main.exe'),str(closure_bcl),'--copy-to',str(out/'package-bcl')],check=True)
smoke_host=out/'desktop-host'; smoke_host.mkdir(exist_ok=True)
shutil.copy2(host/'main.exe',smoke_host/'main.exe')
for dll in (out/'package-bcl').glob('*.dll'):
    if dll.name not in {'mscorlib.dll','System.dll','System.Core.dll','System.Xml.dll','System.Xml.Linq.dll','System.Drawing.dll','System.Runtime.Serialization.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','Mono.Security.dll'}: shutil.copy2(dll,smoke_host/dll.name)
(out/'dependency-manifest.json').write_text(json.dumps({dll.name:hashlib.sha256(dll.read_bytes()).hexdigest() for dll in (out/'package-bcl').glob('*.dll')},indent=2)+'\n')
from console_library import input_roms
logs=[]
for i,rom in enumerate(input_roms()):
    capture=out/f'rom{i:02d}'; capture.mkdir(exist_ok=True)
    local_rom=capture/('game'+rom.suffix); shutil.copy2(rom,local_rom)
    env=dict(os.environ,EUTHERDRIVE_RUNTIME_PROBE_HOST='1',ED_GB_ROM=str(local_rom),ED_GB_CAPTURE=str(capture),MONO_PATH=str(smoke_host),EUTHERDRIVE_TRACE_CONSOLE='0')
    run=subprocess.run(['mono',str(smoke_host/'main.exe')],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=240)
    logs.append(run.stdout); (out/'host-validation.log').write_text('\n'.join(logs))
    print(run.stdout[-3000:])
    if run.returncode or 'RESULT PASS\n' not in run.stdout: raise SystemExit('Console desktop validation failed')

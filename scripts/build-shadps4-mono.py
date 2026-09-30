#!/usr/bin/env python3
"""Build an isolated experimental shadPS4; does not replace the installed emulator."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'e3ce810f3a653f43ac64ebab63023de281a4103a'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--jobs',type=int,default=8)
args=parser.parse_args()
if args.jobs<1:parser.error('jobs must be positive')
source=ROOT/'.deps/shadps4-mono'
build=ROOT/'build/shadps4-dev'
patch=ROOT/'patches/shadps4-mono/0001-mono-runtime.patch'
def git(*args, check=True):
    return subprocess.run(['git','-C',str(source),*args],check=check)
if not (source/'.git').exists():
    source.mkdir(parents=True,exist_ok=True)
    git('init')
    git('remote','add','origin','https://github.com/shadps4-emu/shadPS4.git')
    git('fetch','--depth','1','origin',REVISION)
    git('checkout','--detach',REVISION)
actual=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
if actual!=REVISION:raise SystemExit(f'Expected {REVISION}, found {actual}; checkout was left untouched')
if git('apply','--reverse','--check',str(patch),check=False).returncode:
    git('apply','--check',str(patch))
    git('apply',str(patch))
# Do not fetch macOS Mesa or documentation/test-only nested repositories.
lines=subprocess.check_output(['git','-C',str(source),'config','--file','.gitmodules',
                               '--get-regexp','submodule.*.path'],text=True).splitlines()
paths=[line.split(' ',1)[1] for line in lines if 'mesa-kosmickrisp' not in line]
git('submodule','update','--init','--depth','1','--jobs',str(args.jobs),'--',*paths)
for path in ['externals/zydis','externals/sirit','externals/freetype']:
    subprocess.run(['git','-C',str(source/path),'submodule','update','--init','--depth','1'],check=True)
build.mkdir(parents=True,exist_ok=True)
for name in ['mono_mspace', 'mono_printf', 'mono_safe_string']:
    test=build/('test-'+name.replace('_','-'))
    subprocess.run(['clang++','-std=c++20','-g','-O1','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-fsanitize=address,undefined','-pthread','-I'+str(source/'src'),
        str(source/'src/tests'/f'{name}.cpp'),'-o',str(test)],check=True)
    subprocess.run([str(test)],check=True)
subprocess.run(['cmake','-S',str(source),'-B',str(build),'-G','Ninja',
    '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_BUILD_TYPE=Release',
    '-DENABLE_DISCORD_RPC=OFF','-DENABLE_UPDATER=OFF'],check=True)
subprocess.run(['cmake','--build',str(build),'--parallel',str(args.jobs)],check=True)
print('Built:',build/'shadps4')
print('Enable the opt-in experiment with SHADPS4_EXPERIMENTAL_MONO=1; default behavior is unchanged.')

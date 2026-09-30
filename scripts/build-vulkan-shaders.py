#!/usr/bin/env python3
"""Compile and validate the two Vulkan 1.0 shaders used by the PS4 player."""
from pathlib import Path
import subprocess, struct, tempfile
root=Path(__file__).resolve().parent.parent
output=['// Generated from shaders/*. GLSL sources are the source of truth.','#pragma once','#include <stdint.h>']
with tempfile.TemporaryDirectory() as tmp:
    for symbol,name in [('ed_vk_vertex','fullscreen.vert'),('ed_vk_fragment','texture.frag')]:
        binary=Path(tmp)/'shader.spv'
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.0',str(root/'probes/runtime/shaders'/name),'-o',str(binary)],check=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.0',str(binary)],check=True)
        data=binary.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
        output.append('static const uint32_t '+symbol+'[] = {'+','.join(hex(w) for w in words)+'};')
(root/'probes/runtime/vulkan-shaders.h').write_text('\n'.join(output)+'\n')

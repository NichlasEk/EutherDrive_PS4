#!/usr/bin/env sh
set -eu
project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
reference=$("$project_dir/scripts/prepare-mono-reference.sh")
toolchain=${OO_PS4_TOOLCHAIN:-/opt/openorbis/OpenOrbis/PS4Toolchain}
export OO_PS4_TOOLCHAIN="$toolchain"
output=$project_dir/build/runtime-probe
mkdir -p "$output"
if [ -n "${ED_JBC_DIR:-}" ]; then
    jbc=$ED_JBC_DIR
    [ -f "$jbc/libut99-jbc.a" ] && [ -f "$jbc/source-sha256.txt" ] || { echo 'Incomplete libjbc cache' >&2; exit 1; }
else
    python3 "$project_dir/../ut99-orbis/scripts/build-ps4-usb.py"
    jbc=$project_dir/../ut99-orbis/build/ps4-usb
fi
core_define=
if [ "${ED_GB_PROBE:-0}" = 1 ]; then core_define=-DGB_CORE_PROBE; fi
set --
if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then export ED_GB_PLAYER=1; fi
if [ "${ED_SMS_PLAYER:-0}" = 1 ]; then export ED_GB_PLAYER=1; fi
if [ "${ED_GB_PLAYER:-0}" = 1 ]; then
    core_define=-DGB_PLAYER
    if [ "${ED_SMS_PLAYER:-0}" = 1 ]; then core_define="-DGB_PLAYER -DSMS_PLAYER"; fi
    if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then core_define="-DGB_PLAYER -DCONSOLE_PLAYER"; fi
    set -- -lScePad -lSceUserService -lSceAudioOut
    cc -Wall -Wextra -Werror "$project_dir/probes/runtime/test-gb-resampler.c" -o "$output/test-resampler"
    "$output/test-resampler"
    cc -Wall -Wextra -Werror -pthread "$project_dir/probes/runtime/test-gb-audio.c" -o "$output/test-audio"
    "$output/test-audio"
    cc -Wall -Wextra -Werror -pthread "$project_dir/probes/runtime/test-gb-ui.c" -o "$output/test-ui"
    "$output/test-ui"
    cc -Wall -Wextra -Werror -pthread -DSMS_PLAYER "$project_dir/probes/runtime/test-gb-ui.c" -o "$output/test-sms-ui"
    "$output/test-sms-ui"
    cc -Wall -Wextra -Werror -pthread -DCONSOLE_PLAYER "$project_dir/probes/runtime/test-gb-ui.c" -o "$output/test-console-ui"
    "$output/test-console-ui"
fi
cc -Wall -Wextra -Werror -I"$jbc" "$project_dir/probes/runtime/test-credential-probe.c" -o "$output/test-credentials"
"$output/test-credentials"

# The hook offset and signature are ONLY valid for this exact runtime.
expected=fa39527bbd559f72efd09d9f39c3b6d7ed03be7f75d47965ad333fa9d4932414
actual=$(sha256sum "$reference/sce_module/libmonosgen-2.0.prx" | cut -d ' ' -f 1)
[ "$actual" = "$expected" ] || { echo 'Unsupported Mono runtime hash' >&2; exit 1; }

# core_define contains separately selected compiler defines.
if [ "${ED_VULKAN_PLAYER:-0}" = 1 ]; then
    stack=${ED_VULKAN_STACK:-$project_dir/../ut99-orbis/build/native}
    [ -f "$stack/vulkan-ps4/libvulkan_ps4.a" ] && [ -f "$stack/libpsbc-private.a" ] || { echo 'Build the existing native Vulkan stack first' >&2; exit 1; }
    core_define="$core_define -DVULKAN_PLAYER"
    cc -Wall -Wextra -Werror "$project_dir/probes/runtime/test-vulkan-flip.c" -o "$output/test-vulkan-flip"
    "$output/test-vulkan-flip"
    set -- "$@" --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=sceGnmVideoOutSubmitFlipAndWait "$stack/vulkan-ps4/libvulkan_ps4.a" "$stack/opengnm/libopengnm.a" "$stack/libpsbc-private.a" -lc++ -lc++abi -lunwind -lSceGnmDriver
    sha256sum "$stack/vulkan-ps4/libvulkan_ps4.a" "$stack/opengnm/libopengnm.a" "$stack/libpsbc-private.a" > "$output/vulkan-archives.sha256"
    # An explicitly selected header tree must match the reused ICD sources.
    vulkan_include=$stack/Vulkan-Headers/include
    gnm_include=$stack/opengnm/include
else
    vulkan_include=$toolchain/include
    gnm_include=$toolchain/include
fi
# shellcheck disable=SC2086
clang --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 \
    -DMONO_CREDENTIAL_PROBE $core_define -I"$jbc" -I"$vulkan_include" -I"$gnm_include" -Wall -Wextra -Werror -Wno-unused-function \
    -isysroot "$toolchain" -isystem "$toolchain/include" \
    -c "$project_dir/probes/runtime/host.c" -o "$output/host.o"
ld.lld -m elf_x86_64 -pie --script "$toolchain/link.x" --eh-frame-hdr \
    -L"$toolchain/lib" "$output/host.o" "$jbc/libut99-jbc.a" "$toolchain/lib/crt1.o" \
    --start-group -lc -lkernel -lSceVideoOut -lSceSysmodule "$@" --end-group -o "$output/host.elf"
"$toolchain/bin/linux/create-fself" -in="$output/host.elf" \
    -out="$output/host.oelf" --eboot "$output/eboot.bin" --paid 0x3800000000000011

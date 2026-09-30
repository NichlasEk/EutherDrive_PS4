#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${OO_PS4_TOOLCHAIN:-/opt/openorbis/OpenOrbis/PS4Toolchain}
export OO_PS4_TOOLCHAIN="$sdk"
ut99="$root/../ut99-orbis"
# Use the same pinned, adapted helper as our UT99 USB path.
python3 "$ut99/scripts/build-ps4-usb.py"
jbc="$ut99/build/ps4-usb"
build="$root/build/jit-rights-probe"
mkdir -p "$build" "$root/dist"
cc -Wall -Wextra -Werror -I"$jbc" "$root/probes/runtime/test-credential-probe.c" -o "$build/test-credentials"
"$build/test-credentials"
stage=$(mktemp -d "$build/pkgroot-0.05.XXXXXX")
mkdir -p "$stage/sce_module" "$stage/sce_sys/about"
clang --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 \
    -DNATIVE_CREDENTIAL_PROBE -Wall -Wextra -Werror -Wno-unused-function \
    -isysroot "$sdk" -isystem "$sdk/include" -I"$jbc" \
    -c "$root/probes/runtime/host.c" -o "$build/host.o"
ld.lld -m elf_x86_64 -pie --script "$sdk/link.x" --eh-frame-hdr \
    -L"$sdk/lib" "$build/host.o" "$jbc/libut99-jbc.a" "$sdk/lib/crt1.o" \
    -lc -lkernel -lSceVideoOut -o "$build/host.elf"
"$sdk/bin/linux/create-fself" -in="$build/host.elf" -out="$build/host.oelf" \
    --eboot "$stage/eboot.bin" --paid 0x3800000000000011
cp "$sdk/samples/SDL2/sce_module/"{libc.prx,libSceFios2.prx} "$stage/sce_module/"
cp "$sdk/samples/piglet/sce_sys/about/right.sprx" "$stage/sce_sys/about/"
cp "$sdk/samples/SDL2/sce_sys/icon0.png" "$stage/sce_sys/"
pkgtool="$sdk/bin/linux/PkgTool.Core"
sfo="$stage/sce_sys/param.sfo"
content_id=IV0000-EDRM00001_00-EUTHERMONOPROBE1
"$pkgtool" sfo_new "$sfo"
for pair in APP_TYPE:1 ATTRIBUTE:0 DOWNLOAD_DATA_SIZE:0 SYSTEM_VER:0; do
    "$pkgtool" sfo_setentry "$sfo" "${pair%%:*}" --type Integer --maxsize 4 --value "${pair#*:}"
done
for key in APP_VER VERSION; do
    "$pkgtool" sfo_setentry "$sfo" "$key" --type Utf8 --maxsize 8 --value 0.05
done
"$pkgtool" sfo_setentry "$sfo" CATEGORY --type Utf8 --maxsize 4 --value gd
"$pkgtool" sfo_setentry "$sfo" CONTENT_ID --type Utf8 --maxsize 48 --value "$content_id"
"$pkgtool" sfo_setentry "$sfo" TITLE_ID --type Utf8 --maxsize 12 --value EDRM00001
"$pkgtool" sfo_setentry "$sfo" TITLE --type Utf8 --maxsize 128 --value 'EutherDrive Native JIT Rights'
(cd "$stage" && "$sdk/bin/linux/create-gp4" -out pkg.gp4 --content-id="$content_id" \
    --files 'eboot.bin sce_module/libc.prx sce_module/libSceFios2.prx sce_sys/icon0.png sce_sys/param.sfo sce_sys/about/right.sprx')
sed -i '/<dir targ_name="assets">/,/<\/dir>/d' "$stage/pkg.gp4"
(cd "$stage" && "$pkgtool" pkg_build pkg.gp4 .)
"$pkgtool" pkg_validate --verbose "$stage/$content_id.pkg"
cp "$stage/$content_id.pkg" "$root/dist/eutherdrive-jit-rights-probe-0.05.pkg"
ln -sfn "$stage" "$build/current-pkgroot"
cp "$jbc/source-sha256.txt" "$build/jbc-source-sha256.txt"
sha256sum "$root/dist/eutherdrive-jit-rights-probe-0.05.pkg"

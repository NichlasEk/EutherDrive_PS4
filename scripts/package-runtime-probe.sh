#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
reference=$("$project_dir/scripts/prepare-mono-reference.sh")
toolchain=${OO_PS4_TOOLCHAIN:-/opt/openorbis/OpenOrbis/PS4Toolchain}
build_dir=$project_dir/build/runtime-probe
stage_dir=$build_dir/pkgroot
dist_dir=$project_dir/dist
content_id=IV0000-EDRM00001_00-EUTHERMONOPROBE1
version=0.06
package_name=eutherdrive-runtime-probe-0.06.pkg
title='EutherDrive Mono Probe'
if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then export ED_GB_PLAYER=1; fi
if [ "${ED_SMS_PLAYER:-0}" = 1 ]; then export ED_GB_PLAYER=1; fi
if [ "${ED_GB_PLAYER:-0}" = 1 ]; then export ED_GB_PROBE=1; fi
if [ "${ED_GB_PROBE:-0}" = 1 ]; then
    version=0.07
    package_name=eutherdrive-gb-core-probe-0.07.pkg
    title='EutherDrive GB Core Probe'
fi
if [ "${ED_GB_PLAYER:-0}" = 1 ]; then
    version=0.09
    package_name=eutherdrive-gb-player-0.09.pkg
    title='EutherDrive GB'
fi

if [ "${ED_SMS_PLAYER:-0}" = 1 ]; then
    version=0.10
    package_name=eutherdrive-sms-player-0.10.pkg
    title='EutherDrive Master System'
fi

if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then
    version=0.13
    package_name=eutherdrive-console-player-0.13.pkg
    title='EutherDrive Consoles'
fi

if [ ! -d "$toolchain" ]; then
	echo "OpenOrbis toolchain not found: $toolchain" >&2
	exit 1
fi

for required in libmonosgen-2.0.prx libMonoUtils.sprx libc.prx libSceFios2.prx; do
	if [ ! -f "$reference/sce_module/$required" ]; then
		echo "Missing local runtime file: $reference/sce_module/$required" >&2
		exit 1
	fi
done

if [ "${ED_GB_PROBE:-0}" = 1 ]; then
    if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then
        python3 "$project_dir/scripts/build-console-player.py"
    elif [ "${ED_SMS_PLAYER:-0}" = 1 ]; then
        python3 "$project_dir/scripts/build-sms-player.py"
    else
        python3 "$project_dir/scripts/build-gb-probe.py"
    fi
    mkdir -p "$build_dir"
    core_build=gb-probe
    if [ "${ED_GB_PLAYER:-0}" = 1 ]; then core_build=gb-player; fi
    if [ "${ED_SMS_PLAYER:-0}" = 1 ]; then core_build=sms-player; fi
    if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then core_build=console-player; fi
    cp "$project_dir/build/$core_build/host/main.exe" "$build_dir/main.exe"
else
    "$project_dir/scripts/build-runtime-probe.sh"
fi

"$project_dir/scripts/build-runtime-host.sh"

# Always stage into a new directory, so old DLLs cannot hide missing dependencies.
stage_dir=$(mktemp -d "$build_dir/pkgroot-$version.XXXXXX")

mkdir -p "$stage_dir/sce_module" "$stage_dir/sce_sys/about" \
	"$stage_dir/mono/4.5" "$dist_dir"

cp "$build_dir/eboot.bin" "$stage_dir/eboot.bin"
cp "$build_dir/main.exe" "$stage_dir/main.exe"
cp "$reference/sce_module/libmonosgen-2.0.prx" "$stage_dir/sce_module/"
cp "$reference/sce_module/libMonoUtils.sprx" "$stage_dir/sce_module/"
cp "$toolchain/samples/SDL2/sce_module/libc.prx" "$stage_dir/sce_module/"
cp "$toolchain/samples/SDL2/sce_module/libSceFios2.prx" "$stage_dir/sce_module/"
cp "$toolchain/samples/piglet/sce_sys/about/right.sprx" "$stage_dir/sce_sys/about/"
python3 - "$project_dir/assets/icons/icon0.png" <<'PY'
import sys
from PIL import Image
with Image.open(sys.argv[1]) as icon:
    if icon.format != "PNG" or icon.size != (512, 512) or icon.mode != "RGBA":
        raise SystemExit("App icon must be a 512x512 RGBA PNG")
    icon.load()
PY
cp "$project_dir/assets/icons/icon0.png" "$stage_dir/sce_sys/"
if [ "${ED_GB_PROBE:-0}" = 1 ]; then
    cp "$project_dir/probes/gb/THIRD-PARTY-NOTICES.md" "$stage_dir/GB-NOTICES.txt"
fi
if [ "${ED_GB_PLAYER:-0}" = 1 ]; then
    if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then
        python3 "$project_dir/scripts/console_library.py" "$stage_dir"
        cp "$project_dir/probes/consoles/THIRD-PARTY-NOTICES.md" "$stage_dir/CONSOLE-NOTICES.txt"
    elif [ "${ED_SMS_PLAYER:-0}" = 1 ]; then
        python3 "$project_dir/scripts/sms_library.py" "$stage_dir"
        cp "$project_dir/probes/sms/THIRD-PARTY-NOTICES.md" "$stage_dir/SMS-NOTICES.txt"
    else
        python3 "$project_dir/scripts/gb_library.py" "$stage_dir"
    fi
fi

bcl_source=$reference/mono/4.5
if [ "${ED_CONSOLE_PLAYER:-0}" = 1 ]; then bcl_source=$project_dir/build/console-player/bcl-source; fi
python3 "$project_dir/scripts/runtime-dependencies.py" "$build_dir/main.exe" \
    "$bcl_source" --copy-to "$stage_dir/mono/4.5"
python3 "$project_dir/scripts/runtime-dependencies.py" "$stage_dir/main.exe" \
    "$stage_dir/mono/4.5" > "$build_dir/package-dependencies.log"

pkg_tool=$toolchain/bin/linux/PkgTool.Core
create_gp4=$toolchain/bin/linux/create-gp4
param_sfo=$stage_dir/sce_sys/param.sfo

"$pkg_tool" sfo_new "$param_sfo"
"$pkg_tool" sfo_setentry "$param_sfo" APP_TYPE --type Integer --maxsize 4 --value 1
"$pkg_tool" sfo_setentry "$param_sfo" APP_VER --type Utf8 --maxsize 8 --value "$version"
"$pkg_tool" sfo_setentry "$param_sfo" ATTRIBUTE --type Integer --maxsize 4 --value 0
"$pkg_tool" sfo_setentry "$param_sfo" CATEGORY --type Utf8 --maxsize 4 --value 'gd'
"$pkg_tool" sfo_setentry "$param_sfo" CONTENT_ID --type Utf8 --maxsize 48 --value "$content_id"
"$pkg_tool" sfo_setentry "$param_sfo" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$pkg_tool" sfo_setentry "$param_sfo" SYSTEM_VER --type Integer --maxsize 4 --value 0
"$pkg_tool" sfo_setentry "$param_sfo" TITLE --type Utf8 --maxsize 128 --value "$title"
"$pkg_tool" sfo_setentry "$param_sfo" TITLE_ID --type Utf8 --maxsize 12 --value 'EDRM00001'
"$pkg_tool" sfo_setentry "$param_sfo" VERSION --type Utf8 --maxsize 8 --value "$version"

files=$(cd "$stage_dir" && find . -type f ! -name pkg.gp4 ! -name '*.pkg' -printf '%P ')
(cd "$stage_dir" && "$create_gp4" -out pkg.gp4 --content-id="$content_id" --files "$files")
sed -i '/<dir targ_name="assets">/,/<\/dir>/d' "$stage_dir/pkg.gp4"
sed -i 's#<dir targ_name="sce_sys">#<dir targ_name="mono">\n\t\t\t<dir targ_name="4.5" />\n\t\t</dir>\n\t\t<dir targ_name="sce_sys">#' "$stage_dir/pkg.gp4"
(cd "$stage_dir" && "$pkg_tool" pkg_build pkg.gp4 .)

"$pkg_tool" pkg_validate --verbose "$stage_dir/$content_id.pkg"
cp "$stage_dir/$content_id.pkg" "$dist_dir/$package_name"
ln -sfn "$stage_dir" "$build_dir/current-pkgroot"
sha256sum "$build_dir/main.exe" "$stage_dir/eboot.bin" \
	"$dist_dir/$package_name"

echo "Package: $dist_dir/$package_name"
echo "Do not redistribute the package until the bundled PS4 Mono runtime has been audited."
if [ "${ED_GB_PLAYER:-0}" = 1 ]; then echo 'PRIVATE package includes your supplied ROM. Do not distribute.'; fi

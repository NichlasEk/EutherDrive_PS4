#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
emulator=${SHADPS4:-$project_dir/../ScummVM-PS4/.tools/shadps4/Shadps4-sdl.AppImage}
stage=$project_dir/build/runtime-probe/current-pkgroot
profile_root=$project_dir/build/runtime-probe/emulator-profile
fake_bin=$project_dir/build/runtime-probe/fake-bin
log=$project_dir/build/runtime-probe/emulator.log
guest_log=$profile_root/shadPS4/data/eutherdrive-ps4/runtime-probe.log
native_log=$profile_root/shadPS4/data/eutherdrive-ps4/native-probe.log
if [[ ${ED_GB_PROBE:-0} == 1 ]]; then
    # The core harness reports through NativeReport, including RESULT PASS.
    guest_log=$native_log
fi
if [[ ${NATIVE_JIT_RIGHTS:-0} == 1 ]]; then
    stage=$project_dir/build/jit-rights-probe/current-pkgroot
    native_log=$profile_root/shadPS4/data/eutherdrive-ps4/jit-rights-probe.log
    guest_log=$profile_root/no-managed-code-in-native-probe.log
fi

if [[ ! -x "$emulator" ]]; then
	echo "Set SHADPS4 to an executable shadPS4 SDL build." >&2
	exit 1
fi
if [[ ! -f "$stage/eboot.bin" ]]; then
	echo "Build the package first with ./scripts/package-runtime-probe.sh" >&2
	exit 1
fi

mkdir -p "$profile_root" "$fake_bin" \
	"$project_dir/build/runtime-probe/emulator-config" \
	"$project_dir/build/runtime-probe/emulator-cache"
ln -sfn "$project_dir/scripts/zenity-noninteractive-wrapper.sh" "$fake_bin/zenity"

export PATH="$fake_bin:$PATH"
export XDG_DATA_HOME=$profile_root
export XDG_CONFIG_HOME=$project_dir/build/runtime-probe/emulator-config
export XDG_CACHE_HOME=$project_dir/build/runtime-probe/emulator-cache
export SDL_VIDEODRIVER=x11
ulimit -c 0

# An old PASS must never be accepted as the outcome of this run.
for previous in "$guest_log" "$native_log"; do
    if [[ -f "$previous" ]]; then
        mv "$previous" "$previous.previous.$(date +%s%N)"
    fi
done

set +e
timeout --kill-after=3s "${EMULATOR_TIMEOUT:-25}s" xvfb-run -a "$emulator" \
	--fullscreen false "$stage/eboot.bin" > "$log" 2>&1
emulator_exit=$?
set -e

if [[ -f "$guest_log" ]] && grep -q '^RESULT PASS$' "$guest_log" && \
    grep -Eq '^RESULT PASS MONO=managed RESTORE=(verified|not-needed)$' "$native_log"; then
	cat "$guest_log"
	echo "shadPS4 managed runtime probe: PASS"
	exit 0
fi

if [[ -f "$native_log" ]]; then
    cat "$native_log"
    if [[ ${NATIVE_JIT_RIGHTS:-0} == 1 ]] && grep -q '^RESULT PASS JIT=42 RESTORE=verified$' "$native_log"; then
        echo "Native JIT transaction and restoration: PASS"
        exit 0
    fi
    if grep -q '^FAIL ' "$native_log" && grep -q '^Probe stopped\.' "$native_log"; then
        echo "Native diagnostics stopped with a reported failure; managed tests did not pass." >&2
        echo "Native log: $native_log" >&2
        exit 3
    fi
fi

if grep -q 'Provided file /sys/common/lib/libkernel.sprx does not exist' "$log" && \
	grep -q 'Failed o Load the libKernel' "$log"; then
	echo "shadPS4 reached the native Mono host but cannot supply its required" >&2
	echo "/sys/common/lib/libkernel.sprx path. Managed code did not run." >&2
	echo "Log: $log (emulator exit $emulator_exit)" >&2
	exit 3
fi

echo "shadPS4 did not produce the managed probe log (exit $emulator_exit)." >&2
echo "Log: $log" >&2
exit 2

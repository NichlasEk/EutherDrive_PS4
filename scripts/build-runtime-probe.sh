#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
reference=$("$project_dir/scripts/prepare-mono-reference.sh")
output_dir=$project_dir/build/runtime-probe
bcl=$reference/mono/4.5

mkdir -p "$output_dir"

mcs -sdk:4.5 -platform:x64 -optimize+ \
	-out:"$output_dir/main.exe" \
	-r:"$bcl/System.Memory.dll" \
	-r:"$bcl/System.Runtime.dll" \
	-r:"$bcl/System.Runtime.CompilerServices.Unsafe.dll" \
	-r:"$bcl/System.Buffers.dll" \
	-r:"$bcl/System.Numerics.Vectors.dll" \
	"$project_dir/probes/runtime/Program.cs"

for assembly in System.Memory.dll System.Runtime.dll System.Runtime.Extensions.dll \
	System.Runtime.InteropServices.dll System.Runtime.CompilerServices.Unsafe.dll \
	System.Buffers.dll System.Numerics.Vectors.dll; do
	cp "$bcl/$assembly" "$output_dir/$assembly"
done

EUTHERDRIVE_RUNTIME_PROBE_HOST=1 MONO_PATH="$output_dir" mono "$output_dir/main.exe" \
	> "$output_dir/host-validation.log"

grep -q '^PASS span$' "$output_dir/host-validation.log"
grep -q '^PASS threads$' "$output_dir/host-validation.log"
grep -q '^RESULT PASS$' "$output_dir/host-validation.log"

sha256sum "$output_dir/main.exe"
echo "Managed host validation: PASS"

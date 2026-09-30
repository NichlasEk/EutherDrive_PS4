#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
reference=${PS4_MONO_REFERENCE:-$project_dir/.deps/PS4-OpenOrbis-Mono}
revision=183a861a85d026981160bf25b70c9297af4afdcf

if [ ! -d "$reference/.git" ]; then
	mkdir -p "$(dirname -- "$reference")"
	git clone https://github.com/marcussacana/PS4-OpenOrbis-Mono.git "$reference"
	git -C "$reference" checkout --detach "$revision"
fi

current=$(git -C "$reference" rev-parse HEAD)
if [ "$current" != "$revision" ]; then
	echo "Mono reference has revision $current; expected $revision." >&2
	echo "Use a clean checkout or set PS4_MONO_REFERENCE explicitly." >&2
	exit 1
fi

printf '%s\n' "$reference"

#!/usr/bin/env sh
set -eu

case " $* " in
	*" --title Save Migration "*)
		printf '%s\n' 'Do nothing'
		exit 0
		;;
esac

exec /usr/bin/zenity "$@"


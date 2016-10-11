#!/bin/sh

FILES_DIR="www/files"

OLDIFS="$IFS"
IFS='&'
set -- $QUERY_STRING
IFS="$OLDIFS"

while test $# -gt 0
do
	key=${1%%=*}
	val=${1#*=}

	case "$key" in
	"sha1") sha1="$val" ;;
	*) echo >&2 "unknown key '$key'" ;;
	esac

	shift
done

if test -d "$FILES_DIR"
then
	if test -z "$sha1"
	then
		echo 'Status: 200 OK'
		echo
		ls "$FILES_DIR" | tr '-' ' '
	else
		if test -f "$FILES_DIR/$sha1"-*
		then
			echo 'Status: 200 OK'
			echo
			cat "$FILES_DIR/$sha1"-*
		else
			echo 'Status: 404 Not Found'
			echo
		fi
	fi
fi

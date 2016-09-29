#!/bin/sh

# In part from http://codereview.stackexchange.com/questions/79549/bash-cgi-upload-file

FILES_DIR="www/files"

OLDIFS="$IFS"
IFS='&'
set -- $QUERY_STRING
IFS="$OLDIFS"

while test $# -gt 0
do
    key=${1%=*}
    val=${1#*=}

    case "$key" in
	"sha1") sha1="$val" ;;
	"type") type="$val" ;;
	"size") size="$val" ;;
	"delete") delete=1 ;;
	*) echo >&2 "unknown key '$key'" ;;
    esac

    shift
done

case "$REQUEST_METHOD" in
  POST)
    if test "$delete" = "1"
    then
	rm -f "$FILES_DIR/$sha1-$size-$type"
    else
	mkdir -p "$FILES_DIR"
	cat >"$FILES_DIR/$sha1-$size-$type"
    fi

    echo 'Status: 204 No Content'
    echo
    ;;

  *)
    echo 'Status: 405 Method Not Allowed'
    echo
esac

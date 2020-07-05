#!/bin/sh
#
# Copyright (c) 2020 Christian Couder
#

mark_tmp_file="/tmp/export_marks"

my_init_marks() {
	echo "1" >"$mark_tmp_file"
}

my_get_next_mark() {
	mark=$(cat "$mark_tmp_file")
	echo "$mark"
	mark=$((mark+1))
	echo "$mark" >"$mark_tmp_file"	
}

my_export_blob() {
	blob="$1"
	echo "blob"
	echo "mark :$(my_get_next_mark)"
	size=$(git cat-file -s "$blob") || return
	echo "data $size"
	git cat-file -p "$blob" || return
}

my_export_many_blob() {
	while read blob
	do
		my_export_blob "$blob"
	done
}

my_find_blobs() {
	rev="$1"
	git ls-tree "$rev" | while read mode type hash name
	do
		test "$type" = "blob" && echo "$hash"
	done
}

my_reset_rev() {
	rev="$1"
	echo "reset $rev"
}	

my_export_commits() {
	for rev in "$@"
	do
		git for-each-ref --format="commit %(refname)
mark :$(my_get_next_mark)
author %(author)
committer %(committer)
data %(contents:size)
%(contents)" "$rev"
	done
}

my_init_marks

rev=$(git symbolic-ref "$1")

my_find_blobs "$rev" | my_export_many_blob

echo

my_reset_rev "$rev"

my_export_commits "$rev"


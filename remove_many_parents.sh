#!/bin/sh
#
# Remove as many parents as possible without changing the merge base
# test count.

hash1="$1"
hash2="$2"
nb_mb="$3"

TMP_MERGE_COMMITS="/tmp/merge_commits.txt"
SCRIPT_DIR="$(pwd)"

git rev-list --min-parents=2 "$hash1" "$hash2" >"$TMP_MERGE_COMMITS"

while read -r commit
do
	echo "Processing $commit"
	GIT_EDITOR="$SCRIPT_DIR/remove_parent.sh" git replace --edit "$commit"
	mb_counts=$("$SCRIPT_DIR/merge_bases_count.sh" "$hash1" "$hash2")
	test "$mb_counts" = "$nb_mb" ||
		git replace -d "$commit"
done <"$TMP_MERGE_COMMITS"

rm "$TMP_MERGE_COMMITS"

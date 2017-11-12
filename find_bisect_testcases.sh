#!/bin/sh
#
# Find test cases candidates when bisect goes through
# many merge cases

NB_COMMITS="$1"

COUNT_LOG="my_count_log.txt"
SCRIPT_DIR="$(pwd)"

touch "$COUNT_LOG"

while :
do
  randnum=$(eval "shuf -i 1-$NB_COMMITS -n 2")
  commit1="$(echo "$randnum" | tail -1)"
  commit2="$(echo "$randnum" | head -1)"
  hash1=$(eval "git rev-list master -$NB_COMMITS | sed -n $commit1""p")
  hash2=$(eval "git rev-list master -$NB_COMMITS | sed -n $commit2""p")

  mb_num=$("$SCRIPT_DIR/merge_bases_count.sh" "$hash1" "$hash2")
  if test "$mb_num" -gt 2 
  then
    echo "$hash1 $hash2 : $mb_num" >> "$COUNT_LOG"
  fi
done

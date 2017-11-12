#!/bin/sh
#
# Count merge bases with bisect

trap "git bisect reset" INT

hash1="$1"
hash2="$2"

BISECT_LOG="my_bisect_log.txt"

git bisect start "$hash1" "$hash2" --no-checkout > "$BISECT_LOG"
count=0

if grep -q "Some good revs are not ancestors of the bad rev" "$BISECT_LOG"
then
  return 0
fi

while grep -q "merge base must be tested" "$BISECT_LOG"
do
  git bisect good > "$BISECT_LOG"
  count=$(($count + 1))
done

echo "$count"

rm "$BISECT_LOG"

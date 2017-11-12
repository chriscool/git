#!/bin/sh
#
# Count merge bases with bisect

BISECT_LOG="my_bisect_log.txt"
# export LC_ALL=C
touch $BISECT_LOG

hash1=$1
hash2=$2

git bisect start --no-checkout $hash1 $hash2 > $BISECT_LOG
count=0

while grep -q "merge base must be tested" $BISECT_LOG
do
  git bisect good > $BISECT_LOG
  count=$(($count + 1))
done

echo "$count"

#git bisect reset >/dev/null 2>&1
rm $BISECT_LOG

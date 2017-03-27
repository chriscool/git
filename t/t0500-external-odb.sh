#!/bin/sh

test_description='basic tests for external object databases'

. ./test-lib.sh

ALT_SOURCE="$PWD/alt-repo/.git"
export ALT_SOURCE
write_script odb-helper <<\EOF
GIT_DIR=$ALT_SOURCE; export GIT_DIR
case "$1" in
init)
	echo "capability=get_git_obj"
	echo "capability=have"
	;;
have)
	git cat-file --batch-check --batch-all-objects |
	awk '{print $1 " " $3 " " $2}'
	;;
get_git_obj)
	cat "$GIT_DIR"/objects/$(echo $2 | sed 's#..#&/#')
	;;
esac
EOF
HELPER="\"$PWD\"/odb-helper"

test_expect_success 'setup alternate repo' '
	git init alt-repo &&
	test_commit -C alt-repo one &&
	test_commit -C alt-repo two &&
	alt_head=$(git -C alt-repo rev-parse HEAD)
'

test_expect_success 'alt objects are missing' '
	test_must_fail git log --format=%s $alt_head
'

test_expect_success 'helper can retrieve alt objects' '
	test_config odb.magic.scriptCommand "$HELPER" &&
	cat >expect <<-\EOF &&
	two
	one
	EOF
	git log --format=%s $alt_head >actual &&
	test_cmp expect actual
'

test_done

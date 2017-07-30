#!/bin/sh

test_description='basic tests for external object databases'

. ./test-lib.sh

ALT_SOURCE="$PWD/alt-repo/.git"
export ALT_SOURCE
write_script odb-helper <<\EOF
die() {
	printf >&2 "%s\n" "$@"
	exit 1
}
GIT_DIR=$ALT_SOURCE; export GIT_DIR
case "$1" in
init)
	echo "capability=get_git_obj"
	echo "capability=put_raw_obj"
	echo "capability=have"
	;;
have)
	git cat-file --batch-check --batch-all-objects |
	awk '{print $1 " " $3 " " $2}'
	;;
get_git_obj)
	cat "$GIT_DIR"/objects/$(echo $2 | sed 's#..#&/#')
	;;
put_raw_obj)
	sha1="$2"
	size="$3"
	kind="$4"
	writen=$(git hash-object -w -t "$kind" --stdin)
	test "$writen" = "$sha1" || die "bad sha1 passed '$sha1' vs writen '$writen'"
	;;
*)
	die "unknown command '$1'"
	;;
esac
EOF
HELPER="\"$PWD\"/odb-helper"

test_expect_success 'setup alternate repo' '
	git init alt-repo &&
	(cd alt-repo &&
	 test_commit one &&
	 test_commit two
	) &&
	alt_head=`cd alt-repo && git rev-parse HEAD`
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

test_expect_success 'helper can add objects to alt repo' '
	hash=$(echo "Hello odb!" | git hash-object -w -t blob --stdin) &&
	test -f .git/objects/$(echo $hash | sed "s#..#&/#") &&
	size=$(git cat-file -s "$hash") &&
	git cat-file blob "$hash" | ./odb-helper put_raw_obj "$hash" "$size" blob &&
	alt_size=$(cd alt-repo && git cat-file -s "$hash") &&
	test "$size" -eq "$alt_size"
'

test_expect_success 'commit adds objects to alt repo' '
	test_config odb.magic.scriptCommand "$HELPER" &&
	echo "* odb=magic" >.gitattributes &&
	GIT_NO_EXTERNAL_ODB=1 git add .gitattributes &&
	GIT_NO_EXTERNAL_ODB=1 git commit -m "Add .gitattributes" &&
	test_commit three &&
	hash3=$(git ls-tree HEAD | grep three.t | cut -f1 | cut -d\  -f3) &&
	content=$(cd alt-repo && git show "$hash3") &&
	test "$content" = "three"
'

test_done

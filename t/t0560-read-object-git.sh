#!/bin/sh

test_description='tests for long running read-object process passing git objects'

. ./test-lib.sh

PATH="$PATH:$TEST_DIRECTORY/t0560"

test_expect_success 'setup host repo with a root commit' '
	test_commit zero &&
	hash1=$(git ls-tree HEAD | grep zero.t | cut -f1 | cut -d\  -f3)
'

HELPER="read-object-git"

test_expect_success 'blobs can be retrieved from the host repo' '
	git init guest-repo &&
	(cd guest-repo &&
	 git config odb.magic.subprocessCommand "$HELPER" &&
	 git cat-file blob "$hash1" >/dev/null)
'

test_expect_success 'invalid blobs generate errors' '
	cd guest-repo &&
	test_must_fail git cat-file blob "invalid"
'

test_done

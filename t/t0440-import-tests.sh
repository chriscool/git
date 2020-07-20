#!/bin/sh
#
# Copyright (c) 2020 Christian Couder
#

test_description='Test import and export commands'

. ./test-lib.sh

PATH="$TEST_DIRECTORY/t0440:$PATH"

test_expect_success 'setup remote server repository' '
	git init remote_server &&
	(cd remote_server &&
	 echo content >file &&
	 git add file &&
	 git commit -m one)
'

test_expect_success 'simple import' '
	(cd remote_server &&
	 git fast-export HEAD >../expect) &&
	(cd remote_server &&
	 my_git_export.sh HEAD >../actual) &&
	test_cmp expect actual
'

test_expect_success 'setup many blobs in remote server' '
	(cd remote_server &&
	 echo "other content" >otherfile &&
	 git add otherfile &&
	 echo "yet other content" >yetotherfile &&
	 git add yetotherfile &&
	 git commit -m two)
'

test_expect_success 'import many blobs' '
	(cd remote_server &&
	 git fast-export HEAD >../expect) &&
	(cd remote_server &&
	 my_git_export.sh HEAD >../actual) &&
	test_cmp expect actual
'

test_done

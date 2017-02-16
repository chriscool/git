#!/bin/sh

test_description='test clone with --initial-refspec option'
. ./test-lib.sh


test_expect_success 'setup regular repo' '
	# Make two branches, "master" and "side"
	echo one >file &&
	git add file &&
	git commit -m one &&
	echo two >file &&
	git commit -a -m two &&
	git tag two &&
	echo three >file &&
	git commit -a -m three &&
	git checkout -b side &&
	echo four >file &&
	git commit -a -m four &&
	git checkout master
'

test_expect_success 'add a special ref pointing to a blob' '
	hash=$(echo "Hello world!" | git hash-object -w -t blob --stdin) &&
	git update-ref refs/special/hello "$hash"
'

test_expect_success 'no-local clone from the first repo' '
	mkdir my-clone &&
	(cd my-clone &&
	 git clone --no-local .. . &&
	 test_must_fail git cat-file blob "$hash") &&
	rm -rf my-clone
'

test_expect_success 'no-local clone with --initial-refspec' '
	mkdir my-clone &&
	(cd my-clone &&
	 git clone --no-local --initial-refspec "refs/special/*:refs/special/*" .. . &&
	 git cat-file blob "$hash" &&
	 git rev-parse refs/special/hello >actual &&
	 echo "$hash" >expected &&
	 test_cmp expected actual) &&
	rm -rf my-clone
'

test_done


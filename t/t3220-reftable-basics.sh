#!/bin/sh
#
# Copyright (c) 2019 Christian Couder
#

test_description='check reftable format'

. ./test-lib.sh

test_expect_success 'simple setup with 1 branch and 2 tags' '
	test_commit first &&
	test_commit second &&
	test-tool reftable write-reftable reftable &&
	test-tool reftable read-reftable reftable >raw-actual &&
	sort raw-actual >actual &&
	echo "refs/heads/master" >>expected &&
	echo "refs/tags/first" >>expected &&
	echo "refs/tags/second" >>expected &&
	test_cmp expected actual
'

test_expect_success 'add many branches' '
	for i in $(test_seq 1 5000)
	do
		git checkout -b "br$i" || break
		echo "refs/heads/br$i" >>raw-expected || break
	done &&
	echo "refs/heads/master" >>raw-expected &&
	echo "refs/tags/first" >>raw-expected &&
	echo "refs/tags/second" >>raw-expected &&
	sort raw-expected >expected &&
	test-tool reftable write-reftable reftable &&
	test-tool reftable read-reftable reftable >raw-actual &&
	sort raw-actual >actual &&
	test_cmp expected actual
'

test_done

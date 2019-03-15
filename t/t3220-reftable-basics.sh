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
	echo "refs/heads/master $(git rev-parse master)" >>expected &&
	echo "refs/tags/first $(git rev-parse first)" >>expected &&
	echo "refs/tags/second $(git rev-parse second)" >>expected &&
	test_cmp expected actual
'

test_expect_success 'add many branches' '
	HASH=$(git rev-parse master) &&
	for i in $(test_seq 1 2000)
	do
		git checkout -b "br$i" || break
		echo "refs/heads/br$i $HASH" >>raw-expected || break
	done &&
	echo "refs/heads/master $HASH" >>raw-expected &&
	echo "refs/tags/first $(git rev-parse first)" >>raw-expected &&
	echo "refs/tags/second $(git rev-parse second)" >>raw-expected &&
	sort raw-expected >expected &&
	test-tool reftable write-reftable reftable &&
	test-tool reftable read-reftable reftable >raw-actual &&
	sort raw-actual >actual &&
	test_cmp expected actual
'

test_done

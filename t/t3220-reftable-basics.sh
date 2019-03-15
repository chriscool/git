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

test_done

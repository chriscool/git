#!/bin/sh
#
# Copyright (c) 2019 Christian Couder
#

test_description='check reftable format'

. ./test-lib.sh

first_commit=
second_commit=
master_commit=

test_expect_success 'simple setup with 1 branch and 2 tags' '
	test_commit first &&
	first_commit=$(git rev-parse first) &&
	test_commit second &&
	second_commit=$(git rev-parse second) &&
	master_commit=$(git rev-parse master)
'

test_expect_success 'check writing and reading reftable' '
	test-tool reftable write-reftable reftable &&
	test-tool reftable read-reftable reftable >raw-actual &&
	sort raw-actual >actual &&
	echo "refs/heads/master $master_commit" >>expected &&
	echo "refs/tags/first $first_commit" >>expected &&
	echo "refs/tags/second $second_commit" >>expected &&
	test_cmp expected actual
'

second_a_tag=

test_expect_success 'add 1 annotated tags' '
	git tag -a -m "annotated second" annotated_second master &&
	test-tool reftable write-reftable reftable &&
	test-tool reftable read-reftable reftable >raw-actual &&
	sort raw-actual >actual &&
	second_a_tag=$(git rev-parse annotated_second) &&
	echo "refs/heads/master $master_commit" >expected &&
	echo "refs/tags/annotated_second $second_a_tag $second_commit" >>expected &&
	echo "refs/tags/first $first_commit" >>expected &&
	echo "refs/tags/second $second_commit" >>expected &&
	test_cmp expected actual
'

test_expect_success 'add many branches' '
	for i in $(test_seq 1 2000)
	do
		git checkout -b "br$i" || break
		echo "refs/heads/br$i $master_commit" >>raw-expected || break
	done &&
	echo "refs/heads/master $master_commit" >>raw-expected &&
	echo "refs/tags/annotated_second $second_a_tag $second_commit" >>raw-expected &&
	echo "refs/tags/first $first_commit" >>raw-expected &&
	echo "refs/tags/second $second_commit" >>raw-expected &&
	sort raw-expected >expected &&
	test-tool reftable write-reftable reftable &&
	test-tool reftable read-reftable reftable >raw-actual &&
	sort raw-actual >actual &&
	test_cmp expected actual
'

test_done

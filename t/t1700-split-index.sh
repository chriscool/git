#!/bin/sh

test_description='split index mode tests'

. ./test-lib.sh

# We need total control of index splitting here
sane_unset GIT_TEST_SPLIT_INDEX

test_expect_success 'enable split index' '
	git config splitIndex.maxPercentChange 100 &&
	GIT_TRACE=2 git update-index --split-index &&
	test-dump-split-index .git/index >actual &&
	indexversion=$(test-index-version <.git/index) &&
	if test "$indexversion" = "4"
	then
		own=432ef4b63f32193984f339431fd50ca796493569
		base=508851a7f0dfa8691e9f69c7f055865389012491
	else
		own=8299b0bcd1ac364e5f1d7768efb62fa2da79a339
		base=39d890139ee5356c7ef572216cebcd27aa41f9df
	fi &&
	cat >expect <<-EOF &&
	own $own
	base $base
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'add one file' '
	: >one &&
	GIT_TRACE=2 git update-index --add one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	base $base
	100644 $EMPTY_BLOB 0	one
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'disable split index' '
	GIT_TRACE=2 git update-index --no-split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	BASE=$(test-dump-split-index .git/index | grep "^own" | sed "s/own/base/") &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	not a split index
	EOF
	test_cmp expect actual
'

test_expect_success 'enable split index again, "one" now belongs to base index"' '
	GIT_TRACE=2 git update-index --split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'modify original file, base index untouched' '
	echo modified >one &&
	GIT_TRACE=2 git update-index one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<-EOF &&
	$BASE
	100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0Q
	replacements: 0
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'add another file, which stays index' '
	: >two &&
	GIT_TRACE=2 git update-index --add two &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0	one
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<-EOF &&
	$BASE
	100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0Q
	100644 $EMPTY_BLOB 0	two
	replacements: 0
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'remove file not in base index' '
	GIT_TRACE=2 git update-index --force-remove two &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<-EOF &&
	$BASE
	100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0Q
	replacements: 0
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'remove file in base index' '
	GIT_TRACE=2 git update-index --force-remove one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions: 0
	EOF
	test_cmp expect actual
'

test_expect_success 'add original file back' '
	: >one &&
	GIT_TRACE=2 git update-index --add one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	100644 $EMPTY_BLOB 0	one
	replacements:
	deletions: 0
	EOF
	test_cmp expect actual
'

test_expect_success 'add new file' '
	: >two &&
	GIT_TRACE=2 git update-index --add two &&
	git ls-files --stage >actual &&
	cat >expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp expect actual
'

test_expect_success 'unify index, two files remain' '
	GIT_TRACE=2 git update-index --no-split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	not a split index
	EOF
	test_cmp expect actual
'

test_expect_success 'set core.splitIndex config variable to true' '
	git config core.splitIndex true &&
	: >three &&
	GIT_TRACE=2 git update-index --add three &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	one
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	three
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&
	BASE=$(test-dump-split-index .git/index | grep "^base") &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'set core.splitIndex config variable to false' '
	git config core.splitIndex false &&
	GIT_TRACE=2 git update-index --force-remove three &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	one
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	not a split index
	EOF
	test_cmp expect actual
'

test_expect_success 'set core.splitIndex config variable to true' '
	git config core.splitIndex true &&
	: >three &&
	GIT_TRACE=2 git update-index --add three &&
	BASE=$(test-dump-split-index .git/index | grep "^base") &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual &&
	: >four &&
	GIT_TRACE=2 git update-index --add four &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	four
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'check behavior with splitIndex.maxPercentChange unset' '
	git config --unset splitIndex.maxPercentChange &&
	: >five &&
	GIT_TRACE=2 git update-index --add five &&
	BASE=$(test-dump-split-index .git/index | grep "^base") &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual &&
	: >six &&
	GIT_TRACE=2 git update-index --add six &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	six
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'check splitIndex.maxPercentChange set to 0' '
	git config splitIndex.maxPercentChange 0 &&
	: >seven &&
	GIT_TRACE=2 git update-index --add seven &&
	BASE=$(test-dump-split-index .git/index | grep "^base") &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual &&
	: >eight &&
	GIT_TRACE=2 git update-index --add eight &&
	BASE=$(test-dump-split-index .git/index | grep "^base") &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'shared index files expire after 7 days by default' '
	: >ten &&
	GIT_TRACE=2 git update-index --add ten &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	just_under_7_days_ago=$((5-7*86400)) &&
	test-chmtime =$just_under_7_days_ago .git/sharedindex.* &&
	: >eleven &&
	GIT_TRACE=2 git update-index --add eleven &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	just_over_7_days_ago=$((-1-7*86400)) &&
	test-chmtime =$just_over_7_days_ago .git/sharedindex.* &&
	: >twelve &&
	GIT_TRACE=2 git update-index --add twelve &&
	test $(ls .git/sharedindex.* | wc -l) -le 2
'

test_expect_success 'check splitIndex.sharedIndexExpire set to 8 days' '
	git config splitIndex.sharedIndexExpire "8.days.ago" &&
	test-chmtime =$just_over_7_days_ago .git/sharedindex.* &&
	: >thirteen &&
	GIT_TRACE=2 git update-index --add thirteen &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	just_over_8_days_ago=$((-1-8*86400)) &&
	test-chmtime =$just_over_8_days_ago .git/sharedindex.* &&
	: >fourteen &&
	GIT_TRACE=2 git update-index --add fourteen &&
	test $(ls .git/sharedindex.* | wc -l) -le 2
'

test_expect_success 'check splitIndex.sharedIndexExpire set to "never" and "now"' '
	git config splitIndex.sharedIndexExpire never &&
	just_10_years_ago=$((-365*10*86400)) &&
	test-chmtime =$just_10_years_ago .git/sharedindex.* &&
	: >fifteen &&
	GIT_TRACE=2 git update-index --add fifteen &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	git config splitIndex.sharedIndexExpire now &&
	just_1_second_ago=-1 &&
	test-chmtime =$just_1_second_ago .git/sharedindex.* &&
	: >sixteen &&
	GIT_TRACE=2 git update-index --add sixteen &&
	test $(ls .git/sharedindex.* | wc -l) -le 2
'

test_done

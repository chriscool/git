#!/bin/sh
#
# Copyright (c) 2018 Phillip Wood
#

test_description='git rebase interactive amend'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

EMPTY=""

test_commit_message () {
	rev="$1"  # commit or tag we want to test
	str="$2"  # test against a string, or
	file="$3" # test against the content of a file
	git show --no-patch --pretty=format:%B >actual-message &&
	test -n "$str" && {
		printf "%s\n" "$str" >tmp-expected-message
		file="tmp-expected-message"
	}
	test_cmp "$file" actual-message
}

test_expect_success 'setup' '
	cat >message <<-EOF &&
		amend! B
		${EMPTY}
		new subject
		${EMPTY}
		new
		body
		EOF

	sed "1,2d" message >expected-message &&

	test_commit A A &&
	test_commit B B &&
	git log -1 --pretty=format:"%al %an %aE" >expected-author &&
	ORIG_AUTHOR_NAME="$GIT_AUTHOR_NAME" &&
	ORIG_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" &&
	GIT_AUTHOR_NAME="Amend Author" &&
	GIT_AUTHOR_EMAIL="amend@example.com" &&
	test_commit "$(cat message)" A A1 A1 &&
	test_commit A2 A &&
	test_commit A3 A &&
	GIT_AUTHOR_NAME="$ORIG_AUTHOR_NAME" &&
	GIT_AUTHOR_EMAIL="$ORIG_AUTHOR_EMAIL" &&
	git checkout -b conflicts-branch A &&
	test_commit conflicts A &&

	set_fake_editor &&
	git checkout -b branch B &&
	echo B1 >B &&
	test_tick &&
	git commit --fixup=HEAD -a &&
	test_tick &&
	git commit --allow-empty -F - <<-EOF &&
		amend! B
		${EMPTY}
		B
		${EMPTY}
		edited 1
		EOF
	test_tick &&
	git commit --allow-empty -F - <<-EOF &&
		amend! amend! B
		${EMPTY}
		B
		${EMPTY}
		edited 1
		${EMPTY}
		edited 2
		EOF
	echo B2 >B &&
	test_tick &&
	FAKE_COMMIT_AMEND="edited squash" git commit --squash=HEAD -a &&
	echo B3 >B &&
	test_tick &&
	git commit -a -F - <<-EOF &&
		amend! amend! amend! B
		${EMPTY}
		B
		${EMPTY}
		edited 1
		${EMPTY}
		edited 2
		${EMPTY}
		edited 3
		EOF

	GIT_AUTHOR_NAME="Rebase Author" &&
	GIT_AUTHOR_EMAIL="rebase.author@example.com" &&
	GIT_COMMITTER_NAME="Rebase Committer" &&
	GIT_COMMITTER_EMAIL="rebase.committer@example.com"
'

test_expect_success 'simple amend works' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A2 &&
	FAKE_LINES="1 amend 2" git rebase -i B &&
	test_cmp_rev HEAD^ B &&
	test_cmp_rev HEAD^{tree} A2^{tree} &&
	test_commit_message HEAD "A2"
'

test_expect_success 'amend removes amend! from message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A1 &&
	FAKE_LINES="1 amend 2" git rebase -i A &&
	test_cmp_rev HEAD^ A &&
	test_cmp_rev HEAD^{tree} A1^{tree} &&
	test_commit_message HEAD "" expected-message &&
	git log -1 --pretty=format:"%al %an %aE" >actual-author &&
	test_cmp expected-author actual-author
'

test_expect_success 'amend with conflicts gives correct message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A1 &&
	test_must_fail env FAKE_LINES="1 amend 2" git rebase -i conflicts &&
	git checkout --theirs -- A &&
	git add A &&
	FAKE_COMMIT_AMEND=edited git rebase --continue &&
	test_cmp_rev HEAD^ conflicts &&
	test_cmp_rev HEAD^{tree} A1^{tree} &&
	test_write_lines "" edited >>expected-message &&
	test_commit_message HEAD "" expected-message &&
	git log -1 --pretty=format:"%al %an %aE" >actual-author &&
	test_cmp expected-author actual-author
'

test_expect_success 'skipping amend after fixup gives correct message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A3 &&
	test_must_fail env FAKE_LINES="1 fixup 2 amend 4" git rebase -i A &&
	git reset --hard &&
	FAKE_COMMIT_AMEND=edited git rebase --continue &&
	test_commit_message HEAD "B"
'

test_expect_success 'sequence of fixup, amend & squash --signoff works' '
	git checkout --detach branch &&
	FAKE_LINES="1 fixup 2 amend 3 amend 4 squash 5 amend 6" \
		FAKE_COMMIT_AMEND=squashed \
		FAKE_MESSAGE_COPY=actual-squash-message \
		git -c commit.status=false rebase -ik --signoff A &&
	git diff-tree --exit-code --patch HEAD branch -- &&
	test_cmp_rev HEAD^ A &&
	test_i18ncmp "$TEST_DIRECTORY/t3437/expected-squash-message" \
		actual-squash-message
'

test_expect_success 'first amend commented out in sequence fixup amend amend' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout branch && git checkout --detach branch~2 &&
	git log -1 --pretty=format:%b >expected-message &&
	FAKE_LINES="1 fixup 2 amend 3 amend 4" git rebase -i A &&
	test_cmp_rev HEAD^ A &&
	test_commit_message HEAD "" expected-message
'

test_done

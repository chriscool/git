#!/bin/sh

test_description="Test core.fsmonitor"

. ./perf-lib.sh

# This has to be run with GIT_PERF_REPEAT_COUNT=1 to generate valid results.
# Otherwise the caching that happens for the nth run will negate the validity
# of the comparisons.
if test "$GIT_PERF_REPEAT_COUNT" -ne 1
then
	echo "warning: This test must be run with GIT_PERF_REPEAT_COUNT=1 to generate valid results." >&2
	echo "warning: Setting GIT_PERF_REPEAT_COUNT=1" >&2
	GIT_PERF_REPEAT_COUNT=1
fi

test_perf_large_repo
test_checkout_worktree

# Convert unix style paths to escaped Windows style paths for Watchman
case "$(uname -s)" in
MINGW*|MSYS_NT*)
  GIT_WORK_TREE="$(cygpath -aw "$PWD" | sed 's,\\,/,g')"
  ;;
*)
  GIT_WORK_TREE="$PWD"
  ;;
esac

# The big win for using fsmonitor is the elimination of the need to scan
# the working directory looking for changed files and untracked files. If
# the file information is all cached in RAM, the benefits are reduced.

flush_disk_cache () {
	case "$(uname -s)" in
	MINGW*|MSYS_NT*)
	  sync && test-drop-caches
	  ;;
	*)
	  sudo sync && echo 3 | sudo tee /proc/sys/vm/drop_caches
	  ;;
	esac

}

test_lazy_prereq UNTRACKED_CACHE '
	{ git update-index --test-untracked-cache; ret=$?; } &&
	test $ret -ne 1
'

test_expect_success "setup" '
	# Maybe set untrackedCache & splitIndex depending on the environment
	if test -n "$GIT_PERF_7519_UNTRACKED_CACHE"
	then
		git config core.untrackedCache "$GIT_PERF_7519_UNTRACKED_CACHE"
	else
		if test_have_prereq UNTRACKED_CACHE
		then
			git config core.untrackedCache true
		else
			git config core.untrackedCache false
		fi
	fi &&

	if test -n "$GIT_PERF_7519_SPLIT_INDEX"
	then
		git config core.splitIndex "$GIT_PERF_7519_SPLIT_INDEX"
	fi &&

	# Hook scaffolding
	mkdir .git/hooks &&
#	write_script .git/hooks/query-fsmonitor<<-\EOF &&
#	EOF
	cp "$TEST_DIRECTORY/../templates/hooks--query-fsmonitor.sample" .git/hooks/query-fsmonitor &&

	# have Watchman monitor the test folder
	watchman watch "$GIT_WORK_TREE" &&
	watchman watch-list | grep -q -F "$GIT_WORK_TREE"
'

# Cold file system cache without fsmonitor
test_expect_success "turn off fsmonitor, clear cache" '
	git config core.fsmonitor false &&
	flush_disk_cache
'
test_perf "status (fsmonitor=false, cold cache)" '
	git status
'

# Cold file system cache with fsmonitor
test_expect_success "turn on fsmonitor, clear cache" '
	git config core.fsmonitor true &&
	git status &&
	flush_disk_cache
'
test_perf "status (fsmonitor=true, cold cache)" '
	git status
'

# Warm file system cache without fsmonitor
test_expect_success "turn off fsmonitor, populate cache" '
	git config core.fsmonitor false &&
	git status
'
test_perf "status (fsmonitor=false, warm cache)" '
	git status
'

# Warm file system cache with fsmonitor
test_expect_success "turn on fsmonitor, populate cache" '
	git config core.fsmonitor true &&
	git status
'
test_perf "status (fsmonitor=true, warm cache)" '
	git status
'

# Let's see if -uno & -uall make any difference
test_expect_success "turn off fsmonitor, clear cache" '
	git config core.fsmonitor false &&
	flush_disk_cache
'
test_perf "status -uno (fsmonitor=false, cold cache)" '
	git status -uno
'

test_expect_success "turn on fsmonitor, clear cache" '
	git config core.fsmonitor true &&
	git status &&
	flush_disk_cache
'
test_perf "status -uno (fsmonitor=true, cold cache)" '
	git status -uno
'

test_expect_success "turn off fsmonitor, clear cache" '
	git config core.fsmonitor false &&
	flush_disk_cache
'
test_perf "status -uall (fsmonitor=false, cold cache)" '
	git status -uall
'

test_expect_success "turn on fsmonitor, clear cache" '
	git config core.fsmonitor true &&
	git status &&
	flush_disk_cache
'
test_perf "status -uall (fsmonitor=true, cold cache)" '
	git status -uall
'

test_expect_success "cleanup" '
	watchman watch-del "$GIT_WORK_TREE"
'

test_done

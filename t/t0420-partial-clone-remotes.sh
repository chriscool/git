#!/bin/sh

test_description='partial clone'

. ./test-lib.sh

PATH="$TEST_DIRECTORY/t0420:$PATH"

test_expect_success 'fetching of missing objects' '
	rm -rf repo &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server repack -a -d --write-bitmap-index &&

	git clone "file://$(pwd)/server" repo &&
	HASH=$(git -C repo rev-parse foo) &&
	rm -rf repo/.git/objects/* &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&
	git -C repo cat-file -p "$HASH" &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	git verify-pack --verbose "$IDX" >out &&
	grep "$HASH" out
'

test_expect_success 'fetching of missing objects works with ref-in-want enabled' '
	# ref-in-want requires protocol version 2
	git -C server config protocol.version 2 &&
	git -C server config uploadpack.allowrefinwant 1 &&
	git -C repo config protocol.version 2 &&

	rm -rf repo/.git/objects/* &&
	rm -f trace &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C repo cat-file -p "$HASH" &&
	grep "git< fetch=.*ref-in-want" trace
'

test_expect_success 'fetching of missing objects from another promisor remote' '
	git clone "file://$(pwd)/server" server2 &&
	test_commit -C server2 bar &&
	git -C server2 repack -a -d --write-bitmap-index &&
	HASH2=$(git -C server2 rev-parse bar) &&

	git -C repo remote add server2 "testgit::${PWD}/server2" &&
	git -C repo config remote.server2.promisor true &&
	git -C repo cat-file -p "$HASH2" &&

	git -C repo fetch server2 &&
	rm -rf repo/.git/objects/*
'

exit 1


test_expect_success 'fetching of missing objects from another promisor remote' '

 &&
	git -C repo cat-file -p "$HASH2"
 &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	git verify-pack --verbose "$IDX" >out &&
	grep "$HASH2" out
'

test_expect_success 'fetching of missing objects configures a promisor remote' '
	git clone "file://$(pwd)/server" server3 &&
	test_commit -C server3 baz &&
	git -C server3 repack -a -d --write-bitmap-index &&
	HASH3=$(git -C server3 rev-parse baz) &&
	git -C server3 config uploadpack.allowfilter 1 &&

	rm repo/.git/objects/pack/pack-*.promisor &&

	git -C repo remote add server3 "file://$(pwd)/server3" &&
	git -C repo fetch --filter="blob:none" server3 $HASH3 &&

	test_cmp_config -C repo true remote.server3.promisor &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	git verify-pack --verbose "$IDX" >out &&
	grep "$HASH3" out
'

test_expect_success 'fetching of missing blobs works' '
	rm -rf server server2 repo &&
	rm -rf server server3 repo &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server repack -a -d --write-bitmap-index &&

	git clone "file://$(pwd)/server" repo &&
	git hash-object repo/foo.t >blobhash &&
	rm -rf repo/.git/objects/* &&

	git -C server config uploadpack.allowanysha1inwant 1 &&
	git -C server config uploadpack.allowfilter 1 &&
	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&

	git -C repo cat-file -p $(cat blobhash)
'

test_expect_success 'fetching of missing trees does not fetch blobs' '
	rm -rf server repo &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server repack -a -d --write-bitmap-index &&

	git clone "file://$(pwd)/server" repo &&
	git -C repo rev-parse foo^{tree} >treehash &&
	git hash-object repo/foo.t >blobhash &&
	rm -rf repo/.git/objects/* &&

	git -C server config uploadpack.allowanysha1inwant 1 &&
	git -C server config uploadpack.allowfilter 1 &&
	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&
	git -C repo cat-file -p $(cat treehash) &&

	# Ensure that the tree, but not the blob, is fetched
	git -C repo rev-list --objects --missing=print $(cat treehash) >objects &&
	grep "^$(cat treehash)" objects &&
	grep "^[?]$(cat blobhash)" objects
'

test_done

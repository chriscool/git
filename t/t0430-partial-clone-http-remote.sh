#!/bin/sh

test_description='partial clone with http promisor remote'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd apache-e-odb.conf

# helper scripts must see this
export HTTPD_URL

PATH="$TEST_DIRECTORY/t0430:$PATH"

test_expect_success 'fetching of missing objects' '
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

UPLOADFILENAME="hello_apache_upload.txt"

UPLOAD_URL="$HTTPD_URL/upload/?sha1=$UPLOADFILENAME&size=123&type=blob"

test_expect_success 'can upload a file' '
	echo "Hello Apache World!" >hello_to_send.txt &&
	echo "How are you?" >>hello_to_send.txt &&
	curl --data-binary @hello_to_send.txt --include "$UPLOAD_URL" >out_upload
'

LIST_URL="$HTTPD_URL/list/"

test_expect_success 'can list uploaded files' '
	curl --include "$LIST_URL" >out_list &&
	grep "$UPLOADFILENAME" out_list
'

test_expect_success 'can delete uploaded files' '
	curl --data "delete" --include "$UPLOAD_URL&delete=1" >out_delete &&
	curl --include "$LIST_URL" >out_list2 &&
	! grep "$UPLOADFILENAME" out_list2
'

test_expect_success 'fetching of missing blob from a plain http promisor remote' '
	git clone "file://$(pwd)/server" server2 &&
	test_commit -C server2 bar &&
	git -C server2 repack -a -d --write-bitmap-index &&
	HASH2=$(git -C server2 rev-parse bar:bar.t) &&

	HASH2_SIZE=$(git -C server2 cat-file -s "$HASH2") &&
	UPLOAD_URL="$HTTPD_URL/upload/?sha1=$HASH2&size=$HASH2_SIZE&type=blob" &&
	git -C server2 cat-file blob "$HASH2" >object &&
	curl --data-binary @- --include "$UPLOAD_URL" <object >out_upload &&

	git -C repo remote add server2 "testhttpgit::${PWD}/server2" &&
	git -C repo config remote.server2.promisor true &&
	git -C repo cat-file -p "$HASH2"
'

test_done

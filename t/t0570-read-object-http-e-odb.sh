#!/bin/sh

test_description='tests for read-object process passing plain objects to an HTTPD server'

. ./test-lib.sh

# If we don't specify a port, the current test number will be used
# which will not work as it is less than 1024, so it can only be used by root.
LIB_HTTPD_PORT=$(expr ${this_test#t} + 12000)

. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd apache-e-odb.conf

PATH="$PATH:$TEST_DIRECTORY/t0570"

# odb helper script must see this
export HTTPD_URL

HELPER="read-object-plain"

test_expect_success 'setup repo with a root commit' '
	test_commit zero
'

test_expect_success 'setup another repo from the first one' '
	git init other-repo &&
	(cd other-repo &&
	 git remote add origin .. &&
	 git pull origin master &&
	 git checkout master &&
	 git log)
'

test_expect_success 'setup the helper in the root repo' '
	git config odb.magic.subprocessCommand "$HELPER"
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

FILES_DIR="httpd/www/files"

test_expect_success 'new blobs are transfered to the http server' '
	test_commit one &&
	hash1=$(git ls-tree HEAD | grep one.t | cut -f1 | cut -d\  -f3) &&
	echo "$hash1-4-blob" >expected &&
	ls "$FILES_DIR" >actual &&
	test_cmp expected actual
'

test_expect_success 'blobs can be retrieved from the http server' '
	git cat-file blob "$hash1" &&
	git log -p >expected
'

test_expect_success 'update other repo from the first one' '
	(cd other-repo &&
	 git fetch origin "refs/odbs/magic/*:refs/odbs/magic/*" &&
	 test_must_fail git cat-file blob "$hash1" &&
	 git config odb.magic.subprocesscommand "$HELPER" &&
	 git cat-file blob "$hash1" &&
	 git pull origin master)
'

test_expect_success 'local clone from the first repo' '
	mkdir my-clone &&
	(cd my-clone &&
	 git clone .. . &&
	 git cat-file blob "$hash1")
'

test_expect_success 'no-local clone from the first repo fails' '
	mkdir my-other-clone &&
	(cd my-other-clone &&
	 test_must_fail git clone --no-local .. .) &&
	rm -rf my-other-clone
'

test_expect_success 'no-local clone from the first repo with helper succeeds' '
	mkdir my-other-clone &&
	(cd my-other-clone &&
	 git clone -c odb.magic.subprocessCommand="$HELPER" --no-local .. .) &&
	rm -rf my-other-clone
'

stop_httpd

test_done

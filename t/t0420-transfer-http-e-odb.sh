#!/bin/sh

test_description='tests for transfering external objects to an HTTPD server'

. ./test-lib.sh

# If we don't specify a port, the current test number will be used
# which will not work as it is less than 1024, so it can only be used by root.
LIB_HTTPD_PORT=$(expr ${this_test#t} + 12000)

. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd apache-e-odb.conf

# odb helper script must see this
export HTTPD_URL

write_script odb-http-helper <<\EOF
die() {
	printf >&2 "%s\n" "$@"
	exit 1
}
echo >&2 "odb-http-helper args:" "$@"
case "$1" in
have)
	list_url="$HTTPD_URL/list/"
	curl "$list_url" ||
	die "curl '$list_url' failed"
	;;
get)
	get_url="$HTTPD_URL/list/?sha1=$2"
	curl "$get_url" ||
	die "curl '$get_url' failed"
	;;
put)
	sha1="$2"
	size="$3"
	kind="$4"
	upload_url="$HTTPD_URL/upload/?sha1=$sha1&size=$size&type=$kind"
	curl --data-binary @- --include "$upload_url" >out ||
	die "curl '$upload_url' failed"
	ref_hash=$(echo "$sha1 $size $kind" | GIT_NO_EXTERNAL_ODB=1 git hash-object -w -t blob --stdin) || exit
	git update-ref refs/odbs/magic/"$sha1" "$ref_hash"
	;;
*)
	die "unknown command '$1'"
	;;
esac
EOF
HELPER="\"$PWD\"/odb-http-helper"


test_expect_success 'setup repo with a root commit and the helper' '
	test_commit zero &&
	git config odb.magic.command "$HELPER" &&
	git config odb.magic.plainObjects "true"
'

test_expect_success 'setup another repo from the first one' '
	git init other-repo &&
	(cd other-repo &&
	 git remote add origin .. &&
	 git pull origin master &&
	 git checkout master &&
	 git log)
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
	 git config odb.magic.command "$HELPER" &&
	 git config odb.magic.plainObjects "true" &&
	 git cat-file blob "$hash1" &&
	 git pull origin master)
'

test_expect_success 'clone from the first repo' '
	mkdir my-clone &&
	(cd my-clone &&
	 git clone .. . &&
	 git cat-file blob "$hash1")
'


stop_httpd

test_done

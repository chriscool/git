#!/bin/sh

test_description='tests for cloning using a bundle through e-odb'

. ./test-lib.sh

# If we don't specify a port, the current test number will be used
# which will not work as it is less than 1024, so it can only be used by root.
LIB_HTTPD_PORT=$(expr ${this_test#t} + 12000)

. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd apache-e-odb.conf

# odb helper script must see this
export HTTPD_URL

write_script odb-clone-bundle-helper <<\EOF
die() {
	printf >&2 "%s\n" "$@"
	exit 1
}
echo >&2 "odb-clone-bundle-helper args:" "$@"
case "$1" in
init)
	ref_hash=$(git rev-parse refs/odbs/magic/bundle) ||
	die "couldn't find refs/odbs/magic/bundle"
	GIT_NO_EXTERNAL_ODB=1 git cat-file blob "$ref_hash" >bundle_info ||
	die "couldn't get blob $ref_hash"
	bundle_url=$(sed -e 's/bundle url: //' bundle_info)
	echo >&2 "bundle_url: '$bundle_url'"
	curl "$bundle_url" -o bundle_file ||
	die "curl '$bundle_url' failed"
	GIT_NO_EXTERNAL_ODB=1 git bundle unbundle bundle_file >unbundling_info ||
	die "unbundling 'bundle_file' failed"
	;;
get*)
	die "odb-clone-bundle-helper '$1' called"
	;;
put*)
	die "odb-clone-bundle-helper '$1' called"
	;;
*)
	die "unknown command '$1'"
	;;
esac
EOF
HELPER="\"$PWD\"/odb-clone-bundle-helper"


test_expect_success 'setup repo with a few commits' '
	test_commit one &&
	test_commit two &&
	test_commit three &&
	test_commit four
'

BUNDLE_FILE="file.bundle"
FILES_DIR="httpd/www/files"
GET_URL="$HTTPD_URL/files/$BUNDLE_FILE"

test_expect_success 'create a bundle for this repo and check that it can be downloaded' '
	git bundle create "$BUNDLE_FILE" master &&
	mkdir "$FILES_DIR" &&
	cp "$BUNDLE_FILE" "$FILES_DIR/" &&
	curl "$GET_URL" --output actual &&
	test_cmp "$BUNDLE_FILE" actual
'

test_expect_success 'create an e-odb ref for this bundle' '
	ref_hash=$(echo "bundle url: $GET_URL" | GIT_NO_EXTERNAL_ODB=1 git hash-object -w -t blob --stdin) &&
	git update-ref refs/odbs/magic/bundle "$ref_hash"
'

test_expect_success 'clone using the e-odb helper to download and install the bundle' '
	mkdir my-clone &&
	(cd my-clone &&
	 git clone --no-local \
		-c odb.magic.scriptCommand="$HELPER" \
		--initial-refspec "refs/odbs/magic/*:refs/odbs/magic/*" .. .)
'

stop_httpd

test_done

#!/bin/sh

test_description='test oidmap'
. ./test-lib.sh

# This purposefully is very similar to t0011-hashmap.sh

test_oidmap() {
	echo "$1" | test-tool oidmap $3 > actual &&
	echo "$2" >expect &&
	test_cmp expect actual
}


test_expect_success 'setup' '

	test_commit one &&
	test_commit two &&
	test_commit three &&
	test_commit four

'

test_oidhash() {
	git rev-parse "$1" | perl -ne 'print hex("$4$3$2$1") . "\n" if m/^(..)(..)(..)(..).*/;'
}

test_expect_success PERL 'hash' '

test_oidmap "hash one
hash two
hash invalidOid
hash three" "$(test_oidhash one)
$(test_oidhash two)
Unknown oid: invalidOid
$(test_oidhash three)"

'

test_expect_success 'put' '

test_oidmap "put one 1
put two 2
put invalidOid 4
put three 3" "NULL
NULL
Unknown oid: invalidOid
NULL"

'

test_expect_success 'replace' '

test_oidmap "put one 1
put two 2
put three 3
put invalidOid 4
put two deux
put one un" "NULL
NULL
NULL
Unknown oid: invalidOid
2
1"

'

test_expect_success 'get' '

test_oidmap "put one 1
put two 2
put three 3
get two
get four
get invalidOid
get one" "NULL
NULL
NULL
2
NULL
Unknown oid: invalidOid
1"

'

test_expect_success 'iterate' '

test_oidmap "put one 1
put two 2
put three 3
iterate" "NULL
NULL
NULL
$(git rev-parse two) 2
$(git rev-parse one) 1
$(git rev-parse three) 3"

'

test_done

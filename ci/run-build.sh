#!/bin/sh
#
# Build Git
#

. ${0%/*}/lib-travisci.sh

make $MAKE_OPTS --jobs=2

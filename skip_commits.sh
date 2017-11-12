#!/bin/sh
#
# Skip some commits.

commit="$1"

test $(git cat-file -p "$commit" | egrep "^parent " | wc -l) -eq 1 && {
	parent=$(git cat-file -p "$commit" | perl -ne 'print if s/^parent //')
	found=0
	while test $(git cat-file -p "$parent" | egrep "^parent " | wc -l) -eq 1
	do
		parent=$(git cat-file -p "$parent" | perl -ne 'print if s/^parent //')
		found=1
	done
	test "$found" -eq 1 &&
		GIT_EDITOR="perl -pi -e 's/^parent .*\$//sm; s/^tree (.*)\$/tree \\1\nparent $parent/;' " git replace -f --edit "$commit"
}

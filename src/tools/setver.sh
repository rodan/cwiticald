#!/bin/sh

build=$((`grep BUILD version.h | gsed 's|.*BUILD\s\([0-9]\{1,9\}\).*|\1|'`+1))
gsed  -i "s|^.*BUILD.*$|#define BUILD ${build}|" version.h

commit=$(git log | grep -c '^commit')
gsed  -i "s|^.*COMMIT.*$|#define COMMIT ${commit}|" version.h

compile_date=`date -u`

gsed -i "s|^// compiled on.*|// compiled on ${compile_date}|" version.h


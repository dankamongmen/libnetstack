#!/bin/sh

# requires https://pypi.org/project/githubrelease/

set -e

usage() { echo "usage: `basename $0` oldversion newversion quip" ; }

[ $# -eq 3 ] || { usage >&2 ; exit 1 ; }

OLDVERSION="$1"
VERSION="$2"
QUIP="$3"

git clean -f -d -x

# Doing general context-free regexery has led several times to heartache. We
# thus do tightly-coupled, context-sensitive seds for each class of files.
# Please don't add version numbers where they're not necessary.

# quick sanity checks before and after version update
grep $OLDVERSION CMakeLists.txt > /dev/null || { echo "Couldn't find OLDVERSION ($OLDVERSION) in CMakeLists.txt" >&2 ; exit 1 ; }
sed -i -e "s/\(project(netstack VERSION \)$OLDVERSION/\1$VERSION/" CMakeLists.txt
grep $VERSION CMakeLists.txt > /dev/null || { echo "Couldn't find VERSION ($VERSION) in CMakeLists.txt" >&2 ; exit 1 ; }

BUILDDIR="build-$VERSION"

# do a sanity-check build
mkdir "$BUILDDIR"
cd "$BUILDDIR"
cmake ..
make -j
make test
cd ..

# if that all worked, commit, push, and tag
git commit -a -m v$VERSION
git push
git pull
git tag -a v$VERSION -m v$VERSION -s
git push origin --tags
git pull

#!/bin/sh

echo -n cleaning up .

# Clean up the generated crud
(
    cd config/autoconf
    for FILE in compile config.guess config.sub depcomp install-sh ltmain.sh missing mkinstalldirs; do
	if test -f $FILE; then
	    rm -f $FILE
	fi
	echo -n .
    done
)

for FILE in aclocal.m4 configure config/config.h.in; do
    if test -f $FILE; then
	rm -f $FILE
    fi
	echo -n .
done

for DIR in autom4te.cache; do
    if test -d $DIR; then
	rm -rf $DIR
    fi
	echo -n .
done

find . -type f -name 'Makefile.in' -print0 | xargs -r0  rm -f --
find . -type f -name 'Makefile' -print0 | xargs -r0 rm -f --

echo ' done'

if test "x${1}" = "xclean"; then
    exit
fi

aclocal
libtoolize --force --copy
autoheader
automake --add-missing --copy --gnu -Wall
autoconf -Wall

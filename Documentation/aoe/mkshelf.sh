#! /bin/sh

if test "$#" != "2"; then
	echo "Usage: sh mkshelf.sh {dir} {shelfaddress}" 1>&2
	exit 1
fi
dir=$1
shelf=$2
MAJOR=152

set -e

minor=`echo 10 \* $shelf \* 16 | bc`
for slot in `seq 0 9`; do
	for part in `seq 0 15`; do
		name=e$shelf.$slot
		test "$part" != "0" && name=${name}p$part
		rm -f $dir/$name
		mknod -m 0660 $dir/$name b $MAJOR $minor

		minor=`expr $minor + 1`
	done
done

#!/bin/sh

n_shelves=10

if test "$#" != "1"; then
	echo "Usage: sh mkdevs.sh {dir}" 1>&2
	exit 1
fi
dir=$1

MAJOR=152

echo "Creating AoE devnode files in $dir ..."

set -e

mkdir -p $dir

# (Status info is in sysfs.  See status.sh.)
# rm -f $dir/stat
# mknod -m 0400 $dir/stat c $MAJOR 1
rm -f $dir/err
mknod -m 0400 $dir/err c $MAJOR 2
rm -f $dir/discover
mknod -m 0200 $dir/discover c $MAJOR 3
rm -f $dir/interfaces
mknod -m 0200 $dir/interfaces c $MAJOR 4

i=0
while test $i -lt $n_shelves; do
	sh -xc "sh `dirname $0`/mkshelf.sh $dir $i"
	i=`expr $i + 1`
done

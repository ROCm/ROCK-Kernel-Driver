#!/bin/sh
#
# arch/s390x/boot/install.sh
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Linus Torvalds
#
# Adapted from code in arch/i386/boot/Makefile by H. Peter Anvin
#
# "make install" script for s390 architecture
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - kernel type file
#   $5 - default install path (blank if root directory)
#

# User may have a custom install script

if [ -x ~/bin/installkernel ]; then exec ~/bin/installkernel "$@"; fi
if [ -x /sbin/installkernel ]; then exec /sbin/installkernel "$@"; fi

# Default install - same as make zlilo

if [ -f $5/vmlinuz ]; then
	mv $5/vmlinuz $5/vmlinuz.old
fi

if [ -f $5/System.map ]; then
	mv $5/System.map $5/System.old
fi

cat $2 > $5/vmlinuz
cp $3 $5/System.map

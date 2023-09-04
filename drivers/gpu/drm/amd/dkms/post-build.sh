#!/bin/bash

KERNELVER=$1

#
# Restore original kernel 5.x and Kernel 4.x scripts/Makefile.build modified by post-add.sh
#
if [ ${KERNELVER%%.*} -eq 5  -o  ${KERNELVER%%.*} -eq 4 ]; then
	moddir="/lib/modules/$KERNELVER"
	mkfile="scripts/Makefile.build"

	if [[ -d "$moddir/source" ]]; then
		mkfile="$moddir/source/$mkfile"
	else
		mkfile="$moddir/build/$mkfile"
	fi

	mkfile=$(readlink -f $mkfile)

	if [[ -f "$mkfile~" ]]; then
		mv -f $mkfile{~,}
	fi
fi

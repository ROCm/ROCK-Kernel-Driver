#!/bin/sh
#
# include/asm-sh/machtype.h header generation script for SuperH
#
# Copyright (C) 2003 Paul Mundt
#
# This is pretty much a quick and dirty hack based off of the awk
# script written by rmk that ARM uses to achieve the same sort of
# thing.
#
# Unfortunately this script has a dependance on bash/sed/cut/tr,
# though they should be prevalent enough for this dependancy not
# to matter overly much.
#
# As a note for anyone attempting to manually invoke this script,
# it expects to be run straight out of the arch/sh/tools/ directory
# as it doesn't look at TOPDIR to figure out where asm-sh is
# located.
#
# See the note at the top of the generated header for additional
# information.
#
# Released under the terms of the GNU GPL v2.0.
#

[ $# -ne 1 ] && echo "Usage: $0 <mach defs>" && exit 1

cat << EOF > tmp.h
/*
 * Automagically generated, don't touch.
 */
#ifndef __ASM_SH_MACHTYPES_H
#define __ASM_SH_MACHTYPES_H

#include <linux/config.h>

/*
 * We'll use the following MACH_xxx defs for placeholders for the time
 * being .. these will all go away once sh_machtype is assigned per-board.
 *
 * For now we leave things the way they are for backwards compatibility.
 */

/* Mach types */
EOF

newline='
'
IFS=$newline

rm -f tmp2.h

for entry in `cat $1 | sed -e 's/\#.*$//g;/^$/d' | tr '\t' ' ' | tr -s ' '`; do
	board=`echo $entry | cut -f1 -d' '`

	printf "#ifdef CONFIG_`echo $entry | cut -f2 -d' '`\n" >> tmp.h
	printf "  #define MACH_$board\t\t1\n#else\n  #define MACH_$board\t\t0\n#endif\n" >> tmp.h
	printf "#define mach_is_`echo $board | tr '[A-Z]' '[a-z]'`()\t\t\t(MACH_$board)\n" >> tmp2.h
done

printf "\n/* Machtype checks */\n" >> tmp.h
cat tmp2.h >> tmp.h && rm -f tmp2.h

cat << EOF >> tmp.h

#endif /* __ASM_SH_MACHTYPES_H */
EOF

cat tmp.h
rm -f tmp.h


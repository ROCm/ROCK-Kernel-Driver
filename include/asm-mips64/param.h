/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 1994 - 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PARAM_H
#define _ASM_PARAM_H

#ifndef HZ
#define HZ 100
#  define HZ 100
#ifdef __KERNEL__
#  define hz_to_std(a) (a)
#endif
#endif

#define EXEC_PAGESIZE	4096

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#ifdef __KERNEL__
# define CLOCKS_PER_SEC	100	/* frequency at which times() counts */
#endif

#endif /* _ASM_PARAM_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 1994 - 2000, 2002 Ralf Baechle (ralf@gnu.org)
 * Copyright 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PARAM_H
#define _ASM_PARAM_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifdef CONFIG_DECSTATION
   /*
    * log2(HZ), change this here if you want another HZ value. This is also
    * used in dec_time_init.  Minimum is 1, Maximum is 15.
    */
#  define LOG_2_HZ 7
#  define HZ (1 << LOG_2_HZ)
#else
# define HZ		1000		/* Internal kernel timer frequency */
#endif
# define USER_HZ	100		/* .. some user interfaces are in "ticks" */
# define CLOCKS_PER_SEC	(USER_HZ)	/* like times() */
#endif

#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE	4096

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* _ASM_PARAM_H */

#ifndef _ASM_IA64_SN_ATE_UTILS_H
#define _ASM_IA64_SN_ATE_UTILS_H

/* $Id: ate_utils.h,v 1.1 2002/02/28 17:31:25 marcelo Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

/*
 *	struct map	X[]	.m_size		.m_addr
 *			---	------------	-----------
 *			[0]	mapsize(X)	unused
 *				# X[] unused
 *			[1]	map lock *	mapwant sv_t *
 *				map access	wait for free map space
 *
 *	  mapstart(X)-> [2]	# units		unit number
 *			 :	    :		  :
 *			[ ]	    0
 */

#include <linux/types.h>

#define ulong_t uint64_t

struct map
{
	unsigned long m_size;	/* number of units available */
	unsigned long m_addr;	/* address of first available unit */
};

#define mapstart(X)		&X[2]		/* start of map array */

#define mapsize(X)		X[0].m_size	/* number of empty slots */
						/* remaining in map array */
#define maplock(X)		(((spinlock_t *) X[1].m_size))

#define mapout(X)		((sv_t *) X[1].m_addr)


extern ulong_t atealloc(struct map *, size_t);
extern struct map *atemapalloc(ulong_t);
extern void atefree(struct map *, size_t, ulong_t);
extern void atemapfree(struct map *);

#endif /* _ASM_IA64_SN_ATE_UTILS_H  */


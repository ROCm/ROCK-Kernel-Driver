/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6int$#.c
 *
 * $#-way unrolled portable integer math RAID-6 instruction set
 *
 * This file is postprocessed using unroller.pl
 */

#include "raid6.h"

/*
 * IA-64 wants insane amounts of unrolling.  On other architectures that
 * is just a waste of space.
 */

#if ($# <= 8) || defined(_ia64__)

static void raid6_int$#_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	unative_t wd$$, wq$$, wp$$, w1$$, w2$$;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	for ( d = 0 ; d < bytes ; d += NSIZE*$# ) {
		wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE];
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			wp$$ ^= wd$$;
			w2$$ = wq$$ & NBYTES(0x80);
			w1$$ = (wq$$ << 1) & NBYTES(0xfe);
			w2$$ = (w2$$ << 1) - (w2$$ >> 7);
			w2$$ &= NBYTES(0x1d);
			w1$$ ^= w2$$;
			wq$$ = w1$$ ^ wd$$;
		}
		*(unative_t *)&p[d+NSIZE*$$] = wp$$;
		*(unative_t *)&q[d+NSIZE*$$] = wq$$;
	}
}

const struct raid6_calls raid6_intx$# = {
	raid6_int$#_gen_syndrome,
	NULL,		/* always valid */
	"int" NSTRING "x$#",
	0
};

#endif

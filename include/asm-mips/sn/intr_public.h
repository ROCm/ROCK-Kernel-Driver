/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997 Silicon Graphics, Inc.
 */
#ifndef __ASM_SN_INTR_PUBLIC_H
#define __ASM_SN_INTR_PUBLIC_H

#include <asm/sn/arch.h>

/*
 * The following are necessary to create the illusion of a CEL on the SN0 hub.
 * We'll add more priority levels soon, but for now, any interrupt in a
 * particular band effectively does an spl.  These must be in the PDA since
 * they're different for each processor.  Users of this structure must hold the
 * vector_lock in the appropriate vector block before modifying the mask arrays.
 * There's only one vector block for each Hub so a lock in the PDA wouldn't be
 * adequate.
 */
struct hub_intmasks_s {
	/*
	 * The masks are stored with the lowest-priority (most inclusive)
	 * in the lowest-numbered masks (i.e., 0, 1, 2...).
	 */
	hubreg_t	intpend0_masks;		/* INT_PEND0 */
	hubreg_t	intpend1_masks;		/* INT_PEND1 */
};

#endif /* __ASM_SN_INTR_PUBLIC_H */

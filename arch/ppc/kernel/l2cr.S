/*
	L2CR functions
	Copyright © 1997-1998 by PowerLogix R & D, Inc.
	
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
	Thur, Dec. 12, 1998.
	- First public release, contributed by PowerLogix.
	***********
	Sat, Aug. 7, 1999.
	- Terry: Made sure code disabled interrupts before running. (Previously
			it was assumed interrupts were already disabled).
	- Terry: Updated for tentative G4 support.  4MB of memory is now flushed
			instead of 2MB.  (Prob. only 3 is necessary).
	- Terry: Updated for workaround to HID0[DPM] processor bug
			during global invalidates.
	***********
	Thu, July 13, 2000.
	- Terry: Added isync to correct for an errata.
	
	Author:	Terry Greeniaus (tgree@phys.ualberta.ca)
	Please e-mail updates to this file to me, thanks!
*/
#include <asm/processor.h>
#include <asm/cputable.h>
#include "ppc_asm.h"

/* Usage:
	
	When setting the L2CR register, you must do a few special
	things.  If you are enabling the cache, you must perform a
	global invalidate.  If you are disabling the cache, you must
	flush the cache contents first.  This routine takes care of
	doing these things.  When first enabling the cache, make sure
	you pass in the L2CR you want, as well as passing in the
	global invalidate bit set.  A global invalidate will only be
	performed if the L2I bit is set in applyThis.  When enabling
	the cache, you should also set the L2E bit in applyThis.  If
	you want to modify the L2CR contents after the cache has been
	enabled, the recommended procedure is to first call
	__setL2CR(0) to disable the cache and then call it again with
	the new values for L2CR.  Examples:

	_setL2CR(0)		- disables the cache
	_setL2CR(0xB3A04000)	- enables my G3 upgrade card:
				- L2E set to turn on the cache
				- L2SIZ set to 1MB
				- L2CLK set to 1:1
				- L2RAM set to pipelined synchronous late-write
				- L2I set to perform a global invalidation
				- L2OH set to 0.5 nS
				- L2DF set because this upgrade card
				  requires it

	A similar call should work for your card.  You need to know
	the correct setting for your card and then place them in the
	fields I have outlined above.  Other fields support optional
	features, such as L2DO which caches only data, or L2TS which
	causes cache pushes from the L1 cache to go to the L2 cache
	instead of to main memory.
*/
/*
 * Summary: this procedure ignores the L2I bit in the value passed in,
 * flushes the cache if it was already enabled, always invalidates the
 * cache, then enables the cache if the L2E bit is set in the value
 * passed in.
 *   -- paulus.
 */
_GLOBAL(_set_L2CR)
	/* Make sure this is a 750 or 7400 chip */
BEGIN_FTR_SECTION
	li	r3,-1
	blr
END_FTR_SECTION_IFCLR(CPU_FTR_L2CR)

	/* Turn off interrupts and data relocation. */
	mfmsr	r7		/* Save MSR in r7 */
	rlwinm	r4,r7,0,17,15
	rlwinm	r4,r4,0,28,26	/* Turn off DR bit */
	sync
	mtmsr	r4
	isync

	/* Get the current enable bit of the L2CR into r4 */
	mfspr	r4,L2CR
	
	/* Tweak some bits */
	rlwinm	r5,r3,0,0,0		/* r5 contains the new enable bit */
	rlwinm	r3,r3,0,11,9		/* Turn off the invalidate bit */
	rlwinm	r3,r3,0,1,31		/* Turn off the enable bit */

	/* Check to see if we need to flush */
	rlwinm.	r4,r4,0,0,0
	beq	2f

	/* Flush the cache. First, read the first 4MB of memory (physical) to
	 * put new data in the cache.  (Actually we only need
	 * the size of the L2 cache plus the size of the L1 cache, but 4MB will
	 * cover everything just to be safe).
	 */
		
	 /**** Might be a good idea to set L2DO here - to prevent instructions
	       from getting into the cache.  But since we invalidate
	       the next time we enable the cache it doesn't really matter.
	  ****/

	lis	r4,0x0002
	mtctr	r4
	li	r4,0
1:
	lwzx	r0,r0,r4
	addi	r4,r4,32		/* Go to start of next cache line */
	bdnz	1b
	
	/* Now, flush the first 4MB of memory */
	lis	r4,0x0002
	mtctr	r4
	li	r4,0
	sync
1:
	dcbf	r0,r4
	addi	r4,r4,32		/* Go to start of next cache line */
	bdnz	1b

2:
	/* Set up the L2CR configuration bits (and switch L2 off) */
	sync
	mtspr	L2CR,r3
	sync

	/* Before we perform the global invalidation, we must disable dynamic
	 * power management via HID0[DPM] to work around a processor bug where
	 * DPM can possibly interfere with the state machine in the processor
	 * that invalidates the L2 cache tags.
	 */
	mfspr	r8,HID0			/* Save HID0 in r8 */
	rlwinm	r4,r8,0,12,10		/* Turn off HID0[DPM] */
	sync
	mtspr	HID0,r4			/* Disable DPM */
	sync

	/* Perform a global invalidation */
	oris	r3,r3,0x0020
	sync
	mtspr	L2CR,r3
	sync
	isync				/* For errata */

	/* Wait for the invalidation to complete */
3:	mfspr	r3,L2CR
	rlwinm.	r4,r3,0,31,31
	bne	3b
	
	rlwinm	r3,r3,0,11,9		/* Turn off the L2I bit */
	sync
	mtspr	L2CR,r3
	sync
	
	/* Restore HID0[DPM] to whatever it was before */
	sync
	mtspr	1008,r8
	sync

	/* See if we need to enable the cache */
	cmplwi	r5,0
	beq	4f

	/* Enable the cache */
	oris	r3,r3,0x8000
	mtspr	L2CR,r3
	sync

	/* Restore MSR (restores EE and DR bits to original state) */
4:	SYNC
	mtmsr	r7
	isync
	blr

_GLOBAL(_get_L2CR)
	/* Return the L2CR contents */
	li	r3,0
BEGIN_FTR_SECTION
	mfspr	r3,L2CR
END_FTR_SECTION_IFSET(CPU_FTR_L2CR)
	blr

/* --- End of PowerLogix code ---
 */

/*
 * include/asm-v850/rte_multi.c -- Support for Multi debugger monitor ROM
 * 	on Midas lab RTE-CB series of evaluation boards
 *
 *  Copyright (C) 2001,02,03  NEC Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/init.h>

#include <asm/machdep.h>

/* A table of which interrupt vectors to install, since blindly
   installing all of them makes the debugger stop working.  This is a
   list of offsets in the interrupt vector area; each entry means to
   copy that particular 16-byte vector.  An entry less than zero ends
   the table.  */
static long multi_intv_install_table[] = {
	0x40, 0x50,		/* trap vectors */
#ifdef CONFIG_RTE_CB_MULTI_DBTRAP
	0x60,			/* illegal insn / dbtrap */
#endif
	/* Note -- illegal insn trap is used by the debugger.  */
	0xD0, 0xE0, 0xF0,	/* GINT1 - GINT3 */
	0x240, 0x250, 0x260, 0x270, /* timer D interrupts */
	0x2D0, 0x2E0, 0x2F0,	/* UART channel 0 */
	0x310, 0x320, 0x330,	/* UART channel 1 */
	0x350, 0x360, 0x370,	/* UART channel 2 */
	-1
};

/* Early initialization for kernel using Multi debugger ROM monitor.  */
void __init multi_init (void)
{
	/* We're using the Multi debugger monitor, so we have to install
	   the interrupt vectors.  The monitor doesn't allow them to be
	   initially downloaded into their final destination because
	   it's in the monitor's scratch-RAM area.  Unfortunately, Multi
	   also doesn't deal correctly with ELF sections where the LMA
	   and VMA differ -- it just ignores the LMA -- so we can't use
	   that feature to work around the problem.  What we do instead
	   is just put the interrupt vectors into a normal section, and
	   do the necessary copying and relocation here.  Since the
	   interrupt vector basically only contains `jr' instructions
	   and no-ops, it's not that hard.  */
	extern unsigned long _intv_load_start, _intv_start;
	register unsigned long *src = &_intv_load_start;
	register unsigned long *dst = (unsigned long *)INTV_BASE;
	register unsigned long jr_fixup = (char *)&_intv_start - (char *)dst;
	register long *ii;

	/* Copy interrupt vectors as instructed by multi_intv_install_table. */
	for (ii = multi_intv_install_table; *ii >= 0; ii++) {
		/* Copy 16-byte interrupt vector at offset *ii.  */
		int boffs;
		for (boffs = 0; boffs < 0x10; boffs += sizeof *src) {
			/* Copy a single word, fixing up the jump offs
			   if it's a `jr' instruction.  */
			int woffs = (*ii + boffs) / sizeof *src;
			unsigned long word = src[woffs];

			if ((word & 0xFC0) == 0x780) {
				/* A `jr' insn, fix up its offset (and yes, the
				   wierd half-word swapping is intentional). */
				unsigned short hi = word & 0xFFFF;
				unsigned short lo = word >> 16;
				unsigned long udisp22
					= lo + ((hi & 0x3F) << 16);
				long disp22 = (long)(udisp22 << 10) >> 10;

				disp22 += jr_fixup;

				hi = ((disp22 >> 16) & 0x3F) | 0x780;
				lo = disp22 & 0xFFFF;

				word = hi + (lo << 16);
			}

			dst[woffs] = word;
		}
	}
}

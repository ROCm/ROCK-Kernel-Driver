/*
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 2001 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 2000 Sibyte
 *
 * Written by Justin Carlson (carlson@sibyte.com)
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/config.h>
#include <asm/page.h>

#ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
#define SB1_PREF_LOAD_STREAMED_HINT "0"
#define SB1_PREF_STORE_STREAMED_HINT "1"
#else
#define SB1_PREF_LOAD_STREAMED_HINT "4"
#define SB1_PREF_STORE_STREAMED_HINT "5"
#endif

/* These are the functions hooked by the memory management function pointers */
void sb1_clear_page(void *page)
{
	/*
	 * JDCXXX - This should be bottlenecked by the write buffer, but these
	 * things tend to be mildly unpredictable...should check this on the
	 * performance model
	 *
	 * We prefetch 4 lines ahead.  We're also "cheating" slightly here...
	 * since we know we're on an SB1, we force the assembler to take
	 * 64-bit operands to speed things up
	 */
	__asm__ __volatile__(
		".set push                  \n"
		".set noreorder             \n"
		".set noat                  \n"
		".set mips4                 \n"
		"     addiu     $1, %0, %2  \n"  /* Calculate the end of the page to clear */
#ifdef CONFIG_CPU_HAS_PREFETCH
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ",  0(%0)  \n"  /* Prefetch the first 4 lines */
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ", 32(%0)  \n"
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ", 64(%0)  \n"
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ", 96(%0)  \n"
#endif
		"1:   sd        $0,  0(%0)  \n"  /* Throw out a cacheline of 0's */
		"     sd        $0,  8(%0)  \n"
		"     sd        $0, 16(%0)  \n"
		"     sd        $0, 24(%0)  \n"
#ifdef CONFIG_CPU_HAS_PREFETCH
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ",128(%0)  \n"  /* Prefetch 4 lines ahead     */
#endif
		"     bne       $1, %0, 1b  \n"
		"     addiu     %0, %0, 32  \n"  /* Next cacheline (This instruction better be short piped!) */
		".set pop                   \n"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE-32)
		: "memory");

}

void sb1_copy_page(void *to, void *from)
{

	/*
	 * This should be optimized in assembly...can't use ld/sd, though,
	 * because the top 32 bits could be nuked if we took an interrupt
	 * during the routine.	And this is not a good place to be cli()'ing
	 *
	 * The pref's used here are using "streaming" hints, which cause the
	 * copied data to be kicked out of the cache sooner.  A page copy often
	 * ends up copying a lot more data than is commonly used, so this seems
	 * to make sense in terms of reducing cache pollution, but I've no real
	 * performance data to back this up
	 */

	__asm__ __volatile__(
		".set push                  \n"
		".set noreorder             \n"
		".set noat                  \n"
		".set mips4                 \n"
		"     addiu     $1, %0, %4  \n"  /* Calculate the end of the page to copy */
#ifdef CONFIG_CPU_HAS_PREFETCH
		"     pref       " SB1_PREF_LOAD_STREAMED_HINT  ",  0(%0)  \n"  /* Prefetch the first 3 lines */
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ",  0(%1)  \n"
		"     pref       " SB1_PREF_LOAD_STREAMED_HINT  ",  32(%0) \n"
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ",  32(%1) \n"
		"     pref       " SB1_PREF_LOAD_STREAMED_HINT  ",  64(%0) \n"
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ",  64(%1) \n"
#endif
		"1:   lw        $2,  0(%0)  \n"  /* Block copy a cacheline */
		"     lw        $3,  4(%0)  \n"
		"     lw        $4,  8(%0)  \n"
		"     lw        $5, 12(%0)  \n"
		"     lw        $6, 16(%0)  \n"
		"     lw        $7, 20(%0)  \n"
		"     lw        $8, 24(%0)  \n"
		"     lw        $9, 28(%0)  \n"
#ifdef CONFIG_CPU_HAS_PREFETCH
		"     pref       " SB1_PREF_LOAD_STREAMED_HINT  ", 96(%0)  \n"  /* Prefetch ahead         */
		"     pref       " SB1_PREF_STORE_STREAMED_HINT ", 96(%1)  \n"
#endif
		"     sw        $2,  0(%1)  \n"
		"     sw        $3,  4(%1)  \n"
		"     sw        $4,  8(%1)  \n"
		"     sw        $5, 12(%1)  \n"
		"     sw        $6, 16(%1)  \n"
		"     sw        $7, 20(%1)  \n"
		"     sw        $8, 24(%1)  \n"
		"     sw        $9, 28(%1)  \n"
		"     addiu     %1, %1, 32  \n"  /* Next cacheline */
		"     nop                   \n"  /* Force next add to short pipe */
		"     nop                   \n"  /* Force next add to short pipe */
		"     bne       $1, %0, 1b  \n"
		"     addiu     %0, %0, 32  \n"  /* Next cacheline */
		".set pop                   \n"
		: "=r" (to), "=r" (from)
		: "0" (from), "1" (to), "I" (PAGE_SIZE-32)
		: "$2","$3","$4","$5","$6","$7","$8","$9","memory");
}

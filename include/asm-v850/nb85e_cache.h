/*
 * include/asm-v850/nb85e_cache_cache.h -- Cache control for NB85E_CACHE212 and
 * 	NB85E_CACHE213 cache memories
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_NB85E_CACHE_H__
#define __V850_NB85E_CACHE_H__

/* Cache control registers.  */
#define NB85E_CACHE_ICC_ADDR		0xFFFFF070
#define NB85E_CACHE_DCC_ADDR		0xFFFFF078

/* Size of a cache line in bytes.  */
#define NB85E_CACHE_LINE_SIZE		16


#ifndef __ASSEMBLY__

extern inline void nb85e_cache_flush_cache (unsigned long cache_control_addr)
{
	/*
	   From the NB85E Instruction/Data Cache manual, how to flush
	   the instruction cache (ICC is the `Instruction Cache Control
	   Register'):

		mov	0x3, r2
	  LOP0:
		ld.h	ICC[r0], r1
		cmp	r0, r1
		bnz	LOP0
		st.h	r2, ICC[r0]
	  LOP1:				- First TAG clear
		ld.h	ICC[r0], r1
		cmp	r0, r1
		bnz	LOP1
		st.h	r2, ICC[r0]
	  LOP2:				- Second TAG clear
		ld.h	ICC[r0], r1
		cmp	r0, r1
		bnz	LOP2
	*/
	int cache_flush_bits, ccr_contents;
	__asm__ __volatile__ (
		"	mov	0x3, %1;"
		"1:	ld.h	0[%2], %0;"
		"	cmp	r0, %0;"
		"	bnz	1b;"
		"	st.h	%1, 0[%2];"
		"2:	ld.h	0[%2], %0;"
		"	cmp	r0, %0;"
		"	bnz	2b;"
		"	st.h	%1, 0[%2];"
		"3:	ld.h	0[%2], %0;"
		"	cmp	r0, %0;"
		"	bnz	3b"
		: "=&r" (ccr_contents), "=&r" (cache_flush_bits)
		: "r" (cache_control_addr)
		: "memory");
}

extern inline void nb85e_cache_flush_icache (void)
{
	nb85e_cache_flush_cache (NB85E_CACHE_ICC_ADDR);
}

extern inline void nb85e_cache_flush_dcache (void)
{
	nb85e_cache_flush_cache (NB85E_CACHE_DCC_ADDR);
}

extern inline void nb85e_cache_flush (void)
{
	nb85e_cache_flush_icache ();
	nb85e_cache_flush_dcache ();
}

#endif /* !__ASSEMBLY__ */


/* Define standard definitions in terms of processor-specific ones.  */

/* For <asm/cache.h> */
#define L1_CACHE_BYTES				NB85E_CACHE_LINE_SIZE

/* For <asm/pgalloc.h> */
#define flush_cache_all()			nb85e_cache_flush ()
#define flush_cache_mm(mm)			nb85e_cache_flush ()
#define flush_cache_range(mm, start, end)	nb85e_cache_flush ()
#define flush_cache_page(vma, vmaddr)		nb85e_cache_flush ()
#define flush_page_to_ram(page)			nb85e_cache_flush ()
#define flush_dcache_page(page)			nb85e_cache_flush_dcache ()
#define flush_icache_range(start, end)		nb85e_cache_flush_icache ()
#define flush_icache_page(vma,pg)		nb85e_cache_flush_icache ()
#define flush_icache()				nb85e_cache_flush_icache ()
#define flush_cache_sigtramp(vaddr)		nb85e_cache_flush_icache ()

#endif /* __V850_NB85E_CACHE_H__ */

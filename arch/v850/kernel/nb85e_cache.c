/*
 * arch/v850/kernel/nb85e_cache.c -- Cache control for NB85E_CACHE212 and
 * 	NB85E_CACHE213 cache memories
 *
 *  Copyright (C) 2003  NEC Electronics Corporation
 *  Copyright (C) 2003  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <asm/entry.h>
#include <asm/nb85e_cache.h>

#define WAIT_UNTIL_CLEAR(value) while (value) {}

/* Set caching params via the BHC and DCC registers.  */
void nb85e_cache_enable (u16 bhc, u16 dcc)
{
	unsigned long *r0_ram = (unsigned long *)R0_RAM_ADDR;
	register u16 bhc_val asm ("r6") = bhc;

	/* Configure data-cache.  */
	NB85E_CACHE_DCC = dcc;

	/* Configure caching for various memory regions by writing the BHC
	   register.  The documentation says that an instruction _cannot_
	   enable/disable caching for the memory region in which the
	   instruction itself exists; to work around this, we store
	   appropriate instructions into the on-chip RAM area (which is never
	   cached), and briefly jump there to do the work.  */
	r0_ram[0] = 0xf0720760;	/* st.h r0, 0xfffff072[r0] */
	r0_ram[1] = 0xf06a3760;	/* st.h r6, 0xfffff06a[r0] */
	r0_ram[2] = 0x5640006b;	/* jmp [r11] */
	asm ("mov hilo(1f), r11; jmp [%1]; 1:;"
	     :: "r" (bhc_val), "r" (R0_RAM_ADDR) : "r11");
}

static void clear_icache (void)
{
	/* 1. Read the instruction cache control register (ICC) and confirm
	      that bits 0 and 1 (TCLR0, TCLR1) are all cleared.  */
	WAIT_UNTIL_CLEAR (NB85E_CACHE_ICC & 0x3);

	/* 2. Read the ICC register and confirm that bit 12 (LOCK0) is
  	      cleared.  Bit 13 of the ICC register is always cleared.  */
	WAIT_UNTIL_CLEAR (NB85E_CACHE_ICC & 0x1000);

	/* 3. Set the TCLR0 and TCLR1 bits of the ICC register as follows,
	      when clearing way 0 and way 1 at the same time:
	        (a) Set the TCLR0 and TCLR1 bits.
		(b) Read the TCLR0 and TCLR1 bits to confirm that these bits
		    are cleared.
		(c) Perform (a) and (b) above again.  */
	NB85E_CACHE_ICC |= 0x3;
	WAIT_UNTIL_CLEAR (NB85E_CACHE_ICC & 0x3);
	/* Do it again.  */
	NB85E_CACHE_ICC |= 0x3;
	WAIT_UNTIL_CLEAR (NB85E_CACHE_ICC & 0x3);
}

/* Flush or clear (or both) the data cache, depending on the value of FLAGS;
   the procedure is the same for both, just the control bits used differ (and
   both may be performed simultaneously).  */
static void dcache_op (unsigned short flags)
{
	/* 1. Read the data cache control register (DCC) and confirm that bits
	      0, 1, 4, and 5 (DC00, DC01, DC04, DC05) are all cleared.  */
	WAIT_UNTIL_CLEAR (NB85E_CACHE_DCC & 0x33);

	/* 2. Clear DCC register bit 12 (DC12), bit 13 (DC13), or both
	      depending on the way for which tags are to be cleared.  */
	NB85E_CACHE_DCC &= ~0xC000;

	/* 3. Set DCC register bit 0 (DC00), bit 1 (DC01) or both depending on
	      the way for which tags are to be cleared.
	      ...
	      Set DCC register bit 4 (DC04), bit 5 (DC05), or both depending
	      on the way to be data flushed.  */
	NB85E_CACHE_DCC |= flags;

	/* 4. Read DCC register bit DC00, DC01 [DC04, DC05], or both depending
	      on the way for which tags were cleared [flushed] and confirm
	      that that bit is cleared.  */
	WAIT_UNTIL_CLEAR (NB85E_CACHE_DCC & flags);
}

/* Flushes the contents of the dcache to memory.  */
static inline void flush_dcache (void)
{
	/* We only need to do something if in write-back mode.  */
	if (NB85E_CACHE_DCC & 0x0400)
		dcache_op (0x30);
}

/* Flushes the contents of the dcache to memory, and then clears it.  */
static inline void clear_dcache (void)
{
	/* We only need to do something if the dcache is enabled.  */
	if (NB85E_CACHE_DCC & 0x0C00)
		dcache_op (0x33);
}

/* Clears the dcache without flushing to memory first.  */
static inline void clear_dcache_no_flush (void)
{
	/* We only need to do something if the dcache is enabled.  */
	if (NB85E_CACHE_DCC & 0x0C00)
		dcache_op (0x3);
}

static inline void cache_exec_after_store (void)
{
	flush_dcache ();
	clear_icache ();
}


/* Exported functions.  */

void inline nb85e_cache_flush_all (void)
{
	clear_icache ();
	clear_dcache ();
}

void nb85e_cache_flush_mm (struct mm_struct *mm)
{
	/* nothing */
}

void nb85e_cache_flush_range (struct mm_struct *mm,
			      unsigned long start, unsigned long end)
{
	/* nothing */
}

void nb85e_cache_flush_page (struct vm_area_struct *vma,
			     unsigned long page_addr)
{
	/* nothing */
}

void nb85e_cache_flush_dcache_page (struct page *page)
{
	/* nothing */
}

void nb85e_cache_flush_icache (void)
{
	cache_exec_after_store ();
}

void nb85e_cache_flush_icache_range (unsigned long start, unsigned long end)
{
	cache_exec_after_store ();
}

void nb85e_cache_flush_icache_page (struct vm_area_struct *vma,
				    struct page *page)
{
	cache_exec_after_store ();
}

void nb85e_cache_flush_icache_user_range (struct vm_area_struct *vma,
					  struct page *page,
					  unsigned long adr, int len)
{
	cache_exec_after_store ();
}

void nb85e_cache_flush_sigtramp (unsigned long addr)
{
	cache_exec_after_store ();
}

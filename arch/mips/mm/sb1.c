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


/*
 * In this entire file, I'm not sure what the role of the L2 on the sb1250 
 * is.  Since it is coherent to the system, we should never need to flush
 * it...right?...right???  -JDC
 */

#include <asm/mmu_context.h>

/* These are probed at ld_mmu time */
static unsigned int icache_size;
static unsigned int dcache_size;

static unsigned int icache_line_size;
static unsigned int dcache_line_size;

static unsigned int icache_assoc;
static unsigned int dcache_assoc;

static unsigned int icache_sets;
static unsigned int dcache_sets;
static unsigned int tlb_entries;

void pgd_init(unsigned long page)
{
	unsigned long *p = (unsigned long *) page;
	int i;

	for (i = 0; i < USER_PTRS_PER_PGD; i+=8) {
		p[i + 0] = (unsigned long) invalid_pte_table;
		p[i + 1] = (unsigned long) invalid_pte_table;
		p[i + 2] = (unsigned long) invalid_pte_table;
		p[i + 3] = (unsigned long) invalid_pte_table;
		p[i + 4] = (unsigned long) invalid_pte_table;
		p[i + 5] = (unsigned long) invalid_pte_table;
		p[i + 6] = (unsigned long) invalid_pte_table;
		p[i + 7] = (unsigned long) invalid_pte_table;
	}
}

void flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

	__save_and_cli(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = (get_entryhi() & 0xff);
	set_entrylo0(0);
	set_entrylo1(0);
	for (entry = 0; entry < tlb_entries; entry++) {
		set_entryhi(KSEG0 + (PAGE_SIZE << 1) * entry);
		set_index(entry);
		tlb_write_indexed();
	}
	set_entryhi(old_ctx);
	__restore_flags(flags);	
}



/* These are the functions hooked by the memory management function pointers */
static void sb1_clear_page(void *page)
{
	/* JDCXXX - This should be bottlenecked by the write buffer, but these
	   things tend to be mildly unpredictable...should check this on the
	   performance model */

	/* We prefetch 4 lines ahead.  We're also "cheating" slightly here...
	   since we know we're on an SB1, we force the assembler to take 
	   64-bit operands to speed things up */
	__asm__ __volatile__(
		".set push                  \n"
		".set noreorder             \n"
		".set noat                  \n"
		".set mips4                 \n"
		"     addiu     $1, %0, %2  \n"  /* Calculate the end of the page to clear */
		"     pref       5,  0(%0)  \n"  /* Prefetch the first 4 lines */
		"     pref       5, 32(%0)  \n"  
		"     pref       5, 64(%0)  \n"  
		"     pref       5, 96(%0)  \n"  
		"1:   sd        $0,  0(%0)  \n"  /* Throw out a cacheline of 0's */
		"     sd        $0,  8(%0)  \n"
		"     sd        $0, 16(%0)  \n"
		"     sd        $0, 24(%0)  \n"
		"     pref       5,128(%0)  \n"  /* Prefetch 4 lines ahead     */
		"     bne       $1, %0, 1b  \n"
		"     addiu     %0, %0, 32  \n"  /* Next cacheline (This instruction better be short piped!) */
		".set pop                   \n"
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE-32)
		:"$1","memory");

}

static void sb1_copy_page(void *to, void *from)
{

	/* This should be optimized in assembly...can't use ld/sd, though,
	 * because the top 32 bits could be nuked if we took an interrupt
	 * during the routine.  And this is not a good place to be cli()'ing
	 */

	/* The pref's used here are using "streaming" hints, which cause the
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
		"     pref       4,  0(%0)  \n"  /* Prefetch the first 3 lines to be read and copied */
		"     pref       5,  0(%1)  \n"  
		"     pref       4, 32(%0)  \n"  
		"     pref       5, 32(%1)  \n"  
		"     pref       4, 64(%0)  \n"  
		"     pref       5, 64(%1)  \n"  
		"1:   lw        $2,  0(%0)  \n"  /* Block copy a cacheline */
		"     lw        $3,  4(%0)  \n"
		"     lw        $4,  8(%0)  \n"
		"     lw        $5, 12(%0)  \n"
		"     lw        $6, 16(%0)  \n"
		"     lw        $7, 20(%0)  \n"
		"     lw        $8, 24(%0)  \n"
		"     lw        $9, 28(%0)  \n"
		"     pref       4, 96(%0)  \n"  /* Prefetch ahead         */
		"     pref       5, 96(%1)  \n"
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
		:"=r" (to),
		"=r" (from)
		:
		"0" (from),
		"1" (to),
		"I" (PAGE_SIZE-32)
		:"$1","$2","$3","$4","$5","$6","$7","$8","$9","memory");
/*
	unsigned long *src = from;
	unsigned long *dest = to;
	unsigned long *target = (unsigned long *) (((unsigned long)src) + PAGE_SIZE);
	while (src != target) {
		*dest++ = *src++;
	}
*/
}

/*
 * The dcache is fully coherent to the system, with one
 * big caveat:  the instruction stream.  In other words,
 * if we miss in the icache, and have dirty data in the
 * L1 dcache, then we'll go out to memory (or the L2) and
 * get the not-as-recent data.
 * 
 * So the only time we have to flush the dcache is when
 * we're flushing the icache.  Since the L2 is fully
 * coherent to everything, including I/O, we never have
 * to flush it
 */

static void sb1_flush_cache_all(void)
{

	/*
	 * Haven't worried too much about speed here; given that we're flushing
	 * the icache, the time to invalidate is dwarfed by the time it's going
	 * to take to refill it.  Register usage:
	 * 
	 * $1 - moving cache index
	 * $2 - set count
	 */
	if (icache_sets) { 
		__asm__ __volatile__ (
			".set push                  \n"
			".set noreorder             \n"
			".set noat                  \n"
			".set mips4                 \n"
			"     move   $1, %2         \n" /* Start at index 0 */
                        "1:   cache  0, 0($1)       \n" /* Invalidate this index */
			"     addiu  %1, %1, -1     \n" /* Decrement loop count */
			"     bnez   %1, 1b         \n" /* loop test */
			"     addu   $1, $1, %0     \n" /* Next address JDCXXX - Should be short piped */
			".set pop                   \n"
			::"r" (icache_line_size),
			"r" (icache_sets * icache_assoc),
			"r" (KSEG0)
			:"$1");
	}
	if (dcache_sets) {
		__asm__ __volatile__ (
			".set push                  \n"
			".set noreorder             \n"
			".set noat                  \n"
			".set mips4                 \n"
			"     move   $1, %2         \n" /* Start at index 0 */
                        "1:   cache  0x1, 0($1)     \n" /* WB/Invalidate this index */
			"     addiu  %1, %1, -1     \n" /* Decrement loop count */
			"     bnez   %1, 1b         \n" /* loop test */
			"     addu   $1, $1, %0     \n" /* Next address JDCXXX - Should be short piped */
			".set pop                   \n"
			::"r" (dcache_line_size),
			"r" (dcache_sets * dcache_assoc),
			"r" (KSEG0)
			:"$1");
	}
}

/*
 * When flushing a range in the icache, we have to first writeback
 * the dcache for the same range, so new ifetches will see any
 * data that was dirty in the dcache
 */

static void sb1_flush_icache_range(unsigned long start, unsigned long end)
{
	
	/* JDCXXX - Implement me! */
	sb1_flush_cache_all();
}

static void sb1_flush_cache_mm(struct mm_struct *mm)
{
	/* Don't need to do this, as the dcache is physically tagged */
}

static void sb1_flush_cache_range(struct mm_struct *mm, 
				  unsigned long start,
				  unsigned long end)
{
	/* Don't need to do this, as the dcache is physically tagged */
}


static void sb1_flush_cache_sigtramp(unsigned long page)
{
	/* JDCXXX - Implement me! */
	sb1_flush_cache_all();
}


/*
 * This only needs to make sure stores done up to this
 * point are visible to other agents outside the CPU.  Given 
 * the coherent nature of the ZBus, all that's required here is 
 * a sync to make sure the data gets out to the caches and is
 * visible to an arbitrary A Phase from an external agent 
 *   
 * Actually, I'm not even sure that's necessary; the semantics
 * of this function aren't clear.  If it's supposed to serve as
 * a memory barrier, this is needed.  If it's only meant to 
 * prevent data from being invisible to non-cpu memory accessors
 * for some indefinite period of time (e.g. in a non-coherent 
 * dcache) then this function would be a complete nop.
 */
static void sb1_flush_page_to_ram(struct page *page)
{
	__asm__ __volatile__(
		"     sync  \n"  /* Short pipe */
		:::"memory");	
}


/* Cribbed from the r2300 code */
static void sb1_flush_cache_page(struct vm_area_struct *vma,
				  unsigned long page)
{
	sb1_flush_cache_all();
#if 0
	struct mm_struct *mm = vma->vm_mm;
	unsigned long physpage;

	/* No icache flush needed without context; */
	if (mm->context == 0) 
		return;  

	/* No icache flush needed if the page isn't executable */
	if (!(vma->vm_flags & VM_EXEC))
		return;

	physpage = (unsigned long) page_address(page);
	if (physpage)
		sb1_flush_icache_range(physpage, physpage + PAGE_SIZE);
#endif
}


/*
 *  Cache set values (from the mips64 spec)
 * 0 - 64
 * 1 - 128
 * 2 - 256
 * 3 - 512
 * 4 - 1024
 * 5 - 2048
 * 6 - 4096
 * 7 - Reserved
 */
static unsigned int decode_cache_sets(unsigned int config_field)
{
	if (config_field == 7) {
		/* JDCXXX - Find a graceful way to abort. */
		return 0;
	} 

	return (1<<(config_field + 6));
}

/*
 *  Cache line size values (from the mips64 spec)
 * 0 - No cache present.
 * 1 - 4 bytes
 * 2 - 8 bytes
 * 3 - 16 bytes
 * 4 - 32 bytes
 * 5 - 64 bytes
 * 6 - 128 bytes
 * 7 - Reserved
 */
static unsigned int decode_cache_line_size(unsigned int config_field)
{
	if (config_field == 0) {
		return 0;
	} else if (config_field == 7) {
		/* JDCXXX - Find a graceful way to abort. */
		return 0;
	} 
	return (1<<(config_field + 1));
}

/*
 * Relevant bits of the config1 register format (from the MIPS32/MIPS64 specs)
 *
 * 24:22 Icache sets per way
 * 21:19 Icache line size
 * 18:16 Icache Associativity
 * 15:13 Dcache sets per way
 * 12:10 Dcache line size
 * 9:7   Dcache Associativity
 */


static void probe_cache_sizes(void)
{
	u32 config1;

	__asm__ __volatile__(
		".set push                  \n"
		".set mips64                \n"
		"     mfc0    %0, $16, 1    \n"  /* Get config1 register */
		".set pop                   \n"
		:"=r" (config1));
	icache_line_size = decode_cache_line_size((config1 >> 19) & 0x7);
	dcache_line_size = decode_cache_line_size((config1 >> 10) & 0x7);
	icache_sets = decode_cache_sets((config1 >> 22) & 0x7);
	dcache_sets = decode_cache_sets((config1 >> 13) & 0x7);
        icache_assoc = ((config1 >> 16) & 0x7) + 1;
	dcache_assoc = ((config1 >> 7) & 0x7) + 1;
	icache_size = icache_line_size * icache_sets * icache_assoc;
	dcache_size = dcache_line_size * dcache_sets * dcache_assoc;
	tlb_entries = ((config1 >> 25) & 0x3f) + 1;
}


/* This is called from loadmmu.c.  We have to set up all the
   memory management function pointers, as well as initialize
   the caches and tlbs */
void ld_mmu_sb1(void)
{
	probe_cache_sizes();

	_clear_page = sb1_clear_page;
	_copy_page = sb1_copy_page;
	
	_flush_cache_all = sb1_flush_cache_all;
	_flush_cache_mm = sb1_flush_cache_mm;
	_flush_cache_range = sb1_flush_cache_range;
	_flush_cache_page = sb1_flush_cache_page;
	_flush_cache_sigtramp = sb1_flush_cache_sigtramp;

	_flush_page_to_ram = sb1_flush_page_to_ram;
	_flush_icache_page = sb1_flush_cache_page;
	_flush_icache_range = sb1_flush_icache_range;

	
	/*
	 * JDCXXX I'm not sure whether these are necessary: is this the right 
	 * place to initialize the tlb?  If it is, why is it done 
	 * at this level instead of as common code in loadmmu()?
	 */
	flush_cache_all();
	flush_tlb_all();
	
	/* Turn on caching in kseg0 */
	set_cp0_config(CONF_CM_CMASK, 0);
}

/* $Id: cache.c,v 1.4 2000/01/25 00:11:38 prumpf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Helge Deller (07-13-1999)
 * Copyright (C) 1999 SuSE GmbH Nuernberg
 * Copyright (C) 2000 Philipp Rumpf (prumpf@tux.org)
 *
 * Cache and TLB management
 *
 */
 
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/pdc.h>
#include <asm/cache.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgalloc.h>

struct pdc_cache_info cache_info;
#ifndef __LP64__
static struct pdc_btlb_info btlb_info;
#endif


void __flush_page_to_ram(unsigned long address)
{
	__flush_dcache_range(address, PAGE_SIZE);
	__flush_icache_range(address, PAGE_SIZE);
}



void flush_data_cache(void)
{
	register unsigned long base   = cache_info.dc_base;
	register unsigned long count  = cache_info.dc_count;
	register unsigned long loop   = cache_info.dc_loop;
	register unsigned long stride = cache_info.dc_stride;
	register unsigned long addr;
	register long i, j;

	for(i=0,addr=base; i<count; i++,addr+=stride)
		for(j=0; j<loop; j++)
			fdce(addr);
}
		
static inline void flush_data_tlb_space(void)
{
	unsigned long base   = cache_info.dt_off_base;
	unsigned long count  = cache_info.dt_off_count;
	unsigned long stride = cache_info.dt_off_stride;
	unsigned long loop   = cache_info.dt_loop;

	unsigned long addr;
	long i,j;
	
	for(i=0,addr=base; i<count; i++,addr+=stride)
		for(j=0; j<loop; j++)
			pdtlbe(addr);
}



void flush_data_tlb(void)
{
	unsigned long base   = cache_info.dt_sp_base;
	unsigned long count  = cache_info.dt_sp_count;
	unsigned long stride = cache_info.dt_sp_stride;
	unsigned long space;
	unsigned long old_sr1;
	long i;
	
	old_sr1 = mfsp(1);
	
	for(i=0,space=base; i<count; i++, space+=stride) {
		mtsp(space,1);
		flush_data_tlb_space();
	}

	mtsp(old_sr1, 1);
}

static inline void flush_instruction_tlb_space(void)
{
	unsigned long base   = cache_info.it_off_base;
	unsigned long count  = cache_info.it_off_count;
	unsigned long stride = cache_info.it_off_stride;
	unsigned long loop   = cache_info.it_loop;

	unsigned long addr;
	long i,j;
	
	for(i=0,addr=base; i<count; i++,addr+=stride)
		for(j=0; j<loop; j++)
			pitlbe(addr);
}

void flush_instruction_tlb(void)
{
	unsigned long base   = cache_info.it_sp_base;
	unsigned long count  = cache_info.it_sp_count;
	unsigned long stride = cache_info.it_sp_stride;
	unsigned long space;
	unsigned long old_sr1;
	unsigned int i;
	
	old_sr1 = mfsp(1);
	
	for(i=0,space=base; i<count; i++, space+=stride) {
		mtsp(space,1);
		flush_instruction_tlb_space();
	}

	mtsp(old_sr1, 1);
}


void __flush_tlb_space(unsigned long space)
{
	unsigned long old_sr1;

	old_sr1 = mfsp(1);
	mtsp(space, 1);
	
	flush_data_tlb_space();
	flush_instruction_tlb_space();

	mtsp(old_sr1, 1);
}


void flush_instruction_cache(void)
{
	register unsigned long base   = cache_info.ic_base;
	register unsigned long count  = cache_info.ic_count;
	register unsigned long loop   = cache_info.ic_loop;
	register unsigned long stride = cache_info.ic_stride;
	register unsigned long addr;
	register long i, j;
	unsigned long old_sr1;
	
	old_sr1 = mfsp(1);
	mtsp(0,1);

	/*
	 * Note: fice instruction has 3 bit space field, so one must
	 *       be specified (otherwise you are justing using whatever
	 *       happens to be in sr0).
	 */

	for(i=0,addr=base; i<count; i++,addr+=stride)
		for(j=0; j<loop; j++)
			fice(addr);

	mtsp(old_sr1, 1);
}

/* not yet ... fdc() needs to be implemented in cache.h !
void flush_datacache_range( unsigned int base, unsigned int end )
{
    register long offset,offset_add;
    offset_add = ( (1<<(cache_info.dc_conf.cc_block-1)) * 
		    cache_info.dc_conf.cc_line ) << 4;
    for (offset=base; offset<=end; offset+=offset_add)
	fdc(space,offset);
    fdc(space,end);
}
*/

/* flushes code and data-cache */
void flush_all_caches(void)
{
	flush_instruction_cache();
	flush_data_cache();

	flush_instruction_tlb();
	flush_data_tlb();

	asm volatile("sync");
	asm volatile("syncdma");
	asm volatile("sync");
}

int get_cache_info(char *buffer)
{
	char *p = buffer;

	p += sprintf(p, "I-cache\t\t: %ld KB\n", 
		cache_info.ic_size/1024 );
	p += sprintf(p, "D-cache\t\t: %ld KB (%s)%s\n", 
		cache_info.dc_size/1024,
		(cache_info.dc_conf.cc_wt ? "WT":"WB"),
		(cache_info.dc_conf.cc_sh ? " - shared I/D":"")
	);

	p += sprintf(p, "ITLB entries\t: %ld\n" "DTLB entries\t: %ld%s\n",
		cache_info.it_size,
		cache_info.dt_size,
		cache_info.dt_conf.tc_sh ? " - shared with ITLB":""
	);
		
#ifndef __LP64__
	/* BTLB - Block TLB */
	if (btlb_info.max_size==0) {
		p += sprintf(p, "BTLB\t\t: not supported\n" );
	} else {
		p += sprintf(p, 
		"BTLB fixed\t: max. %d pages, pagesize=%d (%dMB)\n"
		"BTLB fix-entr.\t: %d instruction, %d data (%d combined)\n"
		"BTLB var-entr.\t: %d instruction, %d data (%d combined)\n",
		btlb_info.max_size, (int)4096,
		btlb_info.max_size>>8,
		btlb_info.fixed_range_info.num_i,
		btlb_info.fixed_range_info.num_d,
		btlb_info.fixed_range_info.num_comb, 
		btlb_info.variable_range_info.num_i,
		btlb_info.variable_range_info.num_d,
		btlb_info.variable_range_info.num_comb
		);
	}
#endif

	return p - buffer;
}


void __init 
cache_init(void)
{
	if(pdc_cache_info(&cache_info)<0)
		panic("cache_init: pdc_cache_info failed");

#if 0
	printk("ic_size %lx dc_size %lx it_size %lx pdc_cache_info %d*long pdc_cache_cf %d\n",
	    cache_info.ic_size,
	    cache_info.dc_size,
	    cache_info.it_size,
	    sizeof (struct pdc_cache_info) / sizeof (long),
	    sizeof (struct pdc_cache_cf)
	);
#endif
#ifndef __LP64__
	if(pdc_btlb_info(&btlb_info)<0) {
		memset(&btlb_info, 0, sizeof btlb_info);
	}
#endif
}

/* $Id: cache-sh3.c,v 1.5 2003/05/06 23:28:48 lethal Exp $
 *
 *  linux/arch/sh/mm/cache-sh3.c
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 * Copyright (C) 2002 Paul Mundt
 */

#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

static int __init
detect_cpu_and_cache_system(void)
{
	unsigned long addr0, addr1, data0, data1, data2, data3;

	jump_to_P2();
	/*
	 * Check if the entry shadows or not.
	 * When shadowed, it's 128-entry system.
	 * Otherwise, it's 256-entry system.
	 */
	addr0 = CACHE_OC_ADDRESS_ARRAY + (3 << 12);
	addr1 = CACHE_OC_ADDRESS_ARRAY + (1 << 12);

	/* First, write back & invalidate */
	data0  = ctrl_inl(addr0);
	ctrl_outl(data0&~(SH_CACHE_VALID|SH_CACHE_UPDATED), addr0);
	data1  = ctrl_inl(addr1);
	ctrl_outl(data1&~(SH_CACHE_VALID|SH_CACHE_UPDATED), addr1);

	/* Next, check if there's shadow or not */
	data0 = ctrl_inl(addr0);
	data0 ^= SH_CACHE_VALID;
	ctrl_outl(data0, addr0);
	data1 = ctrl_inl(addr1);
	data2 = data1 ^ SH_CACHE_VALID;
	ctrl_outl(data2, addr1);
	data3 = ctrl_inl(addr0);

	/* Lastly, invaliate them. */
	ctrl_outl(data0&~SH_CACHE_VALID, addr0);
	ctrl_outl(data2&~SH_CACHE_VALID, addr1);

	back_to_P1();

	cpu_data->dcache.ways		= 4;
	cpu_data->dcache.entry_shift	= 4;
	cpu_data->dcache.linesz		= L1_CACHE_BYTES;
	cpu_data->dcache.flags		= 0;

	/*
	 * 7709A/7729 has 16K cache (256-entry), while 7702 has only
	 * 2K(direct) 7702 is not supported (yet)
	 */
	if (data0 == data1 && data2 == data3) {	/* Shadow */
		cpu_data->dcache.way_shift	= 11;
		cpu_data->dcache.entry_mask	= 0x7f0;
		cpu_data->dcache.sets		= 128;
		cpu_data->type = CPU_SH7708;
	} else {				/* 7709A or 7729  */
		cpu_data->dcache.way_shift	= 12;
		cpu_data->dcache.entry_mask	= 0xff0;
		cpu_data->dcache.sets		= 256;
		cpu_data->type = CPU_SH7729;
	}

		/*
	 * SH-3 doesn't have separate caches
		 */
	cpu_data->dcache.flags |= SH_CACHE_COMBINED;
	cpu_data->icache = cpu_data->dcache;

	return 0;
}

/*
 * Write back the dirty D-caches, but not invalidate them.
 *
 * Is this really worth it, or should we just alias this routine
 * to __flush_purge_region too?
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */

void __flush_wback_region(void *start, int size)
{
	unsigned long v, j;
	unsigned long begin, end;
	unsigned long flags;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		for (j = 0; j < cpu_data->dcache.ways; j++) {
			unsigned long data, addr, p;

			p = __pa(v);
			addr = CACHE_OC_ADDRESS_ARRAY |
				(j << cpu_data->dcache.way_shift)|
				(v & cpu_data->dcache.entry_mask);
			local_irq_save(flags);
			data = ctrl_inl(addr);
			
			if ((data & CACHE_PHYSADDR_MASK) ==
			    (p & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				ctrl_outl(data, addr);
				local_irq_restore(flags);
				break;
			}
			local_irq_restore(flags);
		}
	}
}

/*
 * Write back the dirty D-caches and invalidate them.
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */
void __flush_purge_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		unsigned long data, addr;

		data = (v & 0xfffffc00); /* _Virtual_ address, ~U, ~V */
		addr = CACHE_OC_ADDRESS_ARRAY |
			(v & cpu_data->dcache.entry_mask) | SH_CACHE_ASSOC;
		ctrl_outl(data, addr);
	}
}

/*
 * No write back please
 *
 * Except I don't think there's any way to avoid the writeback. So we
 * just alias it to __flush_purge_region(). dwmw2.
 */
void __flush_invalidate_region(void *start, int size)
	__attribute__((alias("__flush_purge_region")));

/*
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * 
 * Copyright (C) 2001-2002 Silicon Graphics, Inc. All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <asm/pgalloc.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn1/synergy.h>
#include <asm/delay.h>

#ifndef MB
#define	MB	(1024*1024)
#endif

/*
 * Lock for protecting SYN_TAG_DISABLE_WAY.
 * Consider making this a per-FSB lock. 
 */
static spinlock_t flush_lock = SPIN_LOCK_UNLOCKED;

/**
 * sn_flush_all_caches - flush a range of addresses from all caches (incl. L4)
 * @flush_addr: identity mapped region 7 address to start flushing
 * @bytes: number of bytes to flush
 *
 * Flush a range of addresses from all caches including L4.  All addresses 
 * fully or partially contained within @flush_addr to @flush_addr + @bytes 
 * are flushed from the all caches.
 */
void
sn_flush_all_caches(long flush_addr, long bytes)
{
	ulong 	addr, baddr, eaddr, bitbucket;
	int	way, alias;

	/*
	 * Because of the way synergy implements "fc", this flushes the
	 * data from all caches on all cpus & L4's on OTHER FSBs. It also
	 * flushes both cpus on the local FSB. It does NOT flush it from 
	 * the local FSB.
	 */
	flush_icache_range(flush_addr, flush_addr+bytes);

	/*
	 * Memory DIMMs are a minimum of 256MB and start on 256MB
	 * boundaries. Convert the start address to an address
	 * that is between +0MB & +128 of the same DIMM. 
	 * Then add 8MB to skip the uncached MinState areas if the address
	 * is on the master node.
	 */
	if (bytes > SYNERGY_L4_BYTES_PER_WAY)
		bytes = SYNERGY_L4_BYTES_PER_WAY;
	baddr = TO_NODE(smp_physical_node_id(), PAGE_OFFSET + (flush_addr & (128*MB-1)) + 8*MB); 
	eaddr = (baddr+bytes+SYNERGY_BLOCK_SIZE-1) & ~(SYNERGY_BLOCK_SIZE-1);
	baddr = baddr & ~(SYNERGY_BLOCK_SIZE-1);

	/*
	 * Now flush the local synergy.
	 */
	spin_lock(&flush_lock);
	for(way=0; way<SYNERGY_L4_WAYS; way++) {
		WRITE_LOCAL_SYNERGY_REG(SYN_TAG_DISABLE_WAY, 0xffL ^ (1L<<way));
		mb();
		for(alias=0; alias < 9; alias++)
			for(addr=baddr; addr<eaddr; addr+=SYNERGY_BLOCK_SIZE)
				bitbucket = *(volatile ulong *)(addr+alias*8*MB);
		mb();
	}
	WRITE_LOCAL_SYNERGY_REG(SYN_TAG_DISABLE_WAY, 0);
	spin_unlock(&flush_lock);

}



/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * 
 * Copyright (c) 2001-2002 Silicon Graphics, Inc.  All rights reserved.
 */

#include <asm/sn/nodepda.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pda.h>
#include <asm/nodedata.h>

#include <linux/string.h>
#include <linux/sched.h>

#include <asm/sn/bte_copy.h>

int bte_offsets[] = { IIO_IBLS0, IIO_IBLS1 };

/*
 * bte_init_node(nodepda, cnode)
 *
 * Initialize the nodepda structure with BTE base addresses and
 * spinlocks.
 *
 */
void
bte_init_node(nodepda_t * mynodepda, cnodeid_t cNode)
{
	int i;

	/*
	 * Indicate that all the block transfer engines on this node
	 * are available.
	 */
	for (i = 0; i < BTES_PER_NODE; i++) {
#ifdef CONFIG_IA64_SGI_SN2
		/* >>> Don't know why the 0x1800000L is here.  Robin */
		mynodepda->node_bte_info[i].bte_base_addr =
		    (char *)LOCAL_MMR_ADDR(bte_offsets[i] | 0x1800000L);
#elif CONFIG_IA64_SGI_SN1
		mynodepda->node_bte_info[i].bte_base_addr =
		    (char *)LOCAL_HUB_ADDR(bte_offsets[i]);
#else
#error BTE Not defined for this hardware platform.
#endif

#ifdef CONFIG_IA64_SGI_BTE_LOCKING
		/* Initialize the notification and spinlock */
		/* so the first transfer can occur. */
		mynodepda->node_bte_info[i].mostRecentNotification =
		    &(mynodepda->node_bte_info[i].notify);
		mynodepda->node_bte_info[i].notify = 0L;
		spin_lock_init(&mynodepda->node_bte_info[i].spinlock);
#endif				/* CONFIG_IA64_SGI_BTE_LOCKING */

	}
}

/*
 * bte_init_cpu()
 *
 * Initialize the cpupda structure with pointers to the
 * nodepda bte blocks.
 *
 */
void
bte_init_cpu(void)
{
	/* Called by setup.c as each cpu is being added to the nodepda */
	if (local_node_data->active_cpu_count & 0x1) {
		pda.cpubte[0] = &(nodepda->node_bte_info[0]);
		pda.cpubte[1] = &(nodepda->node_bte_info[1]);
	} else {
		pda.cpubte[0] = &(nodepda->node_bte_info[1]);
		pda.cpubte[1] = &(nodepda->node_bte_info[0]);
	}
}


/*
 * bte_unaligned_copy(src, dest, len, mode)
 *
 * use the block transfer engine to move kernel
 * memory from src to dest using the assigned mode.
 *
 * Paramaters:
 *   src - physical address of the transfer source.
 *   dest - physical address of the transfer destination.
 *   len - number of bytes to transfer from source to dest.
 *   mode - hardware defined.  See reference information
 *          for IBCT0/1 in the SGI documentation.
 *   bteBlock - kernel virtual address of a temporary
 *              buffer used during unaligned transfers.
 *
 * NOTE: If the source, dest, and len are all cache line aligned,
 * then it would be _FAR_ preferrable to use bte_copy instead.
 */
bte_result_t
bte_unaligned_copy(u64 src, u64 dest, u64 len, u64 mode, char *bteBlock)
{
	int destFirstCacheOffset;
	u64 headBteSource;
	u64 headBteLen;
	u64 headBcopySrcOffset;
	u64 headBcopyDest;
	u64 headBcopyLen;
	u64 footBteSource;
	u64 footBteLen;
	u64 footBcopyDest;
	u64 footBcopyLen;
	bte_result_t rv;

	if (len == 0) {
		return (BTE_SUCCESS);
	}

	headBcopySrcOffset = src & L1_CACHE_MASK;
	destFirstCacheOffset = dest & L1_CACHE_MASK;

	/*
	 * At this point, the transfer is broken into
	 * (up to) three sections.  The first section is
	 * from the start address to the first physical
	 * cache line, the second is from the first physical
	 * cache line to the last complete cache line,
	 * and the third is from the last cache line to the
	 * end of the buffer.  The first and third sections
	 * are handled by bte copying into a temporary buffer
	 * and then bcopy'ing the necessary section into the
	 * final location.  The middle section is handled with
	 * a standard bte copy.
	 *
	 * One nasty exception to the above rule is when the
	 * source and destination are not symetrically
	 * mis-aligned.  If the source offset from the first
	 * cache line is different from the destination offset,
	 * we make the first section be the entire transfer
	 * and the bcopy the entire block into place.
	 */
	if (headBcopySrcOffset == destFirstCacheOffset) {

		/*
		 * Both the source and destination are the same
		 * distance from a cache line boundary so we can
		 * use the bte to transfer the bulk of the
		 * data.
		 */
		headBteSource = src & ~L1_CACHE_MASK;
		headBcopyDest = dest;
		if (headBcopySrcOffset) {
			headBcopyLen =
			    (len >
			     (L1_CACHE_BYTES -
			      headBcopySrcOffset) ? L1_CACHE_BYTES
			     - headBcopySrcOffset : len);
			headBteLen = L1_CACHE_BYTES;
		} else {
			headBcopyLen = 0;
			headBteLen = 0;
		}

		if (len > headBcopyLen) {
			footBcopyLen =
			    (len - headBcopyLen) & L1_CACHE_MASK;
			footBteLen = L1_CACHE_BYTES;

			footBteSource = src + len - footBcopyLen;
			footBcopyDest = dest + len - footBcopyLen;

			if (footBcopyDest ==
			    (headBcopyDest + headBcopyLen)) {
				/*
				 * We have two contigous bcopy
				 * blocks.  Merge them.
				 */
				headBcopyLen += footBcopyLen;
				headBteLen += footBteLen;
			} else if (footBcopyLen > 0) {
				rv = bte_copy(footBteSource,
					      __pa(bteBlock),
					      footBteLen, mode, NULL);
				if (rv != BTE_SUCCESS) {
					return (rv);
				}


				memcpy(__va(footBcopyDest),
				       (char *)bteBlock, footBcopyLen);
			}
		} else {
			footBcopyLen = 0;
			footBteLen = 0;
		}

		if (len > (headBcopyLen + footBcopyLen)) {
			/* now transfer the middle. */
			rv = bte_copy((src + headBcopyLen),
				      (dest +
				       headBcopyLen),
				      (len - headBcopyLen -
				       footBcopyLen), mode, NULL);
			if (rv != BTE_SUCCESS) {
				return (rv);
			}

		}
	} else {


		/*
		 * The transfer is not symetric, we will
		 * allocate a buffer large enough for all the
		 * data, bte_copy into that buffer and then
		 * bcopy to the destination.
		 */

		/* Add the leader from source */
		headBteLen = len + (src & L1_CACHE_MASK);
		/* Add the trailing bytes from footer. */
		headBteLen +=
		    L1_CACHE_BYTES - (headBteLen & L1_CACHE_MASK);
		headBteSource = src & ~L1_CACHE_MASK;
		headBcopySrcOffset = src & L1_CACHE_MASK;
		headBcopyDest = dest;
		headBcopyLen = len;
	}

	if (headBcopyLen > 0) {
		rv = bte_copy(headBteSource,
			      __pa(bteBlock), headBteLen, mode, NULL);
		if (rv != BTE_SUCCESS) {
			return (rv);
		}

		memcpy(__va(headBcopyDest), ((char *)bteBlock +
					     headBcopySrcOffset),
		       headBcopyLen);
	}
	return (BTE_SUCCESS);
}

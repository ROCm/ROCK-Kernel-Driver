/*
 *
 *
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pda.h>
#ifdef CONFIG_IA64_SGI_SN2
#include <asm/sn/sn2/shubio.h>
#endif
#include <asm/nodedata.h>

#include <linux/bootmem.h>
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
 * NOTE: The kernel parameter btetest will cause the initialization
 * code to reserve blocks of physically contiguous memory to be
 * used by the bte test module.
 */
void
bte_init_node(nodepda_t * mynodepda, cnodeid_t cnode)
{
	int i;


	/*
	 * Indicate that all the block transfer engines on this node
	 * are available.
	 */
	for (i = 0; i < BTES_PER_NODE; i++) {
#ifdef CONFIG_IA64_SGI_SN2
		/* >>> Don't know why the 0x1800000L is here.  Robin */
		mynodepda->bte_if[i].bte_base_addr =
		    (char *)LOCAL_MMR_ADDR(bte_offsets[i] | 0x1800000L);

#elif CONFIG_IA64_SGI_SN1
		mynodepda->bte_if[i].bte_base_addr =
		    (char *)LOCAL_HUB_ADDR(bte_offsets[i]);
#else
#error BTE Not defined for this hardware platform.
#endif

		/*
		 * Initialize the notification and spinlock
		 * so the first transfer can occur.
		 */
		mynodepda->bte_if[i].most_rcnt_na =
		    &(mynodepda->bte_if[i].notify);
		mynodepda->bte_if[i].notify = 0L;
#ifdef CONFIG_IA64_SGI_BTE_LOCKING
		spin_lock_init(&mynodepda->bte_if[i].spinlock);
#endif				/* CONFIG_IA64_SGI_BTE_LOCKING */

		mynodepda->bte_if[i].bte_test_buf =
			alloc_bootmem_node(NODE_DATA(cnode), BTE_MAX_XFER);
	}

}


/*
 * bte_reset_nasid(nasid_t)
 *
 * Does a soft reset of the BTEs on the specified nasid.
 * This is followed by a one-line transfer from each of the
 * virtual interfaces.
 */
void
bte_reset_nasid(nasid_t n)
{
	ii_ibcr_u_t	ibcr;

	ibcr.ii_ibcr_regval  = REMOTE_HUB_L(n, IIO_IBCR);
	ibcr.ii_ibcr_fld_s.i_soft_reset = 1;
	REMOTE_HUB_S(n, IIO_IBCR, ibcr.ii_ibcr_regval);

	/* One line transfer on virtual interface 0 */
	REMOTE_HUB_S(n, IIO_IBLS_0, IBLS_BUSY | 1);
	REMOTE_HUB_S(n, IIO_IBSA_0, TO_PHYS(__pa(&nodepda->bte_cleanup)));
	REMOTE_HUB_S(n, IIO_IBDA_0,
		     TO_PHYS(__pa(&nodepda->bte_cleanup[4*L1_CACHE_BYTES])));
	REMOTE_HUB_S(n, IIO_IBNA_0,
		     TO_PHYS(__pa(&nodepda->bte_cleanup[4*L1_CACHE_BYTES])));
	REMOTE_HUB_S(n, IIO_IBCT_0, BTE_NOTIFY);
	while (REMOTE_HUB_L(n, IIO_IBLS0)) {
		/* >>> Need some way out in case of hang... */
	}

	/* One line transfer on virtual interface 1 */
	REMOTE_HUB_S(n, IIO_IBLS_1, IBLS_BUSY | 1);
	REMOTE_HUB_S(n, IIO_IBSA_1, TO_PHYS(__pa(nodepda->bte_cleanup)));
	REMOTE_HUB_S(n, IIO_IBDA_1,
		     TO_PHYS(__pa(nodepda->bte_cleanup[4 * L1_CACHE_BYTES])));
	REMOTE_HUB_S(n, IIO_IBNA_1,
		     TO_PHYS(__pa(nodepda->bte_cleanup[5 * L1_CACHE_BYTES])));
	REMOTE_HUB_S(n, IIO_IBCT_1, BTE_NOTIFY);
	while (REMOTE_HUB_L(n, IIO_IBLS1)) {
		/* >>> Need some way out in case of hang... */
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
	pda->cpu_bte_if[0] = &(nodepda->bte_if[1]);
	pda->cpu_bte_if[1] = &(nodepda->bte_if[0]);
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
 *
 * NOTE: If the source, dest, and len are all cache line aligned,
 * then it would be _FAR_ preferrable to use bte_copy instead.
 */
bte_result_t
bte_unaligned_copy(u64 src, u64 dest, u64 len, u64 mode)
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
	char *bteBlock;

	if (len == 0) {
		return (BTE_SUCCESS);
	}

#ifdef CONFIG_IA64_SGI_BTE_LOCKING
#error bte_unaligned_copy() assumes single BTE selection in bte_copy().
#else
	/* temporary buffer used during unaligned transfers */
	bteBlock = pda->cpu_bte_if[0]->bte_test_buf;
#endif

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

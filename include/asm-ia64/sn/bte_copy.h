/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * 
 * Copyright (c) 2001-2002 Silicon Graphics, Inc.  All rights reserved.
 */

#ifndef _ASM_IA64_SN_BTE_COPY_H
#define _ASM_IA64_SN_BTE_COPY_H

#ident "$Revision: $"

#include <asm/sn/bte.h>
#include <asm/sn/sgi.h>
#include <asm/sn/pda.h>
#include <asm/delay.h>

/*
 * BTE_LOCKING support - Undefining the following line will
 * adapt the bte_copy code to support one bte per cpu in
 * synchronous mode.  Even if bte_copy is called with a
 * notify address, the bte will spin and wait for the transfer
 * to complete.  By defining the following, spin_locks and
 * busy checks are placed around the initiation of a BTE
 * transfer and multiple bte's per cpu are supported.
 */
#define CONFIG_IA64_SGI_BTE_LOCKING 1

/*
 * Some macros to simplify reading.
 *
 * Start with macros to locate the BTE control registers.
 */

#define BTEREG_LNSTAT_ADDR (bte->bte_base_addr)
#define BTEREG_SOURCE_ADDR (bte->bte_base_addr + IIO_IBSA0 - IIO_IBLS0)
#define BTEREG_DEST_ADDR (bte->bte_base_addr + IIO_IBDA0 - IIO_IBLS0)
#define BTEREG_CTRL_ADDR (bte->bte_base_addr + IIO_IBCT0 - IIO_IBLS0)
#define BTEREG_NOTIF_ADDR (bte->bte_base_addr + IIO_IBNA0 - IIO_IBLS0)

/* Some macros to force the IBCT0 value valid. */

#define BTE_VALID_MODES BTE_NOTIFY
#define BTE_VLD_MODE(x) (x & BTE_VALID_MODES)

// #define DEBUG_BTE
// #define DEBUG_BTE_VERBOSE
// #define DEBUG_TIME_BTE

#ifdef DEBUG_BTE
#  define DPRINTK(x) printk x	// Terse
#  ifdef DEBUG_BTE_VERBOSE
#    define DPRINTKV(x) printk x	// Verbose
#  else
#    define DPRINTKV(x)
#  endif
#else
#  define DPRINTK(x)
#  define DPRINTKV(x)
#endif

#ifdef DEBUG_TIME_BTE
extern u64 BteSetupTime;
extern u64 BteTransferTime;
extern u64 BteTeardownTime;
extern u64 BteExecuteTime;
#endif

/*
 * bte_copy(src, dest, len, mode, notification)
 *
 * use the block transfer engine to move kernel
 * memory from src to dest using the assigned mode.
 *
 * Paramaters:
 *   src - physical address of the transfer source.
 *   dest - physical address of the transfer destination.
 *   len - number of bytes to transfer from source to dest.
 *   mode - hardware defined.  See reference information
 *          for IBCT0/1 in the SHUB Programmers Reference
 *   notification - kernel virtual address of the notification cache
 *                  line.  If NULL, the default is used and
 *                  the bte_copy is synchronous.
 *
 * NOTE:  This function requires src, dest, and len to
 * be cache line aligned.
 */
extern __inline__ bte_result_t
bte_copy(u64 src, u64 dest, u64 len, u64 mode, void *notification)
{
#ifdef CONFIG_IA64_SGI_BTE_LOCKING
	int bte_to_use;
#endif

#ifdef DEBUG_TIME_BTE
	u64 invokeTime = 0;
	u64 completeTime = 0;
	u64 xferStartTime = 0;
	u64 xferCompleteTime = 0;
#endif
	u64 transferSize;
	bteinfo_t *bte;

#ifdef DEBUG_TIME_BTE
	invokeTime = ia64_get_itc();
#endif

	DPRINTK(("bte_copy (0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
		 src, dest, len, mode, notification));

	if (len == 0) {
		return (BTE_SUCCESS);
	}

	ASSERT(!((len & L1_CACHE_MASK) ||
		 (src & L1_CACHE_MASK) || (dest & L1_CACHE_MASK)));

	ASSERT(len < ((BTE_LEN_MASK + 1) << L1_CACHE_SHIFT));

#ifdef CONFIG_IA64_SGI_BTE_LOCKING
	{
		bte_to_use = 0;

		/* Attempt to lock one of the BTE interfaces */
		while ((*pda.cpubte[bte_to_use]->
			mostRecentNotification & IBLS_BUSY)
		       &&
		       (!(spin_trylock
			  (&(pda.cpubte[bte_to_use]->spinlock))))
		       && (bte_to_use < BTES_PER_NODE)) {
			bte_to_use++;
		}

		if ((bte_to_use >= BTES_PER_NODE) &&
		    !(mode & BTE_WACQUIRE)) {
			return (BTEFAIL_NOTAVAIL);
		}

		/* Wait until a bte is available. */
	}
	while (bte_to_use >= BTES_PER_NODE);

	bte = pda.cpubte[bte_to_use];
	DPRINTKV(("Got a lock on bte %d\n", bte_to_use));
#else
	/* Assuming one BTE per CPU. */
	bte = pda.cpubte[0];
#endif

	/*
	 * The following are removed for optimization but is
	 * available in the event that the SHUB exhibits
	 * notification problems similar to the hub, bedrock et al.
	 *
	 * bte->mostRecentSrc = src;
	 * bte->mostRecentDest = dest;
	 * bte->mostRecentLen = len;
	 * bte->mostRecentMode = mode;
	 */
	if (notification == NULL) {
		/* User does not want to be notified. */
		bte->mostRecentNotification = &bte->notify;
	} else {
		bte->mostRecentNotification = notification;
	}

	/* Calculate the number of cache lines to transfer. */
	transferSize = ((len >> L1_CACHE_SHIFT) & BTE_LEN_MASK);

	DPRINTKV(("Calculated transfer size of %d cache lines\n",
		  transferSize));

	/* Initialize the notification to a known value. */
	*bte->mostRecentNotification = -1L;


	DPRINTKV(("Before, status is 0x%lx and notify is 0x%lx\n",
		  HUB_L(BTEREG_LNSTAT_ADDR),
		  *bte->mostRecentNotification));

	/* Set the status reg busy bit and transfer length */
	DPRINTKV(("IBLS - HUB_S(0x%lx, 0x%lx)\n",
		  BTEREG_LNSTAT_ADDR, IBLS_BUSY | transferSize));
	HUB_S(BTEREG_LNSTAT_ADDR, IBLS_BUSY | transferSize);


	DPRINTKV(("After setting status, status is 0x%lx and notify is 0x%lx\n", HUB_L(BTEREG_LNSTAT_ADDR), *bte->mostRecentNotification));

	/* Set the source and destination registers */
	DPRINTKV(("IBSA - HUB_S(0x%lx, 0x%lx)\n", BTEREG_SOURCE_ADDR,
		  src));
	HUB_S(BTEREG_SOURCE_ADDR, src);
	DPRINTKV(("IBDA - HUB_S(0x%lx, 0x%lx)\n", BTEREG_DEST_ADDR, dest));
	HUB_S(BTEREG_DEST_ADDR, dest);


	/* Set the notification register */
	DPRINTKV(("IBNA - HUB_S(0x%lx, 0x%lx)\n", BTEREG_NOTIF_ADDR,
		  __pa(bte->mostRecentNotification)));
	HUB_S(BTEREG_NOTIF_ADDR, (__pa(bte->mostRecentNotification)));


	DPRINTKV(("Set Notify, status is 0x%lx and notify is 0x%lx\n",
		  HUB_L(BTEREG_LNSTAT_ADDR),
		  *bte->mostRecentNotification));

	/* Initiate the transfer */
	DPRINTKV(("IBCT - HUB_S(0x%lx, 0x%lx)\n", BTEREG_CTRL_ADDR, mode));
#ifdef DEBUG_TIME_BTE
	xferStartTime = ia64_get_itc();
#endif
	HUB_S(BTEREG_CTRL_ADDR, BTE_VLD_MODE(mode));

	DPRINTKV(("Initiated, status is 0x%lx and notify is 0x%lx\n",
		  HUB_L(BTEREG_LNSTAT_ADDR),
		  *bte->mostRecentNotification));

	// >>> Temporarily work around not getting a notification
	// from medusa.
	// *bte->mostRecentNotification = HUB_L(bte->bte_base_addr);

	if (notification == NULL) {
		/*
		 * Calculate our timeout
		 *
		 * What are we doing here?  We are trying to determine
		 * the fastest time the BTE could have transfered our
		 * block of data.  By takine the clock frequency (ticks/sec)
		 * divided by the BTE MaxT Transfer Rate (lines/sec)
		 * times the transfer size (lines), we get a tick
		 * offset from current time that the transfer should
		 * complete.
		 *
		 * Why do this?  We are watching for a notification
		 * failure from the BTE.  This behaviour has been
		 * seen in the SN0 and SN1 hardware on rare circumstances
		 * and is expected in SN2.  By checking at the
		 * ideal transfer timeout, we minimize our time
		 * delay from hardware completing our request and
		 * our detecting the failure.
		 */
		bte->idealTransferTimeout = jiffies +
		    (HZ / BTE_MAXT_LINES_PER_SECOND * transferSize);

		while ((IBLS_BUSY & bte->notify)) {
			/*
			 * Notification Workaround: When the max
			 * theoretical time has elapsed, read the hub
			 * status register into the notification area.
			 * This fakes the shub performing the copy.
			 */
			if (time_after(jiffies, bte->idealTransferTimeout)) {
				bte->notify = HUB_L(bte->bte_base_addr);
				bte->idealTransferTimeoutReached++;
				bte->idealTransferTimeout = jiffies +
				    (HZ / BTE_MAXT_LINES_PER_SECOND *
				     (bte->notify & BTE_LEN_MASK));
			}
		}
#ifdef DEBUG_TIME_BTE
		xferCompleteTime = ia64_get_itc();
#endif
		if (bte->notify & IBLS_ERROR) {
			/* >>> Need to do real error checking. */
			transferSize = 0;

#ifdef CONFIG_IA64_SGI_BTE_LOCKING
			spin_unlock(&(bte->spinlock));
#endif
			return (BTEFAIL_ERROR);
		}

	}
#ifdef CONFIG_IA64_SGI_BTE_LOCKING
	spin_unlock(&(bte->spinlock));
#endif
#ifdef DEBUG_TIME_BTE
	completeTime = ia64_get_itc();

	BteSetupTime = xferStartTime - invokeTime;
	BteTransferTime = xferCompleteTime - xferStartTime;
	BteTeardownTime = completeTime - xferCompleteTime;
	BteExecuteTime = completeTime - invokeTime;
#endif
	return (BTE_SUCCESS);
}

/*
 * Define the bte_unaligned_copy as an extern.
 */
extern bte_result_t bte_unaligned_copy(u64, u64, u64, u64, char *);

/*
 * The following is the prefered way of calling bte_unaligned_copy
 * If the copy is fully cache line aligned, then bte_copy is
 * used instead.  Since bte_copy is inlined, this saves a call
 * stack.  NOTE: bte_copy is called synchronously and does block
 * until the transfer is complete.  In order to get the asynch
 * version of bte_copy, you must perform this check yourself.
 */
#define BTE_UNALIGNED_COPY(src, dest, len, mode, bteBlock) \
	if ((len & L1_CACHE_MASK) || \
	    (src & L1_CACHE_MASK) || \
	    (dest & L1_CACHE_MASK)) { \
		bte_unaligned_copy (src, dest, len, mode, bteBlock); \
	} else { \
		bte_copy(src, dest, len, mode, NULL); \
	}

#endif				/* _ASM_IA64_SN_BTE_COPY_H */

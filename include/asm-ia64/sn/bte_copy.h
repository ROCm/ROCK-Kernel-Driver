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

#ifndef _ASM_IA64_SN_BTE_COPY_H
#define _ASM_IA64_SN_BTE_COPY_H

#ident "$Revision: 1.1 $"

#include <linux/timer.h>
#include <linux/cache.h>
#include <asm/sn/bte.h>
#include <asm/sn/sgi.h>
#include <asm/sn/pda.h>
#include <asm/delay.h>

#define L1_CACHE_MASK (L1_CACHE_BYTES - 1)

/*
 * BTE_LOCKING support - When CONFIG_IA64_SGI_BTE_LOCKING is
 * not defined, the bte_copy code supports one bte per cpu in
 * synchronous mode.  Even if bte_copy is called with a
 * notify address, the bte will spin and wait for the transfer
 * to complete.  By defining the following, spin_locks and
 * busy checks are placed around the initiation of a BTE
 * transfer and multiple bte's per cpu are supported.
 */
#if 0
#define CONFIG_IA64_SGI_BTE_LOCKING 1
#endif

/*
 * Handle locking of the bte interfaces.
 *
 * All transfers spinlock the interface before setting up the SHUB
 * registers.  Sync transfers hold the lock until all processing is
 * complete.  Async transfers release the lock as soon as the transfer
 * is initiated.
 *
 * To determine if an interface is available, we must check both the
 * busy bit and the spinlock for that interface.
 */
#define BTE_LOCK_IF_AVAIL(_x) (\
	(*pda.cpu_bte_if[_x]->most_rcnt_na & IBLS_BUSY) && \
	(!(spin_trylock(&(pda.cpu_bte_if[_x]->spinlock)))) \
	)

/*
 * Some macros to simplify reading.
 *
 * Start with macros to locate the BTE control registers.
 */

#define BTEREG_LNSTAT_ADDR ((u64 *)(bte->bte_base_addr))
#define BTEREG_SRC_ADDR ((u64 *)(bte->bte_base_addr + BTEOFF_SRC))
#define BTEREG_DEST_ADDR ((u64 *)(bte->bte_base_addr + BTEOFF_DEST))
#define BTEREG_CTRL_ADDR ((u64 *)(bte->bte_base_addr + BTEOFF_CTRL))
#define BTEREG_NOTIF_ADDR ((u64 *)(bte->bte_base_addr + BTEOFF_NOTIFY))

/* Some macros to force the IBCT0 value valid. */

#define BTE_VALID_MODES BTE_NOTIFY
#define BTE_VLD_MODE(x) (x & BTE_VALID_MODES)

// #define BTE_DEBUG
// #define BTE_DEBUG_VERBOSE
// #define BTE_TIME

#ifdef BTE_DEBUG
#  define BTE_PRINTK(x) printk x	/* Terse */
#  ifdef BTE_DEBUG_VERBOSE
#    define BTE_PRINTKV(x) printk x	/* Verbose */
#  else
#    define BTE_PRINTKV(x)
#  endif /* BTE_DEBUG_VERBOSE */
#else
#  define BTE_PRINTK(x)
#  define BTE_PRINTKV(x)
#endif /* BTE_DEBUG */

#define BTE_IDEAL_TMO(x) (jiffies + \
	(HZ / BTE_MAXT_LINES_PER_SECOND * x))

#ifdef BTE_TIME
volatile extern u64 bte_setup_time;
volatile extern u64 bte_transfer_time;
volatile extern u64 bte_tear_down_time;
volatile extern u64 bte_execute_time;

#define BTE_TIME_DECLARE() \
	u64 btcp_strt_tm = 0; \
	u64 btcp_cplt_tm = 0; \
	u64 xfr_strt_tm = 0; \
	u64 xfr_cplt_tm = 0; \

#define BTE_TIME_START() \
	btcp_strt_tm = xfr_strt_tm = xfr_cplt_tm = ia64_get_itc();

#define BTE_TIME_XFR_START() \
	xfr_strt_tm = ia64_get_itc();

#define BTE_TIME_XFR_STOP() \
	xfr_cplt_tm = ia64_get_itc();

#define BTE_TIME_STOP() \
	btcp_cplt_tm = ia64_get_itc(); \
	bte_setup_time = xfr_strt_tm - btcp_strt_tm; \
	bte_transfer_time = xfr_cplt_tm - xfr_strt_tm; \
	bte_tear_down_time = btcp_cplt_tm - xfr_cplt_tm; \
	bte_execute_time = btcp_cplt_tm - btcp_strt_tm; \

#else /* BTE_TIME */
#define BTE_TIME_DECLARE()
#define BTE_TIME_START()
#define BTE_TIME_XFR_START()
#define BTE_TIME_XFR_STOP()
#define BTE_TIME_STOP()
#endif /* BTE_TIME */

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
#endif /* CONFIG_IA64_SGI_BTE_LOCKING */
	u64 transfer_size;
	u64 lines_remaining;
	bteinfo_t *bte;
	BTE_TIME_DECLARE();

	BTE_TIME_START();

	BTE_PRINTK(("bte_copy (0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
		    src, dest, len, mode, notification));

	if (len == 0) {
		BTE_TIME_STOP();
		return (BTE_SUCCESS);
	}

	ASSERT(!((len & L1_CACHE_MASK) ||
		 (src & L1_CACHE_MASK) || (dest & L1_CACHE_MASK)));

	ASSERT(len < ((BTE_LEN_MASK + 1) << L1_CACHE_SHIFT));

#ifdef CONFIG_IA64_SGI_BTE_LOCKING
	{
		bte_to_use = 0;

		/* Attempt to lock one of the BTE interfaces */
		while ((bte_to_use < BTES_PER_NODE) &&
		       BTE_LOCK_IF_AVAIL(bte_to_use)) {

			bte_to_use++;
		}

		if ((bte_to_use >= BTES_PER_NODE) &&
		    !(mode & BTE_WACQUIRE)) {
			BTE_TIME_STOP();
			return (BTEFAIL_NOTAVAIL);
		}

		/* Wait until a bte is available. */
	}
	while (bte_to_use >= BTES_PER_NODE);

	bte = pda.cpu_bte_if[bte_to_use];
	BTE_PRINTKV(("Got a lock on bte %d\n", bte_to_use));
#else
	/* Assuming one BTE per CPU. */
	bte = pda->cpu_bte_if[0];
#endif /* CONFIG_IA64_SGI_BTE_LOCKING */

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
		bte->most_rcnt_na = &bte->notify;
	} else {
		bte->most_rcnt_na = notification;
	}

	/* Calculate the number of cache lines to transfer. */
	transfer_size = ((len >> L1_CACHE_SHIFT) & BTE_LEN_MASK);

	BTE_PRINTKV(("Calculated transfer size of %d cache lines\n",
		     transfer_size));

	/* Initialize the notification to a known value. */
	*bte->most_rcnt_na = -1L;


	BTE_PRINTKV(("Before, status is 0x%lx and notify is 0x%lx\n",
		     HUB_L(BTEREG_LNSTAT_ADDR),
		     *bte->most_rcnt_na));

	/* Set the status reg busy bit and transfer length */
	BTE_PRINTKV(("IBLS - HUB_S(0x%lx, 0x%lx)\n",
		     BTEREG_LNSTAT_ADDR, IBLS_BUSY | transfer_size));
	HUB_S(BTEREG_LNSTAT_ADDR, (IBLS_BUSY | transfer_size));

	/* Set the source and destination registers */
	BTE_PRINTKV(("IBSA - HUB_S(0x%lx, 0x%lx)\n", BTEREG_SRC_ADDR,
		     (TO_PHYS(src))));
	HUB_S(BTEREG_SRC_ADDR, (TO_PHYS(src)));
	BTE_PRINTKV(("IBDA - HUB_S(0x%lx, 0x%lx)\n", BTEREG_DEST_ADDR,
		     (TO_PHYS(dest))));
	HUB_S(BTEREG_DEST_ADDR, (TO_PHYS(dest)));

	/* Set the notification register */
	BTE_PRINTKV(("IBNA - HUB_S(0x%lx, 0x%lx)\n", BTEREG_NOTIF_ADDR,
		     (TO_PHYS(__pa(bte->most_rcnt_na)))));
	HUB_S(BTEREG_NOTIF_ADDR, (TO_PHYS(__pa(bte->most_rcnt_na))));

	/* Initiate the transfer */
	BTE_PRINTKV(("IBCT - HUB_S(0x%lx, 0x%lx)\n", BTEREG_CTRL_ADDR, mode));
	BTE_TIME_XFR_START();
	HUB_S(BTEREG_CTRL_ADDR, BTE_VLD_MODE(mode));

	BTE_PRINTKV(("Initiated, status is 0x%lx and notify is 0x%lx\n",
		     HUB_L(BTEREG_LNSTAT_ADDR),
		     *bte->most_rcnt_na));

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
		bte->ideal_xfr_tmo = BTE_IDEAL_TMO(transfer_size);

		while (bte->notify == -1UL) {
			/*
			 * Notification Workaround: When the max
			 * theoretical time has elapsed, read the hub
			 * status register into the notification area.
			 * This fakes the shub performing the copy.
			 */
			BTE_PRINTKV(("  Timing.  IBLS = 0x%lx, "
				     "notify= 0x%lx\n",
				     HUB_L(BTEREG_LNSTAT_ADDR),
				     bte->notify));
			if (time_after(jiffies, bte->ideal_xfr_tmo)) {
				lines_remaining = HUB_L(BTEREG_LNSTAT_ADDR) &
					BTE_LEN_MASK;
				bte->ideal_xfr_tmo_cnt++;
				bte->ideal_xfr_tmo =
					BTE_IDEAL_TMO(lines_remaining);

				BTE_PRINTKV(("  Timeout.  cpu %d "
					     "IBLS = 0x%lx, "
					     "notify= 0x%lx, "
					     "Lines remaining = %d. "
					     "New timeout = %d.\n",
					     smp_processor_id(),
					     HUB_L(BTEREG_LNSTAT_ADDR),
					     bte->notify,
					     lines_remaining,
					     bte->ideal_xfr_tmo));
			}
		}
		BTE_PRINTKV((" Delay Done.  IBLS = 0x%lx, notify= 0x%lx\n",
			     HUB_L(BTEREG_LNSTAT_ADDR),
			  bte->notify));
		BTE_TIME_XFR_STOP();
		if (bte->notify & IBLS_ERROR) {
			/* >>> Need to do real error checking. */
			transfer_size = 0;

#ifdef CONFIG_IA64_SGI_BTE_LOCKING
			spin_unlock(&(bte->spinlock));
#endif /* CONFIG_IA64_SGI_BTE_LOCKING */
			BTE_PRINTKV(("Erroring status is 0x%lx and "
				     "notify is 0x%lx\n",
				     HUB_L(BTEREG_LNSTAT_ADDR),
				     bte->notify));

			BTE_TIME_STOP();
			bte->notify = 0L;
			return (BTEFAIL_ERROR);
		}

	}
#ifdef CONFIG_IA64_SGI_BTE_LOCKING
	spin_unlock(&(bte->spinlock));
#endif /* CONFIG_IA64_SGI_BTE_LOCKING */
	BTE_TIME_STOP();
	BTE_PRINTKV(("Returning status is 0x%lx and notify is 0x%lx\n",
		     HUB_L(BTEREG_LNSTAT_ADDR),
		     *bte->most_rcnt_na));

	return (BTE_SUCCESS);
}

/*
 * Define the bte_unaligned_copy as an extern.
 */
extern bte_result_t bte_unaligned_copy(u64, u64, u64, u64);

/*
 * The following is the prefered way of calling bte_unaligned_copy
 * If the copy is fully cache line aligned, then bte_copy is
 * used instead.  Since bte_copy is inlined, this saves a call
 * stack.  NOTE: bte_copy is called synchronously and does block
 * until the transfer is complete.  In order to get the asynch
 * version of bte_copy, you must perform this check yourself.
 */
#define BTE_UNALIGNED_COPY(src, dest, len, mode)			\
	(((len & L1_CACHE_MASK) || (src & L1_CACHE_MASK) ||		\
	  (dest & L1_CACHE_MASK)) ?					\
		bte_unaligned_copy(src, dest, len, mode) :		\
		bte_copy(src, dest, len, mode, NULL))

#endif /* _ASM_IA64_SN_BTE_COPY_H */

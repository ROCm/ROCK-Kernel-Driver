/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * 
 * Copyright (c) 2001-2002 Silicon Graphics, Inc.  All rights reserved.
 */

#ifndef _ASM_IA64_SN_BTE_H
#define _ASM_IA64_SN_BTE_H

#ident "$Revision: $"

#include <linux/spinlock.h>
#include <linux/cache.h>
#include <asm/sn/io.h>

#define L1_CACHE_MASK (L1_CACHE_BYTES - 1)	/* Mask to retrieve
						 * the offset into this
						 * cache line.*/

/* BTE status register only supports 16 bits for length field */
#define BTE_LEN_MASK ((1 << 16) - 1)

/*
 * Constants used in determining the best and worst case transfer
 * times. To help explain the two, the following graph of transfer
 * status vs time may help.
 *
 *     active +------------------:-+  :
 *  status    |                  : |  :
 *       idle +__________________:_+=======
 *            0 Time           MaxT  MinT
 *
 *  Therefore, MaxT is the maximum thoeretical rate for transfering
 *  the request block (assuming ideal circumstances)
 *
 *  MinT is the minimum theoretical rate for transferring the
 *  requested block (assuming maximum link distance and contention)
 *
 *  The following defines are the inverse of the above.  They are
 *  used for calculating the MaxT time and MinT time given the 
 *  number of lines in the transfer.
 */
#define BTE_MAXT_LINES_PER_SECOND 800
#define BTE_MINT_LINES_PER_SECOND 600


/* Define hardware */
#define BTES_PER_NODE 2

/* Define hardware modes */
#define BTE_NOTIFY (IBCT_NOTIFY)
#define BTE_NORMAL BTE_NOTIFY
#define BTE_ZERO_FILL (BTE_NOTIFY | IBCT_ZFIL_MODE)

/* Use a reserved bit to let the caller specify a wait for any BTE */
#define BTE_WACQUIRE (0x4000)

/*
 * Structure defining a bte.  An instance of this
 * structure is created in the nodepda for each
 * bte on that node (as defined by BTES_PER_NODE)
 * This structure contains everything necessary
 * to work with a BTE.
 */
typedef struct bteinfo_s {
	u64 volatile notify ____cacheline_aligned;
	char *bte_base_addr ____cacheline_aligned;
	spinlock_t spinlock;
	u64 idealTransferTimeout;
	u64 idealTransferTimeoutReached;
	u64 mostRecentSrc;
	u64 mostRecentDest;
	u64 mostRecentLen;
	u64 mostRecentMode;
	u64 volatile *mostRecentNotification;
} bteinfo_t;

/* Possible results from bte_copy and bte_unaligned_copy */
typedef enum {
	BTE_SUCCESS,		/* 0 is success */
	BTEFAIL_NOTAVAIL,	/* BTE not available */
	BTEFAIL_ERROR,		/* Generic error */
	BTEFAIL_DIR		/* Diretory error */
} bte_result_t;

#endif				/* _ASM_IA64_SN_BTE_H */

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


#ifndef _ASM_IA64_SN_BTE_H
#define _ASM_IA64_SN_BTE_H

#ident "$Revision: 1.1 $"

#include <linux/spinlock.h>
#include <linux/cache.h>
#include <asm/sn/io.h>

/* BTE status register only supports 16 bits for length field */
#define BTE_LEN_BITS (16)
#define BTE_LEN_MASK ((1 << BTE_LEN_BITS) - 1)
#define BTE_MAX_XFER ((1 << BTE_LEN_BITS) * L1_CACHE_BYTES)


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
	u64 ideal_xfr_tmo;	/* Time out */
	u64 ideal_xfr_tmo_cnt;
	/* u64 most_recent_src;
	 * u64 most_recent_dest;
	 * u64 most_recent_len;
	 * u64 most_recent_mode; */
	u64 volatile *most_rcnt_na;
	void *bte_test_buf;
} bteinfo_t;

/* Possible results from bte_copy and bte_unaligned_copy */
typedef enum {
	BTE_SUCCESS,		/* 0 is success */
	BTEFAIL_NOTAVAIL,	/* BTE not available */
	BTEFAIL_ERROR,		/* Generic error */
	BTEFAIL_DIR		/* Diretory error */
} bte_result_t;

void bte_reset_nasid(nasid_t);

#endif				/* _ASM_IA64_SN_BTE_H */

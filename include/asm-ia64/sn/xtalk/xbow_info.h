/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997,2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 */
#ifndef _ASM_IA64_SN_XTALK_XBOW_INFO_H
#define _ASM_IA64_SN_XTALK_XBOW_INFO_H

#include <linux/types.h>

#define XBOW_PERF_MODES	       0x03

typedef struct xbow_link_status {
    uint64_t              rx_err_count;
    uint64_t              tx_retry_count;
} xbow_link_status_t;


#endif				/* _ASM_IA64_SN_XTALK_XBOW_INFO_H */

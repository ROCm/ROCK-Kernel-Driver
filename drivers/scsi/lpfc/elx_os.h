/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/utsname.h>
#include <stdarg.h>

typedef uint64_t elx_dma_addr_t;
typedef uint64_t elx_dma_handle_t;
typedef uint64_t elx_acc_handle_t;

/* This structure provides data members that capture the sk_buff
 * address, the next sk_buff, and the physical address mapping
 * of the sk_buff data region.  This structure is used by 
 * lpfc_ip_prep_io and lpfc_ip_unprep_io to.
 */
struct elx_phys_net_map {
	struct elx_phys_net_map *p_next;
	struct sk_buff *p_sk_buff;
	elx_dma_addr_t phys_addr;
};

typedef struct elx_phys_net_map ELX_PHYS_NET_MAP_t;

typedef struct elx_os_io {
	int datadir;
	elx_dma_addr_t nonsg_phys;
} ELX_OS_IO_t;

/* Open Source defines */

#ifdef CONFIG_PPC64
#define powerpc
#endif

#ifdef powerpc
#define LITTLE_ENDIAN_HOST   0
#define BIG_ENDIAN_HW        1
#else
#define LITTLE_ENDIAN_HOST   1
#define LITTLE_ENDIAN_HW     1
#endif

#ifdef GRANULAR_LOCKS
#define ELX_SLI_LOCK(phba, flag)    elx_sli_lock(phba, &flag)
#define ELX_SLI_UNLOCK(phba, flag)  elx_sli_unlock(phba, &flag)
#define ELX_MEM_LOCK(phba, flag)    elx_mem_lock(phba, &flag)
#define ELX_MEM_UNLOCK(phba, flag)  elx_mem_unlock(phba, &flag)
#define ELX_DISC_LOCK(phba, flag)   elx_disc_lock(phba, &flag)
#define ELX_DISC_UNLOCK(phba, flag) elx_disc_unlock(phba, &flag)
#define ELX_IOC_LOCK(phba, flag)    elx_ioc_lock(phba, &flag)
#define ELX_IOC_UNLOCK(phba, flag)  elx_ioc_unlock(phba, &flag)
#define ELX_SCH_LOCK(phba, flag)    elx_sch_lock(phba, &flag)
#define ELX_SCH_UNLOCK(phba, flag)  elx_sch_unlock(phba, &flag)
#define ELX_DRVR_LOCK(phba, flag)   flag = flag
#define ELX_DRVR_UNLOCK(phba, flag) flag = flag
#define ELX_CLK_LOCK(flag)          flag = flag
#define ELX_CLK_UNLOCK(flag)        flag = flag
#else
#define ELX_SLI_LOCK(phba, flag)    flag = flag
#define ELX_SLI_UNLOCK(phba, flag)  flag = flag
#define ELX_MEM_LOCK(phba, flag)    flag = flag
#define ELX_MEM_UNLOCK(phba, flag)  flag = flag
#define ELX_DISC_LOCK(phba, flag)   flag = flag
#define ELX_DISC_UNLOCK(phba, flag) flag = flag
#define ELX_IOC_LOCK(phba, flag)    flag = flag
#define ELX_IOC_UNLOCK(phba, flag)  flag = flag
#define ELX_SCH_LOCK(phba, flag)    flag = flag
#define ELX_SCH_UNLOCK(phba, flag)  flag = flag
#define ELX_DRVR_LOCK(phba, flag)   elx_drvr_lock(phba, &flag)
#define ELX_DRVR_UNLOCK(phba, flag) elx_drvr_unlock(phba, &flag)
#define ELX_CLK_LOCK(flag)          elx_clk_lock(0, &flag)
#define ELX_CLK_UNLOCK(flag)        elx_clk_unlock(0, &flag)
#endif				/* GRANULAR_LOCKS */

#define ELX_DMA_SYNC_FORDEV  1
#define ELX_DMA_SYNC_FORCPU  2

/* These macros are for 64 bit support */
#define putPaddrLow(addr)    ((uint32_t) \
(0xffffffff & (elx_dma_addr_t)(addr)))
#define putPaddrHigh(addr)   ((uint32_t) \
 (0xffffffff & (((elx_dma_addr_t)(addr))>>32)))
#define getPaddr(high, low)  \
  ( (( (elx_dma_addr_t)(high)<<16 ) << 16)|( (elx_dma_addr_t)(low)))

#define _static_

#define ELX_DRVR_TIMEOUT 16

#define FC_MAX_SEGSZ 8192
#define FC_MAX_POOL  1024
struct elx_mem_pool {
	void *p_virt;
	elx_dma_addr_t p_phys;
	uint16_t p_refcnt;
	uint16_t p_left;
};

/* Defines to enable configuration parameters for LINUX */
#define EXPORT_LINUX 1
#define EXPORT_AIX 0

#define MAX_ELX_BRDS       32	/* Max # boards per system */
#define MAX_FC_BINDINGS    64	/* Max # of persistent bindings */
#define MAX_FCP_TARGET     0xff	/* max num of FCP targets supported */
#define MAX_FCP_LUN        0xff	/* max num of FCP LUNs supported */
#define MAX_FCP_CMDS       0x4000	/* max num of FCP cmds supported */
#define FC_MAX_ADPTMSG     64

#define DEV_SID(x) (uint16_t)(x & 0xff)	/* extract sid from device id */
#define DEV_PAN(x) (uint16_t)((x>>8) & 0x01)	/* extract pan from device id */
#define LPFC_MAX_SCSI_ID_PER_PAN 0x100

typedef uint32_t fc_lun_t;

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

#include "elx_util.h"

#if defined(RED_HAT_LINUX_KERNEL) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,9))
#define KERNEL_HAS_PCI_MAP_PAGE
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,12))
#define KERNEL_HAS_PCI_MAP_PAGE
#endif

#ifdef powerpc
/* On powerpc, 0 is a valid physical address and the DMA mapping calls
   return -1 on failure */
#define INVALID_PHYS       -1
#else
#define INVALID_PHYS       0
#endif				/* powerpc */

#define is_invalid_phys(addr) ((addr) == (void *)((unsigned long)INVALID_PHYS))

typedef struct elx_lck {
	spinlock_t elx_lock;
} elx_lck_t;

/* Per Driver info */
typedef struct elxLinuxDriver {
	elx_lck_t clklock;
} LINUX_DRVR_t;

/* Per HBA info */
typedef struct elxLinuxHba {
	struct Scsi_Host *host;
	struct pci_dev *pcidev;
	elx_lck_t drvrlock;
	elx_lck_t slilock;
	elx_lck_t memlock;
	elx_lck_t schlock;
	elx_lck_t disclock;
	elx_lck_t ioclock;
	elx_lck_t hiprilock;
	elx_lck_t iodonelock;
	ELX_SLINK_t iodone;
	atomic_t cmnds_in_flight;
	struct net_device_stats ndstats;

	void *pci_bar0_map;	/* mapped address for PCI BAR0 */
	void *pci_bar1_map;	/* mapped address for PCI BAR1 */

	void *MBslimaddr;	/* virtual address for mbox cmds */
	void *HAregaddr;	/* virtual address for host attn reg */
	void *CAregaddr;	/* virtual address for chip attn reg */
	void *HSregaddr;	/* virtual address for host status reg */
	void *HCregaddr;	/* virtual address for host ctl reg */
	wait_queue_head_t linkevtwq;
	wait_queue_head_t rscnevtwq;
	wait_queue_head_t ctevtwq;

	struct scsi_cmnd *cmnd_retry_list;
	int in_retry;

} LINUX_HBA_t;

/* Per Target info */
typedef struct elxLinuxTgt {
} LINUX_TGT_t;

/* Per LUN info */
typedef struct elxLinuxLun {
	void *scsi_dev;
	uint32_t scpcnt;
} LINUX_LUN_t;

/* Per SCSI cmd info */
typedef struct elxLinuxBuf {
	uint32_t timeout;	/* Fill in how OS represents a time stamp */
	uint32_t offset;
	uint32_t *fc_cmd_dma_handle;
} LINUX_BUF_t;

typedef uint32_t elx_lun_t;

typedef struct sc_buf T_SCSIBUF;
#define SET_ADAPTER_STATUS(bp, val) bp->general_card_status = val;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,17)
#define  NETDEVICE struct net_device
#else
#define  NETDEVICE struct device
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43)
#define netif_start_queue(dev)  clear_bit(0, (void*)&dev->tbusy)
#define netif_stop_queue(dev)   set_bit(0, (void*)&dev->tbusy)
#define netdevice_start(dev)    dev->start = 1
#define netdevice_stop(dev)     dev->start = 0
#define dev_kfree_skb_irq(a)    dev_kfree_skb(a)
#else
#define netdevice_start(dev)
#define netdevice_stop(dev)
#endif

/* forward declaration for compiler */
struct elxHBA;

struct lpfn_probe {
	int (*open) (NETDEVICE *);
	int (*stop) (NETDEVICE *);
	int (*hard_start_xmit) (struct sk_buff *, NETDEVICE *);
	int (*hard_header) (struct sk_buff *, NETDEVICE *,
			    unsigned short, void *, void *, unsigned);
	int (*rebuild_header) (struct sk_buff *);
	void (*receive) (struct elxHBA *, void *, uint32_t);
	struct net_device_stats *(*get_stats) (NETDEVICE *);
	int (*change_mtu) (NETDEVICE *, int);
	int (*probe) (void);
};
#define LPFN_PROBE  1
#define LPFN_DETACH 2
#define LPFN_DFC    3

/* SCSI Layer io_request locking macros */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
 /* io_request_lock is not present in 2.6.0 */
#define LPFC_LOCK_SCSI_DONE    spin_lock_irqsave(&io_request_lock, sflag)
#define LPFC_UNLOCK_SCSI_DONE  spin_unlock_irqrestore(&io_request_lock, sflag)
#else
#define LPFC_LOCK_SCSI_DONE    spin_lock_irqsave(host->host_lock, sflag)
#define LPFC_UNLOCK_SCSI_DONE  spin_unlock_irqrestore(host->host_lock, sflag)
#endif

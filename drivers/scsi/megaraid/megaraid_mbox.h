/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: megaraid.h
 * Version	: v2.20.0.rc1 (May 17 2004)
 */

#ifndef _MEGARAID_H_
#define _MEGARAID_H_

#include "kdep.h"
#include "mbox_defs.h"
#include "mega_common.h"


#define MEGARAID_VERSION	"2.20.0.rc1"
#define MEGARAID_EXT_VERSION	"(Release Date: Mon May 17 18:55:02 EDT 2004)"


/*
 * Define some PCI values here until they are put in the kernel
 */
#define PCI_DEVICE_ID_PERC4_DI_DISCOVERY		0x000E
#define PCI_SUBSYS_ID_PERC4_DI_DISCOVERY		0x0123

#define PCI_DEVICE_ID_PERC4_SC				0x1960
#define PCI_SUBSYS_ID_PERC4_SC				0x0520

#define PCI_DEVICE_ID_PERC4_DC				0x1960
#define PCI_SUBSYS_ID_PERC4_DC				0x0518

#define PCI_DEVICE_ID_PERC4_QC				0x0407
#define PCI_SUBSYS_ID_PERC4_QC				0x0531

#define PCI_DEVICE_ID_PERC4_DI_EVERGLADES		0x000F
#define PCI_SUBSYS_ID_PERC4_DI_EVERGLADES		0x014A

#define PCI_DEVICE_ID_PERC4E_SI_BIGBEND			0x0013
#define PCI_SUBSYS_ID_PERC4E_SI_BIGBEND			0x016c

#define PCI_DEVICE_ID_PERC4E_DI_KOBUK			0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_KOBUK			0x016d

#define PCI_DEVICE_ID_PERC4E_DI_CORVETTE		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_CORVETTE		0x016e

#define PCI_DEVICE_ID_PERC4E_DI_EXPEDITION		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_EXPEDITION		0x016f

#define PCI_DEVICE_ID_PERC4E_DI_GUADALUPE		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_GUADALUPE		0x0170

#define PCI_DEVICE_ID_PERC4E_DC_320_2E			0x0408
#define PCI_SUBSYS_ID_PERC4E_DC_320_2E			0x0002

#define PCI_DEVICE_ID_PERC4E_SC_320_1E			0x0408
#define PCI_SUBSYS_ID_PERC4E_SC_320_1E			0x0001

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_0		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_0		0xA520

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_1		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_1		0x0520

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_2		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_2		0x0518

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_0x		0x0407
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_0x		0x0530

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_2x		0x0407
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_2x		0x0532

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_4x		0x0407
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_4x		0x0531

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_1E		0x0408
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_1E		0x0001

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_2E		0x0408
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_2E		0x0002

#define PCI_DEVICE_ID_MEGARAID_SATA_150_4		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SATA_150_4		0x4523

#define PCI_DEVICE_ID_MEGARAID_SATA_150_6		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SATA_150_6		0x0523

#define PCI_DEVICE_ID_MEGARAID_SATA_300_4x		0x0409
#define PCI_SUBSYS_ID_MEGARAID_SATA_300_4x		0x1504

#define PCI_DEVICE_ID_MEGARAID_SATA_300_8x		0x0409
#define PCI_SUBSYS_ID_MEGARAID_SATA_300_8x		0x1508

#define PCI_DEVICE_ID_INTEL_RAID_SRCU42X		0x0407
#define PCI_SUBSYS_ID_INTEL_RAID_SRCU42X		0x0532

#define PCI_DEVICE_ID_INTEL_RAID_SRCS16			0x1960
#define PCI_SUBSYS_ID_INTEL_RAID_SRCS16			0x0523

#define PCI_DEVICE_ID_INTEL_RAID_SRCU42E		0x0408
#define PCI_SUBSYS_ID_INTEL_RAID_SRCU42E		0x0002

#define PCI_DEVICE_ID_INTEL_RAID_SRCZCRX		0x0407
#define PCI_SUBSYS_ID_INTEL_RAID_SRCZCRX		0x0530

#define PCI_DEVICE_ID_INTEL_RAID_SRCS28X		0x0409
#define PCI_SUBSYS_ID_INTEL_RAID_SRCS28X		0x1508

#define PCI_DEVICE_ID_INTEL_RAID_SROMBU42E_ALIEF	0x0408
#define PCI_SUBSYS_ID_INTEL_RAID_SROMBU42E_ALIEF	0x3431

#define PCI_DEVICE_ID_INTEL_RAID_SROMBU42E_HARWICH	0x0408
#define PCI_SUBSYS_ID_INTEL_RAID_SROMBU42E_HARWICH	0x3499

#define PCI_DEVICE_ID_FSC_MEGARAID_PCI_EXPRESS_ROMB	0x0408
#define PCI_SUBSYS_ID_FSC_MEGARAID_PCI_EXPRESS_ROMB	0x1065

#define PCI_SUBSYS_ID_PERC3_DC				0x493

#ifndef PCI_SUBSYS_ID_FSC
#define PCI_SUBSYS_ID_FSC			0x1734
#endif

#define MBOX_MAX_COMMANDS	126	// max command ids available to driver
#define MBOX_MAX_DRIVER_CMDS	200	// number of cmds supported by driver
#define MBOX_DEFAULT_SG_SIZE	26	// default sg size supported by all fw
#define MBOX_MAX_SG_SIZE	32	// maximum scatter-gather list size
#define MBOX_MAX_SECTORS	128
#define MBOX_TIMEOUT		30	// timeout value for internal cmds
#define MBOX_BUSY_WAIT		10	// max usec to wait for busy mailbox
#define MBOX_RESET_WAIT		180	// wait these many seconds in reset

/*
 * maximum transfer that can happen through the firmware commands issued
 * internnaly from the driver.
 */
#define MBOX_IBUF_SIZE	4096


/**
 * mbox_ccb_t - command control block specific to mailbox based controllers
 * @raw_mbox		: raw mailbox pointer
 * @mbox		: mailbox
 * @mbox64		: extended mailbox
 * @mbox_dma_h		: maibox dma address
 * @sgl64		: 64-bit scatter-gather list
 * @sgl32		: 32-bit scatter-gather list
 * @sgl_dma_h		: dma handle for the scatter-gather list
 * @pthru		: passthru structure
 * @pthru_dma_h		: dma handle for the passthru structure
 * @epthru		: extended passthru structure
 * @epthru_dma_h	: dma handle for extended passthru structure
 * @buf_dma_h		: dma handle for buffers w/o sg list
 *
 * command control block specific to the mailbox based controllers
 */
typedef struct {
	uint8_t			*raw_mbox;
	mbox_t			*mbox;
	mbox64_t		*mbox64;
	dma_addr_t		mbox_dma_h;
	mbox_sgl64		*sgl64;
	mbox_sgl32		*sgl32;
	dma_addr_t		sgl_dma_h;
	mraid_passthru_t	*pthru;
	dma_addr_t		pthru_dma_h;
	mraid_epassthru_t	*epthru;
	dma_addr_t		epthru_dma_h;
	dma_addr_t		buf_dma_h;
} mbox_ccb_t;


/**
 * mraid_device_t - adapter soft state structure for mailbox controllers
 * @param baseport		: base port of hba memory
 * @param baseaddr		: mapped addr of hba memory
 * @param una_mbox64		: 64-bit mbox - unaligned
 * @param una_mbox64_dma	: mbox dma addr - unaligned
 * @param mbox64		: 64-bit mbox - aligned
 * @param mbox			: 32-bit mbox - aligned
 * @param mbox_dma		: mbox dma addr - aligned
 * @param mailbox_lock		: exclusion lock for the mailbox
 * @param ccb_list		: list of our command control blocks
 * @param mbox_pool		: pool of mailboxes
 * @param mbox_pool_handle	: handle for the mailbox pool memory
 * @param sg_pool		: pool of scatter-gather lists for this driver
 * @param sg_pool_handle	: handle to the pool above
 * @param epthru_pool		: a pool for extended passthru commands
 * @param epthru_pool_handle	: handle to the pool above
 * @param int_cmdid		: command id for internal commands
 * @param pdrv_state		: array for state of each physical drive.
 * @param flags			: mailbox hba specific flags
 * @param cmdid			: available command ids
 * @param scb			: associated scb
 * @param cmdid_index		: top of stack of available command ids
 * @param proc_read		: /proc configuration read
 * @param proc_stat		: /proc information for statistics
 * @param proc_mbox		: /proc information for current mailbox
 * @param proc_rr		: /proc information for rebuild rate
 * @param proc_battery		: /proc information for battery status
 * @param proc_pdrvstat		: /proc information for physical drives state
 * @param proc_rdrvstat		: /proc information for logical drives status
 * @param last_disp		: flag used to show device scanning
 * @param hw_error		: set if FW not responding
 * @fast_load			: If set, skip physical device scanning
 * @channel_class		: channel class, RAID or SCSI
 *
 * Initialization structure for mailbox controllers: memory based and IO based
 * All the fields in this structure are LLD specific and may be discovered at
 * init() or start() time.
 */
typedef struct {
	mbox64_t			*una_mbox64;
	dma_addr_t			una_mbox64_dma;
	mbox_t				*mbox;
	mbox64_t			*mbox64;
	dma_addr_t			mbox_dma;
	spinlock_t			mailbox_lock;

	unsigned long			baseport;
	unsigned long			baseaddr;

	struct mraid_pci_blk		mbox_pool[MBOX_MAX_DRIVER_CMDS];
	struct mraid_pci_blk_pool	*mbox_pool_handle;
	struct mraid_pci_blk		epthru_pool[MBOX_MAX_DRIVER_CMDS];
	struct mraid_pci_blk_pool	*epthru_pool_handle;
	struct mraid_pci_blk		sg_pool[MBOX_MAX_DRIVER_CMDS];
	struct mraid_pci_blk_pool	*sg_pool_handle;

	mbox_ccb_t			ccb_list[MBOX_MAX_DRIVER_CMDS];

	struct tasklet_struct		dpc_h;

	uint8_t				int_cmdid;
	uint8_t				pdrv_state[MBOX_MAX_PHYSICAL_DRIVES];
	uint32_t			flags;

	struct {
		int			cmdid;
		scb_t			*scb;
	} cmdid_instance[MBOX_MAX_COMMANDS];
	int				cmdid_index;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry		*proc_root;
	struct proc_dir_entry		*proc_read;
	struct proc_dir_entry		*proc_stat;
	struct proc_dir_entry		*proc_rr;
	struct proc_dir_entry		*proc_battery;
#define MBOX_MAX_PROC_CHANNELS		4
	struct proc_dir_entry		*proc_pdrvstat[MBOX_MAX_PROC_CHANNELS];
	struct proc_dir_entry		*proc_rdrvstat[MBOX_MAX_PROC_CHANNELS];
#endif
	uint32_t			last_disp;
	int				hw_error;
	int				fast_load;
	uint8_t				channel_class;
} mraid_device_t;

// route to raid device from adapter
#define ADAP2RAIDDEV(adp)	((mraid_device_t *)((adp)->raid_device))

#define MAILBOX_LOCK(rdev)	(&rdev->mailbox_lock)

// Find out if this channel is a RAID or SCSI
#define IS_RAID_CH(rdev, ch)	(((rdev)->channel_class >> (ch)) & 0x01)


/*
 * This driver supports more commands than made available by the firmware.
 * Following macros enable us to assign free command ids to the driver
 * commands. Make sure try not to allocate commands ids for driver internal
 * commands. These commands have fixed command ID.
 */
/*
 * Get a free command id.
 */
#define INTERNAL_CMD_SIGNATURE	(-1)	// signature to differetiate cmds

#define	MBOX_GET_CMDID(rdev, scb)					\
if (scb->scp->cmnd[0] != MRAID_INTERNAL_COMMAND) {			\
	if (rdev->cmdid_index == -1) {					\
		scb->sno = -1;						\
	}								\
	else {								\
		scb->sno =						\
		rdev->cmdid_instance[rdev->cmdid_index].cmdid;		\
		rdev->cmdid_instance[scb->sno].scb = scb;		\
		rdev->cmdid_instance[rdev->cmdid_index].cmdid = -1;	\
		rdev->cmdid_index--;					\
	}								\
}									\

/*
 * Get SCB corresponding to the command id
 */
#define MBOX_GET_CMDID_SCB(rdev, id)	 rdev->cmdid_instance[id].scb

/*
 * Free a command id
 */
#define MBOX_FREE_CMDID(rdev, id)					\
		rdev->cmdid_instance[++rdev->cmdid_index].cmdid = id;	\
		rdev->cmdid_instance[id].scb = NULL;			\


#define RDINDOOR(rdev)		readl((rdev)->baseaddr + 0x20)
#define RDOUTDOOR(rdev)		readl((rdev)->baseaddr + 0x2C)
#define WRINDOOR(rdev, value)	writel(value, (rdev)->baseaddr + 0x20)
#define WROUTDOOR(rdev, value)	writel(value, (rdev)->baseaddr + 0x2C)

#endif // _MEGARAID_H_

/* vim: set ts=8 sw=8 tw=78: */

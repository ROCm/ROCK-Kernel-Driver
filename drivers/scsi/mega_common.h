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
 * FILE		: mega_common.h
 * Version	: v2.20.0 (Apr 14 2004)
 *
 * Libaray of common routine used by all megaraid drivers.
 */

#ifndef _MEGA_COMMON_H_
#define _MEGA_COMMON_H_

#include "lsi_defs.h"

#define PCI_DIR(scp)		scsi_to_pci_dma_dir(scp->sc_data_direction)

#define VENDOR_SPECIFIC_COMMAND	0xE0

/*
 * lockscope definitions, callers can specify the lock scope with this data
 * type. LOCK_INT would mean the caller has not acquired the lock before
 * making the call and LOCK_EXT would mean otherwise.
 */
typedef enum { LOCK_INT, LOCK_EXT } lockscope_t;

/**
 * scb_t - scsi command control block
 * @param ccb		: command control block for individual driver
 * @param list		: list of control blocks
 * @param gp		: general purpose field for LLDs
 * @param sno		: all SCBs have a serial number
 * @param scp		: associated scsi command
 * @param state		: current state of scb
 * @param dma_dir	: direction of data transfer
 * @param dma_type	: transfer with sg list, buffer, or no data transfer
 * @param dev_channel	: actual channel on the device
 * @param dev_target	: actual target on the device
 * @param status	: completion status
 * @param entry_time	: command entry time
 * @param exit_time	: command exit time
 *
 * This is our central data structure to issue commands the each driver.
 * Driver specific data structures are maintained in the ccb field.
 * scb provides a field 'gp', which can be used by LLD for its own purposes
 *
 * dev_channel and dev_target must be initialized with the actual channel and
 * target on the controller.
 */
typedef struct {
	caddr_t			ccb;
	struct list_head	list;
	unsigned long		gp;	
	unsigned int		sno;
	Scsi_Cmnd		*scp;
	uint32_t		state;
	uint32_t		dma_direction;
	uint32_t		dma_type;
	uint16_t		dev_channel;
	uint16_t		dev_target;
	uint32_t		status;
	unsigned long		entry_time;
	unsigned long		exit_time;
} scb_t;

/*
 * SCB states as it transitions from one state to another
 */
#define SCB_FREE	0x0000	/* on the free list */
#define SCB_ACTIVE	0x0001	/* off the free list */
#define SCB_PENDQ	0x0002	/* on the pending queue */
#define SCB_ISSUED	0x0004	/* issued - owner f/w */
#define SCB_ABORT	0x0008	/* Got an abort for this one */
#define SCB_RESET	0x0010	/* Got a reset for this one */

/*
 * DMA types for scb
 */
#define MRAID_DMA_NONE	0x0000	/* no data transfer for this command */
#define MRAID_DMA_WSG	0x0001	/* data transfer using a sg list */
#define MRAID_DMA_WBUF	0x0002	/* data transfer using a contiguous buffer */


/**
 * struct adapter_t - driver's initialization structure
 * @param list			: list of megaraid host structures
 * @param dpc_h			: tasklet handle
 * @param slot			: slot number in global array of adapters
 * @param id			: PCI device identifier
 * @param host			: pointer to host structure of mid-layer
 * @param init_id		: initiator ID, the default value should be 7
 * @param boot_enabled		: set if this device is boot capable
 * @param bd_channel		: the physical channel number with boot device
 * @param bd_target		: the target of the boot device
 * @param virtual_ch		: the channel number to export logical drives on
 * @param max_channel		: maximum channel number supported - inclusive
 * @param max_target		: max target supported - inclusive
 * @param max_lun		: max lun supported - inclusive
 * @param device_ids		: to convert kernel device addr to our devices.
 * @param max_cdb_sz		: biggest CDB size supported.
 * @param max_cmds		: max outstanding commands
 * @param ha			: is high availability present - clustering
 * @param sglen			: max sg elements supported
 * @param max_sectors		: max sectors per request
 * @param cmd_per_lun		: max outstanding commands per LUN
 * @param highmem_dma		: can DMA beyond 4GB addresses. See also, NOTES.
 * @param fw_version		: firmware version
 * @param bios_version		: bios version
 * @param ibuf			: buffer to issue internal commands
 * @param ibuf_dma_h		: dma handle for the above buffer
 * @param flags			: controller specific flags
 * @param unique_id		: unique identifier for each adapter
 * @param irq			: IRQ for this adapter
 * @param mdevice		: each contoller's device data
 * @param pdev			: pci configuration pointer for kernel
 * @param lock			: synchronization lock for mid-layer and driver
 * @param host_lock		: pointer to appropriate lock
 * @param scb_list		: pointer to the bulk of SCBs memory area
 * @param scb_pool		: pool of free scbs.
 * @param pend_list		: pending commands list
 * @param completed_list	: list of completed commands
 * @param quiescent		: driver is quiescent for now.
 * @param outstanding_cmds	: number of commands pending in the driver
 * @param iscb			: control block for command issued internally
 * @param isc			: associated SCSI command for generality
 * @param imtx			: allow only one internal pending command
 * @param iwq			: wait queue for synchronous internal commands
 * @param ito			: internal timeout value, (-1) means no timeout
 * @param icmd_recovery		: internal command path timed out
 * @param stats			: IO stastics about the controller
 * @param raid_device		: raid adapter specific pointer
 *
 * The fields init_id, boot_enabled, bd_channel, bd_target, virtual_ch,
 * max_channel, max_target, max_lun, and device_ids are part of a subsytem
 * called the device map.
 * If LLDs want to have a flexbile booting order for their devices (boot from
 * any logical or physical device) - they should make use of the framework
 * APIs mraid_setup_device_map(adapter_t *) and
 * MRAID_GET_DEVICE_MAP(adp, scp, channel, target, islogical).
 *
 * mraid_setup_device_map() can be called anytime after the device map is
 * available and MRAID_GET_DEVICE_MAP() can be called whenever the mapping is
 * required, usually from LLD's queue entry point. The formar API sets up the
 * fields 'device_ids' with appropriate value. Make sure before calling this
 * routine - all fields in device map are filled in otherwise unexpected
 * behavior will result. The later uses this information to return information
 * about the device in question. LLDs can use the macro
 * MRAID_IS_LOGICAL(adapter_t *, struct scsi_cmnd *) to find out if the
 * device in question is a logical drive.
 *
 * quiescent flag should be set by the driver if it is not accepting more
 * commands
 *
 * If any internal command is timed out, icmd flag_recovery should be set and
 * further internal commands will return error until the command is actually
 * completed if ever.
 *
 * NOTES:
 * i.	the highmem_dma flag denotes whether we are registering ourselves as a
 * 64-bit capable driver or not. If a HBA supports 64-bit addressing, that
 * alone is not a sufficient condition for registering as 64-bit driver. The
 * kernel should also provide support for such arrangement. To denote if the
 * HBA supports 64-bit addressing, the flag DMA_64 is set in adapter_t
 * object.
 */

/*
 * amount of space required to store the bios and firmware version strings
 */
#define VERSION_SIZE	16


/*
 * Valid values for flags field of adapter_t structure.
 */
#define MRAID_DMA_64		0x00000001	/* can dma in 64-bit address
						range */
#define MRAID_BOARD_MEMMAP	0x00000002	/* Is a memory-mapped
						controller */
#define MRAID_BOARD_IOMAP	0x00000004	/* Is a IO-mapped controller */


typedef struct {
	struct list_head		list;
	struct tasklet_struct		dpc_h;
	int				slot;

	const struct pci_device_id	*pci_id;
	struct pci_dev			*pdev;
	uint32_t			unique_id;
	uint8_t				irq;
	bool_t				highmem_dma;

	spinlock_t			*host_lock;
	spinlock_t			lock;
	scb_t				iscb;
	Scsi_Cmnd			isc;
	struct scsi_device		isdev;
	struct semaphore		imtx;
#define MRAID_STATE_SLEEP		0
#define MRAID_WAKEUP_NORM		1
#define MRAID_WAKEUP_TIMEOUT		2
#define MRAID_INTERNAL_COMMAND		VENDOR_SPECIFIC_COMMAND
	wait_queue_head_t		iwq;

	bool_t				quiescent;
	int				outstanding_cmds;
	uint8_t				ito;
	uint8_t				icmd_recovery;
	caddr_t				ibuf;
	dma_addr_t			ibuf_dma_h;

	scb_t				*scb_list;
	struct list_head		scb_pool;
	struct list_head		pend_list;
	struct list_head		completed_list;

	uint8_t				max_channel;
	uint16_t			max_target;
	uint8_t				max_lun;
	int				max_cmds;
	uint8_t				fw_version[VERSION_SIZE];
	uint8_t				bios_version[VERSION_SIZE];
	uint8_t				max_cdb_sz;
	bool_t				ha;
	uint16_t			init_id;

	bool_t				boot_enabled;
	uint8_t				bd_channel;
	uint16_t			bd_target;
	uint8_t				virtual_ch;
	int	device_ids[LSI_MAX_CHANNELS][LSI_MAX_LOGICAL_DRIVES_64LD];

	struct Scsi_Host		*host;

	uint16_t			sglen;
	uint16_t			max_sectors;
	uint16_t			cmd_per_lun;
	uint32_t			flags;

#ifdef MRAID_HAVE_STATS
	mraid_stats_t			stats;
#endif
	caddr_t				raid_device;

	atomic_t			being_detached;

} adapter_t;


/**
 * MRAID_GET_DEVICE_MAP - device ids
 * @param adp		- Adapter's soft state
 * @param scp		- mid-layer scsi command pointer
 * @param p_chan	- physical channel on the controller
 * @param target	- target id of the device or logical drive number
 * @param islogical	- set if the command is for the logical drive
 *
 * Macro to retrieve information about device class, logical or physical and
 * the corresponding physical channel and target or logical drive number
 **/
#define MRAID_GET_DEVICE_MAP(adp, scp, p_chan, target, islogical)	\
	/*								\
	 * Is the request coming for the virtual channel		\
	 */								\
	islogical = (SCP2CHANNEL(scp) == (adp)->virtual_ch) ? 1 : 0;	\
									\
	/*								\
	 * Get an index into our table of drive ids mapping		\
	 */								\
	if (islogical) {						\
		p_chan = 0xFF;						\
		target =						\
		(adp)->device_ids[(adp)->virtual_ch][SCP2TARGET(scp)];	\
	}								\
	else {								\
		p_chan = ((adp)->device_ids[SCP2CHANNEL(scp)][SCP2TARGET(scp)] >> 8) & 0xFF;	\
		target = ((adp)->device_ids[SCP2CHANNEL(scp)][SCP2TARGET(scp)] & 0xFF);	\
	}

#define MRAID_IS_LOGICAL(adp, scp)	\
	(SCP2CHANNEL(scp) == (adp)->virtual_ch) ? MRAID_TRUE : MRAID_FALSE;

/**
 * struct mraid_driver_t - global driver data
 * @param is_pvt_intf		: Is intrface available for private interfaces
 * @param driver_version	: driver version
 * @param device_list		: list of adapter_t structures
 * @param attach_count		: number of controllers detected by the driver
 * @param raid_device		: array of attached raid controllers
 *
 * mraid_driver_t contains information which is global to the driver.
 *
 * FIXME: we provide two external interfaces in addition to the regular IO
 * path, private ioctl and /proc. Care must be taken about using these two
 * interfaces while module is being unloaded. For now, we provide a macro,
 * IS_INTF_AVAILABLE(), which would return 1 if it is ok to use. In case of 0,
 * the corresponding entry points in each LLD must return w/o further
 * processing.
 */
#define MAX_CONTROLLERS		32
typedef struct _mraid_driver_t {
	atomic_t		is_pvt_intf;
	uint8_t			driver_version[8];
	struct list_head	device_list;
	uint8_t			attach_count;
	adapter_t		*adapter[MAX_CONTROLLERS];
} mraid_driver_t;

#define SET_PRV_INTF_AVAILABLE() atomic_set(&mraid_driver_g.is_pvt_intf, 1)
#define SET_PRV_INTF_UNAVAILABLE() atomic_set(&mraid_driver_g.is_pvt_intf, 0)
#define IS_PRV_INTF_AVAILABLE() atomic_read(&mraid_driver_g.is_pvt_intf) ? 1 : 0


/*
 * ### Helper routines ###
 */
extern int debug_level;
#define LSI_DBGLVL debug_level

/*
 * Assertaion helpers. The driver must use the foursome macros:
 * try_assertion {
 * 	ASSERT(expression);
 * }
 * catch_assertion {
 * 	// failed assetion steps
 * }
 * end_assertion
 *
 * Depending on compliation flag 'DEBUG' flag, the assert condition can panic
 * the machine or just print the assertion failure message. In the later case,
 * the driver will catch the failure in catch_assertion block and can take
 * recovery action.
 * NOTE: This is not a generic implementation since we only catch an integer
 * true-false assertion failure unlike C++ fullblown try-catch-throw
 * exceptions.
 */
#define try_assertion {			\
	int	__assertion_catched = 0;
#define catch_assertion if( __assertion_catched )
#define	end_assertion				}

#if defined (_ASSERT_PANIC)
#define ASSERT_ACTION	panic
#else
#define ASSERT_ACTION	printk
#endif

#define ASSERT(expression)						\
	if( !(expression) ) {						\
		__assertion_catched = 1;				\
	ASSERT_ACTION("assertion failed:(%s), file: %s, line: %d:%s\n",	\
			#expression, __FILE__, __LINE__, __FUNCTION__);	\
	}

/*
 * Library to allocate memory regions which are DMA'able
 */
/*
 * struct mraid_pci_blk_pool - structure holds DMA memory pool info
 * @param dev			: pci device that will be doing the DMA
 * @param dmah_arr		: dma handle for allocated pages
 * @param page_arr		: virtual addresses for all allocated pages
 * @param page_count		: actual number of pages allocated
 *
 * Pool allocator, wraps the pci_alloc_consistent page allocator, so
 * small blocks are easily used by drivers for bus mastering controllers.
 *
 * Limit number of pages to max MEMLIB_MAX_PAGES
 */
#define	MEMLIB_MAX_PAGES	64

struct mraid_pci_blk_pool {
	struct pci_dev	*dev;
	dma_addr_t	dmah_arr[MEMLIB_MAX_PAGES];
	caddr_t		page_arr[MEMLIB_MAX_PAGES];
	int		page_count;
};

/*
 * struct mraid_pci_blk - structure holds DMA memory block info
 * @param vaddr		: virtual address to a memory block
 * @param dma_addr	: DMA handle to a memory block
 *
 * This structure is filled up for the caller. It is the responsibilty of the
 * caller to allocate this array big enough to store addresses for all
 * requested elements
 */
struct mraid_pci_blk {
	caddr_t		vaddr;
	dma_addr_t	dma_addr;
};

void mraid_setup_device_map(adapter_t *);
void mraid_icmd_done(struct scsi_cmnd *);
void mraid_icmd_timeout(unsigned long);
inline scb_t *mraid_get_icmd(adapter_t *);
inline void mraid_free_icmd(adapter_t *);
inline scb_t *mraid_alloc_scb(adapter_t *, struct scsi_cmnd *);
inline void mraid_dealloc_scb(adapter_t *, scb_t *);
void mraid_add_scb_to_pool(adapter_t *, scb_t *);
struct mraid_pci_blk_pool *mraid_pci_blk_pool_create(struct pci_dev *,
	size_t, size_t, size_t, struct mraid_pci_blk[]);
void mraid_pci_blk_pool_destroy(struct mraid_pci_blk_pool *);

#endif /* _MEGA_COMMON_H_ */

/* vim: set ts=8 sw=8 tw=78: */

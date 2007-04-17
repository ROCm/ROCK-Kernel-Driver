/*
 * Copyright (C) 2006 Sony Computer Entertainment Inc.
 * Copyright 2006, 2007 Sony Corporation
 * storage support for PS3
 *
 * based on scsi_debug.h
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PS3_STOR_H
#define _PS3_STOR_H

#include <linux/types.h>
#include <../arch/powerpc/platforms/ps3/platform.h>

#define LV1_STORAGE_SEND_ATAPI_COMMAND	(1)
#define LV1_STORAGE_ATA_HDDOUT		(0x23)

#define PS3_STOR_MAX_INQUIRY_DATA_SIZE	(128)
#define PS3_STOR_INQUIRY_DATA_SIZE	(86)
#define PS3_STOR_READCAP_DATA_SIZE	(8)
#define PS3_STOR_SENSE_LEN		(32)
#define PS3_STOR_VERSION		"1.00"
#define PS3_STOR_CANQUEUE		(1)
#define PS3_STOR_MAX_CMD_LEN		(16)

struct lv1_atapi_cmnd_block {
	u8	pkt[32];	/* packet command block           */
	u32	pktlen;		/* should be 12 for ATAPI 8020    */
	u32	blocks;
	u32	block_size;
	u32	proto;		/* transfer mode                  */
	u32	in_out;		/* transfer direction             */
	u64	buffer;		/* parameter except command block */
	u32	arglen;		/* length above                   */
};

enum lv1_atapi_proto {
	NA_PROTO = -1,
	NON_DATA_PROTO     = 0,
	PIO_DATA_IN_PROTO  = 1,
	PIO_DATA_OUT_PROTO = 2,
	DMA_PROTO = 3
};

enum lv1_atapi_in_out {
	DIR_NA = -1,
	DIR_WRITE = 0, /* memory -> device */
	DIR_READ = 1 /* device -> memory */
};

/*
 * describe protocol of an ATAPI command
 */
struct ps3_stor_dev_info;

struct scsi_command_handler_info {
	int buflen;
	int proto;
	int in_out;
	int (*cmnd_handler)(struct ps3_stor_dev_info *, struct scsi_cmnd *);
};

/*
 * to position parameter
 */
enum {
	NOT_AVAIL          = -1,
	USE_SRB_10         = -2,
	USE_SRB_6          = -3,
	USE_CDDA_FRAME_RAW = -4
};
/*
 * for LV1 maintainance
 */
enum  {
	PS3_STORAGE_PATA_0, /* primary   PATA bus */
	PS3_STORAGE_PATA_1, /* secondary PATA bus */
	PS3_STORAGE_FLASH,
	PS3_STORAGE_NUM_OF_BUS_TYPES /* terminator */
};

/*
 * LV1 per physical bus info:
 * PATA0, PATA1, FLASH
 */
struct ps3_stor_lv1_bus_info {
	int bus_type;           /* PATA0, PATA1, FLASH */
	int devices;            /* number of devices on the bus */
	struct list_head dev_list;
};

/*
 * LV1 per region info
 */
struct ps3_stor_lv1_region_info {
	int region_index;	/* index of this region       */
	unsigned int region_id;	/* id of this region          */
	u64 region_size;	/* region size in sector      */
	u64 region_start;	/* start sector */
};

/*
 * LV1 per device info
 */
struct ps3_stor_lv1_dev_info {
	struct list_head bus_dev_list; /* device list of devices          */
				       /* which share same physical bus   */
	struct ps3_stor_dev_info * dev_info;
	/* repository values */
	struct ps3_repository_device repo;
	enum ps3_dev_type device_type;	/* bus#X.dev#Y.type     */
	u64 attached_port;		/* bus#x.dev#Y.port     */
	u64 sector_size;		/* bus#X.dev#Y.blk_size */

	/* house keeping */
	int bus_type;			/* PATA0,1 or FLASH */
	unsigned int irq_plug_id;
	unsigned int interrupt_id;
	u64 dma_region;
	u64 current_tag;
	int bus_device_index;		/*
					 * device index of same lv1 phy bus.
					 * 0 for first device, 1 for second.
					 * should be same as SCSI id
					 */
	/* regions */
	unsigned int regions;	/* number of regions reported thru repository */
	unsigned long accessible_region_flag; /* flag of accessible regions */
	unsigned int accessible_regions; /* number of accessible regions of this dev.
				 * currently, this includes region #0
				 * NOTE: maximum is 8, if exceed, the rest of
				 * regions are ignored
				 */
	struct ps3_stor_lv1_region_info * region_info_array;
};

enum read_or_write {
	SCSIDEBUG_READ,
	SCSIDEBUG_WRITE
};


enum thread_wakeup_reason {
	SRB_QUEUED,
	THREAD_TERMINATE
};

enum bounce_buffer_type {
	DEDICATED_KMALLOC,
	DEDICATED_SPECIAL,
};

struct ps3_stor_dev_info {
	struct list_head dev_list;
	struct ps3_stor_lv1_dev_info * lv1_dev_info;
	struct ps3_stor_host_info *host_info;
	const struct scsi_command_handler_info * handler_info;
	unsigned int target;

	u64 sector_size;	/* copied from lv1 repository at initialize */
	/* devices may change these value */
	struct rw_semaphore bounce_sem;	/* protect the following members:
					* bounce_buf (pointer itself, not buffer),
					* dedicated_bounce_size
					* max_sectors in scsi_dev->request_queue
					*/
	int  dedicated_bounce;	/* set nonzero if the bounce buffer is dedicated */
	int  dedicated_bounce_size;
	int  dedicated_dma_region; /* set if partial dma region allocated */
	enum bounce_buffer_type bounce_type;	/* bounce buffer type */
	void * bounce_buf;
	u64 separate_bounce_lpar; /* lpar address for separated buffer  */

	char used;

	/* main thread communication */
	struct task_struct * thread_struct;
	spinlock_t srb_lock;
	struct scsi_cmnd * srb;              /* queued srb; just one srb allowd             */
	struct semaphore thread_sema;        /* device main thread wakeup                   */
	struct completion thread_terminated; /* notify thread temination to slave_destory() */
	int thread_wakeup_reason;

	/* interrupt handler communication */
	struct completion irq_done;
	volatile u64 lv1_status;	/* result of get_async_status() */
	volatile int lv1_retval;	/* return value of get_async_status() */

};

struct ps3_stor_host_info {
	struct list_head host_list;
	struct Scsi_Host *scsi_host;
	struct platform_device dev;
	struct list_head dev_info_list;
	struct ps3_stor_lv1_bus_info * lv1_bus_info;
};

#define from_dev_to_ps3_stor_host(p) \
	container_of(p, struct ps3_stor_host_info, dev)
#define from_dev_to_scsi_device(p) \
	container_of(p, struct scsi_device, sdev_gendev)


struct ps3_stor_quirk_probe_info {
	struct completion irq_done;
	unsigned int device_id;
	int lv1_retval;
	u64 lv1_status;
	u64 lv1_tag;
	u64 lv1_ret_tag;
};


#define NOTIFICATION_DEVID ((u64)(-1L))

struct device_probe_info {
	unsigned int device_id;
	enum ps3_dev_type device_type;
	int      found;
	int      region_expected;
	int      region_ready;
};

#endif

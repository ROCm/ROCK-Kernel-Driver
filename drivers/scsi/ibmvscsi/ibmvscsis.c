/**************************************************************************/
/* -*- -linux- -*-                                                        */
/* IBM eServer i/pSeries Virtual SCSI Target Driver                       */
/* Copyright (C) 2003 Dave Boutcher (boutcher@us.ibm.com) IBM Corp.       */
/*                                                                        */
/*  This program is free software; you can redistribute it and/or modify  */
/*  it under the terms of the GNU General Public License as published by  */
/*  the Free Software Foundation; either version 2 of the License, or     */
/*  (at your option) any later version.                                   */
/*                                                                        */
/*  This program is distributed in the hope that it will be useful,       */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*  GNU General Public License for more details.                          */
/*                                                                        */
/*  You should have received a copy of the GNU General Public License     */
/*  along with this program; if not, write to the Free Software           */
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  */
/*                                                                   USA  */
/*                                                                        */
/* This module contains the eServer virtual SCSI target code.  The driver */
/* takes SRP requests from the virtual SCSI client (the linux version is  */
/* int ibmvscsi.c, but there can be other clients, like AIX or OF) and    */
/* passes them on to real devices in this system.                         */
/*                                                                        */
/* The basic hierarchy (and somewhat the organization of this file) is    */
/* that SCSI CDBs are in SRPs are in CRQs.                                */
/*                                                                        */
/**************************************************************************/
/*
  TODO:
  - Support redirecting SRP SCSI requests to a real SCSI driver
*/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/bio.h>

#include <asm/hvcall.h>
#include <asm/vio.h>
#include <asm/iommu.h>

#include "../scsi.h"
#include "viosrp.h"

MODULE_DESCRIPTION("IBM Virtual SCSI Target");
MODULE_AUTHOR("Dave Boutcher");
MODULE_LICENSE("GPL");

static int ibmvscsis_debug = 0;

/*
 * Quick macro to enable/disable interrupts
 * TODO: move to vio.h to be common with ibmvscsi.c
 */
#define h_vio_signal(ua, mode) \
  plpar_hcall_norets(H_VIO_SIGNAL, ua, mode)

/* 
 * These are indexes into the following table, and have to match!!!
 */
#define SENSE_SUCCESS       0
#define SENSE_ABORT         1
#define SENSE_INVALID_ID    2
#define SENSE_DEVICE_FAULT  3
#define SENSE_DEVICE_BUSY   4
#define SENSE_UNIT_OFFLINE  5
#define SENSE_INVALID_CMD   6
#define SENSE_INTERMEDIATE  7
#define SENSE_WRITE_PROT    8

static unsigned char ibmvscsis_sense_data[][3] = {
/*
 * Sense key lookup table
 * Format: SenseKey,AdditionalSenseCode,AdditionalSenseCodeQualifier
 * Adapted from 3w-xxxx.h
 */
	{0x00, 0x00, 0x00},	/* Success           */
	{0x0b, 0x00, 0x00},	/* Aborted command   */
	{0x0b, 0x14, 0x00},	/* ID not found      */
	{0x04, 0x00, 0x00},	/* Device fault      */
	{0x0b, 0x00, 0x00},	/* Device busy       */
	{0x02, 0x04, 0x00},	/* Unit offline      */
	{0x20, 0x04, 0x00},	/* Invalid Command   */
	{0x10, 0x00, 0x00},	/* Intermediate      */
	{0x07, 0x27, 0x00},	/* Write Protected   */
};

/*
 * SCSI defined structure for inquiry data
 * TODO: Seral number is currently messed up if you do
 *       scsiinfo.  I'm not sure why and I think it comes out of 
 *       here
 */
struct inquiry_data {
	u8 qual_type;
	u8 rmb_reserve;
	u8 version;
	u8 aerc_naca_hisup_format;
	u8 addl_len;
	u8 sccs_reserved;
	u8 bque_encserv_vs_multip_mchngr_reserved;
	u8 reladr_reserved_linked_cmdqueue_vs;
	char vendor[8];
	char product[16];
	char revision[4];
	char vendor_specific[20];
	char reserved1[2];
	char version_descriptor[16];
	char reserved2[22];
};

/*
 * Our proc dir entry under /proc/drivers.  We use proc to configure
 * this driver right now.
 * TODO: For 2.5 move to sysfs
 */
#ifdef CONFIG_PROC_FS
#define IBMVSCSIS_PROC_DIR "ibmvscsis"
static struct proc_dir_entry *ibmvscsis_proc_dir;
#endif

extern int vio_num_address_cells;

/* 
 * an RPA command/response transport queue.  This is our structure
 * that points to the actual queue.  feel free to modify this structure
 * as needed
 */
struct crq_queue {
	struct VIOSRP_CRQ *msgs;
	int size, cur;
	dma_addr_t msg_token;
	spinlock_t lock;
};

/*
 * This structure tracks our fundamental unit of work.  Whenever
 * an SRP Information Unit (IU) arrives, we track all the good stuff
 * here
 */
struct iu_entry {
	union VIOSRP_IU *iu;
	struct server_adapter *adapter;
	struct list_head next;
	dma_addr_t iu_token;
	int aborted;
	struct {
		dma_addr_t remote_token;
		char *data_buffer;
		dma_addr_t data_token;
		long data_len;
		struct vdev *vd;
		char in_use:1;
		char diunder:1;
		char diover:1;
		char dounder:1;
		char doover:1;
		char write:1;
		char linked:1;
		int data_out_residual_count;
		int data_in_residual_count;
		int ioerr;
	} req;
};

/* 
 * a pool of ius for use 
 */
struct iu_pool {
	spinlock_t lock;
	struct list_head iu_entries;
	struct iu_entry *list;
	union VIOSRP_IU *iu_storage;
	dma_addr_t iu_token;
	u32 size;
};

/*
 * Represents a single device that someone told us about
 * that we treat as a LUN
 */
struct vdev {
	struct list_head list;
	char type;		/* 'B' for block, 'S' for SCSI */
	atomic_t refcount;
	int disabled;
	u64 lun;
	struct {
		struct block_device *bdev;
		dev_t dev;
		long blksize;
		long lastlba;
		int ro;
	} b;
};

/*
 * Represents a bus.  target #'s in SCSI are 6 bits long,
 * so you can have 64 targets per bus
 */
#define TARGETS_PER_BUS (64)
#define BUS_PER_ADAPTER (8)
struct vbus {
	struct vdev *vdev[TARGETS_PER_BUS];
};

/*
 * Buffer cache
 */
struct dma_buffer {
	dma_addr_t token;
	char *addr;
	size_t len;
};
#define DMA_BUFFER_CACHE_SIZE (16)
#define DMA_BUFFER_INIT_COUNT (4)
#define DMA_BUFFER_INIT_LEN (PAGE_SIZE*16)

/* all driver data associated with a host adapter */
struct server_adapter {
	struct device *dev;
	struct vio_dev *dma_dev;
	struct crq_queue queue;
	struct tasklet_struct crq_tasklet;
	struct tasklet_struct endio_tasklet;
	atomic_t crq_task_count;	/* TODO: this is only for debugging. get rid of it */
	atomic_t endio_task_count;	/* TODO: this is only for debugging. get rid of it */
	struct iu_pool pool;
	spinlock_t lock;
	struct bio *bio_done;
	struct bio *bio_donetail;
	struct list_head inflight;
	struct vbus *vbus[8];
	int nvdevs;
	char name[32];
	unsigned long liobn;
	unsigned long riobn;

	/* This ugly expression allocates a bit array of 
	 * in-use flags large enough for the number of buffers
	 */
	unsigned long dma_buffer_use[(DMA_BUFFER_CACHE_SIZE +
				      sizeof(unsigned long) - 1)
				     / sizeof(unsigned long)];
	struct dma_buffer dma_buffer[DMA_BUFFER_CACHE_SIZE];

	/* Statistics only */
	atomic_t iu_count;	/* number allocated */
	atomic_t bio_count;	/* number allocated */
	atomic_t crq_processed;
	atomic_t interrupts;
	atomic_t read_processed;
	atomic_t write_processed;
	atomic_t buffers_allocated;
	atomic_t errors;
};

/* 
 * Forward declarations
 */
static long send_rsp(struct iu_entry *iue, int status);

/*
 * The following are lifted from usb.h
 */
#define DEBUG 1
#ifdef DEBUG
#define dbg(format, arg...) if (ibmvscsis_debug) printk(KERN_WARNING __FILE__ ": " format , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif
#define err(format, arg...) printk(KERN_ERR __FILE__ ": " format , ## arg)
#define info(format, arg...) printk(KERN_INFO __FILE__ ": " format  , ## arg)
#define warn(format, arg...) printk(KERN_WARNING __FILE__ ": " format , ## arg)

/* ==============================================================
 * Utility Routines
 * ==============================================================
 */
/*
 * return an 8 byte lun given a bus, target, lun.  
 * Today this only supports single level luns.  Should we add a level or a
 * 64 bit LUN as input to support multi-level luns?
 */
u64 make_lun(unsigned int bus, unsigned int target, unsigned int lun)
{
	u16 result = (0x8000 |
		      ((target & 0x003f) << 8) |
		      ((bus & 0x0007) << 5) | (lun & 0x001f));
	return ((u64) result) << 48;
}

/*
 * Given an 8 byte LUN, return the first level bus/target/lun.
 * Today this doesn't support multi-level LUNs
 */
#define GETBUS(x) ((int)((((u64)(x)) >> 53) & 0x0007))
#define GETTARGET(x) ((int)((((u64)(x)) >> 56) & 0x003f))
#define GETLUN(x) ((int)((((u64)(x)) >> 48) & 0x001f))

static u8 getcontrolbyte(u8 * cdb)
{
	return cdb[COMMAND_SIZE(cdb[0]) - 1];
}

static u8 getlink(struct iu_entry *iue)
{
	return (getcontrolbyte(iue->iu->srp.cmd.cdb) & 0x01);
}

/*
 * Given an SRP, figure out the data in length
 */
static int did_len(struct SRP_CMD *cmd)
{
	struct memory_descriptor *md;
	struct indirect_descriptor *id;
	int offset = cmd->additional_cdb_len * 4;

	switch (cmd->data_out_format) {
	case SRP_NO_BUFFER:
		offset += 0;
		break;
	case SRP_DIRECT_BUFFER:
		offset += sizeof(struct memory_descriptor);
		break;
	case SRP_INDIRECT_BUFFER:
		offset += sizeof(struct indirect_descriptor)
		    +
		    ((cmd->data_out_count -
		      1) * sizeof(struct memory_descriptor));
		break;
	default:
		err("ibmvscsis: did_len Invalid data_out_format %d\n",
		    cmd->data_out_format);
		return 0;
	}

	switch (cmd->data_in_format) {
	case SRP_NO_BUFFER:
		return 0;
	case SRP_DIRECT_BUFFER:
		md = (struct memory_descriptor *)(cmd->additional_data +
						  offset);
		return md->length;
	case SRP_INDIRECT_BUFFER:
		id = (struct indirect_descriptor *)(cmd->additional_data +
						    offset);
		return id->total_length;
	default:
		err("ibmvscsis: Invalid data_in_format %d\n",
		    cmd->data_in_format);
		return 0;
	}
}

/* 
 * We keep a pool of IUs, this routine builds the pool.  The pool is 
 * per-adapter.  The size of the pool is negotiated as part of the SRP
 * login, where we negotiate the number of requests (IUs) the client
 * can send us.  This routine is not synchronized.
 */
static int initialize_iu_pool(struct server_adapter *adapter, int size)
{
	struct iu_pool *pool = &adapter->pool;
	int i;

	pool->size = size;
	pool->lock = SPIN_LOCK_UNLOCKED;
	INIT_LIST_HEAD(&pool->iu_entries);

	pool->list = kmalloc(pool->size * sizeof(*pool->list), GFP_KERNEL);
	if (!pool->list) {
		err("Error: no memory for IU list\n");
		return -ENOMEM;
	}
	memset(pool->list, 0x00, pool->size * sizeof(*pool->list));

	pool->iu_storage =
	    dma_alloc_coherent(adapter->dev,
			       pool->size * sizeof(*pool->iu_storage),
			       &pool->iu_token, 0);
	if (!pool->iu_storage) {
		err("Error: no memory for IU pool\n");
		kfree(pool->list);
		return -ENOMEM;
	}

	for (i = 0; i < pool->size; ++i) {
		pool->list[i].iu = pool->iu_storage + i;
		pool->list[i].iu_token =
		    pool->iu_token + sizeof(*pool->iu_storage) * i;
		pool->list[i].adapter = adapter;
		list_add_tail(&pool->list[i].next, &pool->iu_entries);
	}

	return 0;
}

/*
 * Free the pool we allocated in initialize_iu_pool
 */
static void release_iu_pool(struct server_adapter *adapter)
{
	struct iu_pool *pool = &adapter->pool;
	int i, in_use = 0;
	for (i = 0; i < pool->size; ++i)
		if (pool->list[i].req.in_use)
			++in_use;
	if (in_use)
		err("ibmvscsis: releasing event pool with %d events still in use?\n", in_use);
	kfree(pool->list);
	dma_free_coherent(adapter->dev, pool->size * sizeof(*pool->iu_storage),
			  pool->iu_storage, pool->iu_token);
}

/*
 * Get an IU from the pool.  Return NULL of the pool is empty.  This
 * routine is syncronized by a lock.  The routine sets all the important
 * fields to 0
 */
static struct iu_entry *get_iu(struct server_adapter *adapter)
{
	struct iu_entry *e;
	unsigned long flags;

	spin_lock_irqsave(&adapter->pool.lock, flags);
	if (!list_empty(&adapter->pool.iu_entries)) {
		e = list_entry(adapter->pool.iu_entries.next, struct iu_entry,
			       next);
		list_del(adapter->pool.iu_entries.next);

		if (e->req.in_use) {
			err("Found in-use iue in pool!");
		}

		memset(&e->req, 0x00, sizeof(e->req));

		e->req.in_use = 1;
	} else {
		e = NULL;
	}

	spin_unlock_irqrestore(&adapter->pool.lock, flags);
	atomic_inc(&adapter->iu_count);
	return e;
}

/* 
 * Return an IU to the pool.  This routine is synchronized
 */
static void free_iu(struct iu_entry *iue)
{
	unsigned long flags;
	if (iue->req.vd) {
		atomic_dec(&iue->req.vd->refcount);
	}

	spin_lock_irqsave(&iue->adapter->pool.lock, flags);
	if (iue->req.in_use == 0) {
		warn("ibmvscsis: Internal error, freeing iue twice!\n");
	} else {
		iue->req.in_use = 0;
		list_add_tail(&iue->next, &iue->adapter->pool.iu_entries);
	}
	spin_unlock_irqrestore(&iue->adapter->pool.lock, flags);
	atomic_dec(&iue->adapter->iu_count);
}

/*
 * Get a CRQ from the inter-partition queue.
 */
static struct VIOSRP_CRQ *crq_queue_next_crq(struct crq_queue *queue)
{
	struct VIOSRP_CRQ *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
	} else
		crq = NULL;
	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}

/* 
 * Make the RDMA hypervisor call.  There should be a better way to do this
 * than inline assembler.
 * TODO: Fix the inline assembler
 */
static long h_copy_rdma(long length,
			unsigned long sliobn, unsigned long slioba,
			unsigned long dliobn, unsigned long dlioba)
{
	long lpar_rc = 0;
	__asm__ __volatile__(" li 3,0x110 \n\t"
			     " mr 4, %1 \n\t"
			     " mr 5, %2 \n\t"
			     " mr 6, %3 \n\t"
			     " mr 7, %4 \n\t"
			     " mr 8, %5 \n\t"
			     " .long 0x44000022 \n\t"
			     " mr %0, 3 \n\t":"=&r"(lpar_rc)
			     :"r"(length), "r"(sliobn), "r"(slioba),
			     "r"(dliobn), "r"(dlioba)
			     :"r0", "r3", "r4", "r5", "r6", "r7", "r8", "cr0",
			     "cr1", "ctr", "xer", "memory");
	return lpar_rc;
}

/*
 * Send an SRP to another partition using the CRQ.
 */
static int send_srp(struct iu_entry *iue, u64 length)
{
	long rc, rc1;
	union {
		struct VIOSRP_CRQ cooked;
		u64 raw[2];
	} crq;

	/* First copy the SRP */
	rc = h_copy_rdma(length,
			 iue->adapter->liobn,
			 iue->iu_token,
			 iue->adapter->riobn, iue->req.remote_token);

	if (rc) {
		err("Error: In send_srp, h_copy_rdma rc %ld\n", rc);
	}

	crq.cooked.valid = 0x80;
	crq.cooked.format = VIOSRP_SRP_FORMAT;
	crq.cooked.reserved = 0x00;
	crq.cooked.timeout = 0x00;
	crq.cooked.IU_length = length;
	crq.cooked.IU_data_ptr = iue->iu->srp.generic.tag;

	if (rc == 0) {
		crq.cooked.status = 0x99;	/* TODO: is this right? */
	} else {
		crq.cooked.status = 0x00;
	}

	rc1 =
	    plpar_hcall_norets(H_SEND_CRQ, iue->adapter->dma_dev->unit_address,
			       crq.raw[0], crq.raw[1]);

	if (rc1) {
		err("ibmvscscsis Error: In send_srp, h_send_crq rc %ld\n", rc1);
		return rc1;
	}

	return rc;
}

/*
 * Send data to a single SRP memory descriptor
 * Returns amount of data sent, or negative value on error
 */
static long send_md_data(dma_addr_t stoken, int len,
			 struct memory_descriptor *md,
			 struct server_adapter *adapter)
{
	int tosend;
	long rc;

	if (len < md->length)
		tosend = len;
	else
		tosend = md->length;

	rc = h_copy_rdma(tosend,
			 adapter->liobn,
			 stoken, adapter->riobn, md->virtual_address);

	if (rc != H_Success) {
		err("Error sending data with h_copy_rdma, rc %ld\n", rc);
		return -1;
	}

	return tosend;
}

/*
 * Send data to the SRP data_in buffers
 * Returns amount of data sent, or negative value on error
 */
static long send_cmd_data(dma_addr_t stoken, int len, struct iu_entry *iue)
{
	struct SRP_CMD *cmd = &iue->iu->srp.cmd;
	struct memory_descriptor *md;
	struct indirect_descriptor *id;
	int offset = 0;
	int total_length = 0;
	int i;
	int thislen;
	int bytes;
	int sentlen = 0;

	offset = cmd->additional_cdb_len * 4;

	switch (cmd->data_out_format) {
	case SRP_NO_BUFFER:
		offset += 0;
		break;
	case SRP_DIRECT_BUFFER:
		offset += sizeof(struct memory_descriptor);
		break;
	case SRP_INDIRECT_BUFFER:
		offset += sizeof(struct indirect_descriptor)
		    +
		    ((cmd->data_out_count -
		      1) * sizeof(struct memory_descriptor));
		break;
	default:
		err("Error: did_len Invalid data_out_format %d\n",
		    cmd->data_out_format);
		return 0;
	}

	switch (cmd->data_in_format) {
	case SRP_NO_BUFFER:
		return 0;
	case SRP_DIRECT_BUFFER:
		md = (struct memory_descriptor *)(cmd->additional_data +
						  offset);
		return send_md_data(stoken, len, md, iue->adapter);
	}

	if (cmd->data_in_format != SRP_INDIRECT_BUFFER) {
		err("Error: send_cmd_data Invalid data_in_format %d\n",
		    cmd->data_in_format);
		return 0;
	}

	id = (struct indirect_descriptor *)(cmd->additional_data + offset);

	total_length = id->total_length;

	/* Work through the partial memory descriptor list */
	for (i = 0; ((i < cmd->data_in_count) && (len)); i++) {
		if (len > id->list[i].length) {
			thislen = id->list[i].length;
		} else {
			thislen = len;
		}

		bytes =
		    send_md_data(stoken + sentlen, thislen, id->list + i,
				 iue->adapter);
		if (bytes < 0)
			return bytes;

		if (bytes != thislen) {
			warn("Error: Tried to send %d, sent %d\n", thislen,
			     bytes);
		}

		sentlen += bytes;
		total_length -= bytes;
		len -= bytes;
	}

	if (len) {
		warn("Left over data sending to indirect buffer\n");
		iue->req.diover = 1;
		iue->req.data_in_residual_count = len;
	}

	return sentlen;
}

/*
 * Get data from the other partition from a single SRP memory descriptor
 * Returns amount of data sent, or negative value on error
 */
static long get_md_data(dma_addr_t ttoken, int len,
			struct memory_descriptor *md,
			struct server_adapter *adapter)
{
	int toget;
	long rc;

	if (len < md->length)
		toget = len;
	else
		toget = md->length;

	rc = h_copy_rdma(toget,
			 adapter->riobn,
			 md->virtual_address, adapter->liobn, ttoken);

	if (rc != H_Success) {
		err("Error sending data with h_copy_rdma, rc %ld\n", rc);
		return -1;
	}

	return toget;
}

/*
 * Get data from an SRP data in area.
 * Returns amount of data sent, or negative value on error
 */
static long get_cmd_data(dma_addr_t stoken, int len, struct iu_entry *iue)
{
	struct SRP_CMD *cmd = &iue->iu->srp.cmd;
	struct memory_descriptor *md;
	struct indirect_descriptor *id;
	int offset = 0;
	int total_length = 0;
	int i;
	int thislen;
	int bytes;
	int sentlen = 0;

	offset = cmd->additional_cdb_len * 4;

	switch (cmd->data_out_format) {
	case SRP_NO_BUFFER:
		return 0;
		break;
	case SRP_DIRECT_BUFFER:
		md = (struct memory_descriptor *)(cmd->additional_data +
						  offset);
		return get_md_data(stoken, len, md, iue->adapter);
		break;
	}

	if (cmd->data_out_format != SRP_INDIRECT_BUFFER) {
		err("get_cmd_data Invalid data_out_format %d\n",
		    cmd->data_out_format);
		return 0;
	}

	id = (struct indirect_descriptor *)(cmd->additional_data + offset);

	total_length = id->total_length;

	/* Work through the partial memory descriptor list */
	for (i = 0; ((i < cmd->data_out_count) && (len)); i++) {
		if (len > id->list[i].length) {
			thislen = id->list[i].length;
		} else {
			thislen = len;
		}

		bytes =
		    get_md_data(stoken + sentlen, thislen, id->list + i,
				iue->adapter);
		if (bytes < 0)
			return bytes;

		if (bytes != thislen) {
			err("Tried to send %d, sent %d\n", thislen, bytes);
		}

		sentlen += bytes;
		total_length -= bytes;
		len -= bytes;
	}

	if (len) {
		warn("Left over data get indirect buffer\n");
	}

	return sentlen;
}

/*
 * Get some data buffers to start.  This doesn't lock the adapter structure!
 */
static void init_data_buffer(struct server_adapter *adapter)
{
	int i;

	for (i = 0; i < DMA_BUFFER_INIT_COUNT; i++) {
		if (adapter->dma_buffer[i].addr == NULL) {
			adapter->dma_buffer[i].addr = (char *)
			    dma_alloc_coherent(adapter->dev,
					       DMA_BUFFER_INIT_LEN,
					       &adapter->dma_buffer[i].token,
					       0);
			adapter->dma_buffer[i].len = DMA_BUFFER_INIT_LEN;
			dbg("data buf %p token %8.8x, len %ld\n",
			    adapter->dma_buffer[i].addr,
			    adapter->dma_buffer[i].token,
			    adapter->dma_buffer[i].len);
			atomic_inc(&adapter->buffers_allocated);
		}
	}

	return;
}

/*
 * Get a memory buffer that includes a mapped TCE.  
 */
static void get_data_buffer(char **buffer, dma_addr_t * data_token, size_t len,
			    struct server_adapter *adapter)
{
	int i;

	for (i = 0; i < DMA_BUFFER_CACHE_SIZE; i++) {
		if ((adapter->dma_buffer[i].addr) &&
		    (adapter->dma_buffer[i].len >= len) &&
		    (!test_and_set_bit(i, adapter->dma_buffer_use))) {
			*buffer = adapter->dma_buffer[i].addr;
			*data_token = adapter->dma_buffer[i].token;
			return;
		}
	}

	/* Couldn't get a buffer!  Try and get a new one */
	*buffer = (char *)dma_alloc_coherent(adapter->dev, len, data_token, 0);
	atomic_inc(&adapter->buffers_allocated);
	dbg("get:  %p, %8.8x, %ld\n", *buffer, *data_token, len);
	return;
}

/*
 * Free a memory buffer that includes a mapped TCE.  
 */
static void free_data_buffer(char *buffer, dma_addr_t data_token, size_t len,
			     struct server_adapter *adapter)
{
	int i;

	/* First see if this buffer is already in the cache */
	for (i = 0; i < DMA_BUFFER_CACHE_SIZE; i++) {
		if (adapter->dma_buffer[i].addr == buffer) {
			if (adapter->dma_buffer[i].token != data_token) {
				err("Incoherent data buffer pool info!\n");
			}
			if (!test_and_clear_bit(i, adapter->dma_buffer_use)) {
				err("Freeing data buffer twice!\n");
			}
			return;
		}
	}

	/* See if there is an empty slot in our list */
	for (i = 0; i < DMA_BUFFER_CACHE_SIZE; i++) {
		if (!test_and_set_bit(i, adapter->dma_buffer_use)) {
			if (adapter->dma_buffer[i].addr == NULL) {
				adapter->dma_buffer[i].addr = buffer;
				adapter->dma_buffer[i].token = data_token;
				adapter->dma_buffer[i].len = len;
				clear_bit(i, adapter->dma_buffer_use);
				return;
			} else {
				clear_bit(i, adapter->dma_buffer_use);
			}
		}
	}

	/* Now see if there is a smaller buffer we should throw out */
	for (i = 0; i < DMA_BUFFER_CACHE_SIZE; i++) {
		if (!test_and_set_bit(i, adapter->dma_buffer_use)) {
			if (adapter->dma_buffer[i].len < len) {
				dbg("fre1: %p, %8.8x, %ld\n",
				    adapter->dma_buffer[i].addr,
				    adapter->dma_buffer[i].token,
				    adapter->dma_buffer[i].len);

				dma_free_coherent(adapter->dev,
						  adapter->dma_buffer[i].len,
						  adapter->dma_buffer[i].addr,
						  adapter->dma_buffer[i].token);

				atomic_dec(&adapter->buffers_allocated);

				adapter->dma_buffer[i].addr = buffer;
				adapter->dma_buffer[i].token = data_token;
				adapter->dma_buffer[i].len = len;
				clear_bit(i, adapter->dma_buffer_use);
				return;
			} else {
				clear_bit(i, adapter->dma_buffer_use);
			}
		}
	}

	/* No space to cache this.  Give it back to the kernel */
	dbg("fre2: %p, %8.8x, %ld\n", buffer, data_token, len);
	dma_free_coherent(adapter->dev, len, buffer, data_token);
	atomic_dec(&adapter->buffers_allocated);
}

/*
 * Release all the data buffers
 */
static void release_data_buffer(struct server_adapter *adapter)
{
	int i;
	int free_in_use = 0;

	for (i = 0; i < DMA_BUFFER_INIT_COUNT; i++) {
		if (adapter->dma_buffer[i].addr != NULL) {
			if (test_bit(i, adapter->dma_buffer_use)) {
				free_in_use++;
			}
			dma_free_coherent(adapter->dev,
					  adapter->dma_buffer[i].len,
					  adapter->dma_buffer[i].addr,
					  adapter->dma_buffer[i].token);

			atomic_dec(&adapter->buffers_allocated);
		}
	}

	if (free_in_use) {
		err("Freeing %d in-use data buffers\n", free_in_use);
	}
	return;
}

/*
 * the routine that gets called on end_io of our bios.  We basically
 * schedule the processing to be done in our task, since we don't want
 * do things like RDMA in someone else's interrupt handler
 *
 * Each iu request may result in multiple bio requests.  only proceed
 * when all the bio requests have done.
 */
static int ibmvscsis_end_io(struct bio *bio, unsigned int nbytes, int error)
{
	struct iu_entry *iue = (struct iu_entry *)bio->bi_private;
	struct server_adapter *adapter = iue->adapter;
	unsigned long flags;

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags)) {
		iue->req.ioerr = 1;
	};

	/* Add the bio to the done queue */
	spin_lock_irqsave(&adapter->lock, flags);
	if (adapter->bio_donetail) {
		adapter->bio_donetail->bi_next = bio;
		adapter->bio_donetail = bio;
	} else
		adapter->bio_done = adapter->bio_donetail = bio;
	spin_unlock_irqrestore(&adapter->lock, flags);

	/* Schedule the task */
	tasklet_schedule(&adapter->endio_tasklet);

	return 0;
}

/*
 * Find the vdev structure from the LUN field in an SRP IUE
 * Note that this routine bumps a refcount field in the vdev.
 * Normally this is done when free_iu is called.
 */
static struct vdev *find_device(struct iu_entry *iue)
{
	u16 *lun = (u16 *) & iue->iu->srp.cmd.lun;
	u32 bus = (lun[0] & 0x00E0) >> 5;
	u32 target = (lun[0] & 0x3F00) >> 8;
	u32 slun = (lun[0] & 0x001F);
	struct vdev *vd;
	unsigned long flags;

	/* If asking for a lun other than 0, return nope */
	if (slun) {
		return NULL;
	}

	/* Only from SRP CMD */
	if (iue->iu->srp.generic.type != SRP_CMD_TYPE)
		return NULL;

	/* if not a recognized LUN format, return NULL */
	if ((lun[0] & 0xC000) != 0x8000)
		return NULL;

	spin_lock_irqsave(&iue->adapter->lock, flags);
	if (iue->adapter->vbus[bus] == NULL) {
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		return NULL;
	}

	vd = iue->adapter->vbus[bus]->vdev[target];

	if ((vd == NULL) || (vd->disabled)) {
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		return NULL;
	}

	if (vd) {
		atomic_inc(&vd->refcount);
	}
	spin_unlock_irqrestore(&iue->adapter->lock, flags);

	return vd;
}

/*
 * Process BH buffer completions.  When the end_io routine gets called 
 * we queue the bio on an internal queue and start a task to process them
 */
static void endio_task(unsigned long data)
{
	struct server_adapter *adapter = (struct server_adapter *)data;
	struct iu_entry *iue;
	struct bio *bio;
	int bytes;
	unsigned long flags;

	if (atomic_inc_return(&adapter->endio_task_count) > 1) {
		err("In endio_task twice!!!\n");
	}

	do {
		spin_lock_irqsave(&adapter->lock, flags);
		if ((bio = adapter->bio_done)) {
			if (bio == adapter->bio_donetail)
				adapter->bio_donetail = NULL;
			adapter->bio_done = bio->bi_next;
			bio->bi_next = NULL;
		}
		if (bio) {
			/* Remove this iue from the in-flight list */
			iue = (struct iu_entry *)bio->bi_private;
			list_del(&iue->next);
		}

		spin_unlock_irqrestore(&adapter->lock, flags);

		if (bio) {
			/* Send back the SRP and data if this request was NOT
			 * aborted 
			 */
			if (!iue->aborted) {

				if (!iue->req.ioerr) {
					/* return data if this was a read */
					if (!iue->req.write) {
						bytes =
						    send_cmd_data(iue->req.
								  data_token,
								  iue->req.
								  data_len,
								  iue);
						if (bytes != iue->req.data_len) {
							err("Error sending data on response (tried %d, sent %d\n", bio->bi_size, bytes);
							send_rsp(iue,
								 SENSE_ABORT);
						} else {
							send_rsp(iue,
								 SENSE_SUCCESS);
						}
					} else {
						send_rsp(iue, SENSE_SUCCESS);
					}
				} else {
					err("Block operation failed\n");
					print_command(iue->iu->srp.cmd.cdb);
					send_rsp(iue, SENSE_DEVICE_FAULT);
				}
			}

			free_data_buffer(iue->req.data_buffer,
					 iue->req.data_token, iue->req.data_len,
					 adapter);

			free_iu(iue);

			bio_put(bio);
			atomic_dec(&adapter->bio_count);
		}
	} while (bio);
	atomic_dec(&adapter->endio_task_count);
}

/* ==============================================================
 * SCSI Command Emulation Routines
 * ==============================================================
 */

/*
 * Process an inquiry SCSI Command
 */
static void process_inquiry(struct iu_entry *iue)
{
	struct inquiry_data *id;
	dma_addr_t data_token;
	int bytes;

	id = (struct inquiry_data *)dma_alloc_coherent(iue->adapter->dev,
						       sizeof(*id),
						       &data_token, 0);
	memset(id, 0x00, sizeof(*id));

	/* If we have a valid device */
	if (iue->req.vd) {
		dbg("  inquiry returning device\n");
		id->qual_type = 0x00;	/* Direct Access    */
		id->rmb_reserve = 0x00;	/* TODO: CD is removable  */
		id->version = 0x84;	/* ISO/IE                 */
		id->aerc_naca_hisup_format = 0x22;	/* naca & format 0x02 */
		id->addl_len = sizeof(*id) - 4;	/* sizeof(*this) - 4 */
		id->bque_encserv_vs_multip_mchngr_reserved = 0x00;
		id->reladr_reserved_linked_cmdqueue_vs = 0x02;	/* CMDQ   */
		memcpy(id->vendor, "IBM     ", 8);
		memcpy(id->product, "VSCSI blkdev    ", 16);
		memcpy(id->revision, "0001", 4);
	} else {
		dbg("  inquiry returning no device\n");
		id->qual_type = 0x7F;	/* Not supported, no device */
	}

	bytes = send_cmd_data(data_token, sizeof(*id), iue);

	dma_free_coherent(iue->adapter->dev, sizeof(*id), id, data_token);

	if (bytes < 0) {
		send_rsp(iue, SENSE_DEVICE_FAULT);
	} else {
		send_rsp(iue, SENSE_SUCCESS);
	}

	free_iu(iue);
}

/*
 * Handle an I/O.  Called by WRITE6, WRITE10, etc
 */
static void process_rw(char *cmd, int rw, struct iu_entry *iue, long lba,
		       long len)
{
	char *buffer;
	struct bio *bio;
	int bytes;
	int num_biovec;
	int cur_biovec;
	long flags;

	dbg("%s %16.16lx[%d:%d:%d][%d:%d] lba %ld len %ld reladr %d link %d\n",
	    cmd,
	    iue->iu->srp.cmd.lun,
	    GETBUS(iue->iu->srp.cmd.lun),
	    GETTARGET(iue->iu->srp.cmd.lun),
	    GETLUN(iue->iu->srp.cmd.lun),
	    MAJOR(iue->req.vd->b.dev),
	    MINOR(iue->req.vd->b.dev),
	    lba,
	    len / iue->req.vd->b.blksize,
	    iue->iu->srp.cmd.cdb[1] & 0x01, iue->req.linked);

	if (rw == WRITE) {
		atomic_inc(&iue->adapter->write_processed);
	} else if (rw == READ) {
		atomic_inc(&iue->adapter->read_processed);
	} else {
		err("Major internal error...rw not read or write\n");
		send_rsp(iue, SENSE_DEVICE_FAULT);

		free_iu(iue);
		return;
	}

	if (len == 0) {
		warn("Zero length I/O\n");
		send_rsp(iue, SENSE_INVALID_CMD);

		free_iu(iue);
		return;
	}

	/* Writing to a read-only device */
	if ((rw == WRITE) && (iue->req.vd->b.ro)) {
		warn("WRITE op to r/o device\n");
		send_rsp(iue, SENSE_WRITE_PROT);

		free_iu(iue);
		return;
	}

	get_data_buffer(&buffer, &iue->req.data_token, len, iue->adapter);
	iue->req.data_buffer = buffer;
	iue->req.data_len = len;
	if (buffer == NULL) {
		err("Not able to get a data buffer (%lu pages)\n",
		    len / PAGE_SIZE);
		send_rsp(iue, SENSE_DEVICE_FAULT);

		free_iu(iue);
		return;
	}

	/* if reladr */
	if (iue->iu->srp.cmd.cdb[1] & 0x01) {
		lba = lba + iue->req.vd->b.lastlba;
	}

	/* If this command is linked, Keep this lba */
	if (iue->req.linked) {
		iue->req.vd->b.lastlba = lba;
	} else {
		iue->req.vd->b.lastlba = 0;
	}

	if (rw == WRITE) {
		iue->req.write = 1;
		/* Get the data */
		bytes = get_cmd_data(iue->req.data_token, len, iue);
		if (bytes != len) {
			err("Error transferring data\n");
			send_rsp(iue, SENSE_DEVICE_FAULT);

			free_iu(iue);
			return;
		}
	}

	num_biovec = (len - 1) / PAGE_CACHE_SIZE + 1;

	bio = bio_alloc(GFP_ATOMIC, num_biovec);
	if (!bio) {
		/* Ouch.  couldn't get a bio.  Mark this I/O as 
		 * in error, then decrement the outstanding bio.
		 * If there are still outstanding bio, they will send
		 * the error and free the IU.  If there are none, we
		 * should do it here
		 */
		iue->req.ioerr = 1;
		err("Not able to get a bio\n");
		send_rsp(iue, SENSE_DEVICE_FAULT);
		free_iu(iue);
		return;
	}

	iue->aborted = 0;
	spin_lock_irqsave(&iue->adapter->lock, flags);
	list_add_tail(&iue->next, &iue->adapter->inflight);
	spin_unlock_irqrestore(&iue->adapter->lock, flags);

	atomic_inc(&iue->adapter->bio_count);
	bio->bi_size = len;
	bio->bi_bdev = iue->req.vd->b.bdev;
	bio->bi_sector = lba;
	bio->bi_end_io = &ibmvscsis_end_io;
	bio->bi_private = iue;
	bio->bi_rw = (rw == WRITE) ? 1 : 0;
	bio->bi_phys_segments = 1;
	bio->bi_hw_segments = 1;

	/* This all assumes that the buffers we get are page-aligned */
	for (cur_biovec = 0; cur_biovec < num_biovec; cur_biovec++) {
		long thislen;

		if (len > PAGE_CACHE_SIZE) {
			thislen = PAGE_CACHE_SIZE;
		} else {
			thislen = len;
		}

		bio->bi_io_vec[cur_biovec].bv_page = virt_to_page(buffer);
		bio->bi_io_vec[cur_biovec].bv_len = thislen;
		bio->bi_io_vec[cur_biovec].bv_offset =
		    (unsigned long)buffer & PAGE_OFFSET_MASK;
		bio->bi_vcnt++;

		len -= thislen;
		buffer += thislen;
	}
	generic_make_request(bio);
}

/*
 * Process a READ6
 */
static void processRead6(struct iu_entry *iue)
{
	long lba = (*((u32 *) (iue->iu->srp.cmd.cdb))) & 0x001FFFFF;
	long len = iue->iu->srp.cmd.cdb[4];

	/* Length of 0 indicates 256 */
	if (len == 0) {
		len = 256;
	}

	len = len * iue->req.vd->b.blksize;

	process_rw("Read6", READ, iue, lba, len);
}

/*
 * Process a READ10
 */
static void processRead10(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u16 *) (iue->iu->srp.cmd.cdb + 7)) * iue->req.vd->b.blksize;

	process_rw("Read10", READ, iue, lba, len);
}

/*
 * Process a READ10
 */
static void processRead12(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u32 *) (iue->iu->srp.cmd.cdb + 6)) * iue->req.vd->b.blksize;

	process_rw("Read12", READ, iue, lba, len);
}

static void processWrite6(struct iu_entry *iue)
{
	long lba = (*((u32 *) (iue->iu->srp.cmd.cdb))) & 0x001FFFFF;
	long len = iue->iu->srp.cmd.cdb[4];

	/* Length of 0 indicates 256 */
	if (len == 0) {
		len = 256;
	}

	len = len * iue->req.vd->b.blksize;

	process_rw("Write6", WRITE, iue, lba, len);
}

static void processWrite10(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u16 *) (iue->iu->srp.cmd.cdb + 7)) * iue->req.vd->b.blksize;

	process_rw("Write10", WRITE, iue, lba, len);
}

static void processWrite12(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u32 *) (iue->iu->srp.cmd.cdb + 6)) * iue->req.vd->b.blksize;

	process_rw("Write12", WRITE, iue, lba, len);
}

/*
 * Handle Read Capacity
 */
static void processReadCapacity(struct iu_entry *iue)
{
	struct ReadCapacityData {
		u32 blocks;
		u32 blocksize;
	} *cap;
	dma_addr_t data_token;
	int bytes;

	cap = (struct ReadCapacityData *)dma_alloc_coherent(iue->adapter->dev,
							    sizeof(*cap),
							    &data_token, 0);

	/* return block size and last valid block */
	cap->blocksize = iue->req.vd->b.blksize;
	cap->blocks = iue->req.vd->b.bdev->bd_inode->i_size
	    / iue->req.vd->b.blksize 
	    - 1;

	info("Reporting capacity as %u block of size %u\n", cap->blocks,
	     cap->blocksize);

	bytes = send_cmd_data(data_token, sizeof(*cap), iue);

	dma_free_coherent(iue->adapter->dev, sizeof(*cap), cap, data_token);

	if (bytes != sizeof(*cap)) {
		err("Error sending read capacity data. bytes %d, wanted %ld\n",
		    bytes, sizeof(*cap));
	}

	send_rsp(iue, SENSE_SUCCESS);

	free_iu(iue);
}

/*
 * Process Mode Sense
 * TODO: I know scsiinfo asks for a bunch of mode pages not implemented here.
 *       Also, we need to act differently for virtual disk and virtual CD
 */
#define MODE_SENSE_BUFFER_SIZE (512)
static void processModeSense(struct iu_entry *iue)
{
	dma_addr_t data_token;
	int bytes;

	u8 *mode = (u8 *) dma_alloc_coherent(iue->adapter->dev,
					     MODE_SENSE_BUFFER_SIZE,
					     &data_token, 0);
	/* which page */
	switch (iue->iu->srp.cmd.cdb[2]) {
	case 0:
	case 0x3f:
		mode[1] = 0x00;	/* Default medium */
		if (iue->req.vd->b.ro) {
			mode[2] = 0x80;	/* device specific  */
		} else {
			mode[2] = 0x00;	/* device specific  */
		}
		/* note the DPOFUA bit is set to zero! */
		mode[3] = 0x08;	/* block descriptor length */
		*((u32 *) & mode[4]) = iue->req.vd->b.bdev->bd_inode->i_size /
		    iue->req.vd->b.blksize;
		*((u32 *) & mode[8]) = iue->req.vd->b.blksize;
		bytes = mode[0] = 12;	/* length */
		break;

	case 0x08:		/* Cache page */
		/* length should be 4 */
		if (iue->iu->srp.cmd.cdb[4] != 4
		    && iue->iu->srp.cmd.cdb[4] != 0x20) {
			send_rsp(iue, SENSE_INVALID_CMD);
			dma_free_coherent(iue->adapter->dev,
					  MODE_SENSE_BUFFER_SIZE,
					  mode, data_token);
			free_iu(iue);
			return;
		}

		mode[1] = 0x00;	/* Default medium */
		if (iue->req.vd->b.ro) {
			mode[2] = 0x80;	/* device specific */
		} else {
			mode[2] = 0x00;	/* device specific */
		}
		/* note the DPOFUA bit is set to zero! */
		mode[3] = 0x08;	/* block descriptor length */
		*((u32 *) & mode[4]) = iue->req.vd->b.bdev->bd_inode->i_size /
		    iue->req.vd->b.blksize;
		*((u32 *) & mode[8]) = iue->req.vd->b.blksize;

		/* Cache page */
		mode[12] = 0x08;	/* page */
		mode[13] = 0x12;	/* page length */
		mode[14] = 0x01;	/* no cache (0x04 for read/write cache) */

		bytes = mode[0] = 12 + mode[13];	/* length */
		break;
	default:
		warn("Request for unknown mode page %d\n",
		     iue->iu->srp.cmd.cdb[2]);
		send_rsp(iue, SENSE_INVALID_CMD);
		dma_free_coherent(iue->adapter->dev,
				  MODE_SENSE_BUFFER_SIZE, mode, data_token);
		free_iu(iue);
		return;
	}

	bytes = send_cmd_data(data_token, bytes, iue);

	dma_free_coherent(iue->adapter->dev,
			  MODE_SENSE_BUFFER_SIZE, mode, data_token);

	send_rsp(iue, SENSE_SUCCESS);

	free_iu(iue);
	return;
}

/*
 * Report LUNS command.
 */
static void processReportLUNs(struct iu_entry *iue)
{
	int listsize = did_len(&iue->iu->srp.cmd);
	dma_addr_t data_token;
	int index = 2;		/* Start after the two entries (length and LUN0) */
	int bus;
	int target;
	int bytes;
	unsigned long flags;

	u64 *lunlist = (u64 *) dma_alloc_coherent(iue->adapter->dev,
						  listsize,
						  &data_token, 0);

	memset(lunlist, 0x00, listsize);

	/* work out list size in units of u64 */
	listsize = listsize / 8;

	if (listsize < 1) {
		warn("report luns buffer too small\n");
		send_rsp(iue, SENSE_INVALID_CMD);
		free_iu(iue);
	}

	spin_lock_irqsave(&iue->adapter->lock, flags);

	/* send lunlist of size 1 when requesting lun is not all zeros */
	if (iue->iu->srp.cmd.lun != 0x0LL) {
		*lunlist = ((u64) 1 * 8) << 32;
		goto send_lunlist;
	}

	/* return the total number of luns plus LUN0 in bytes */
	*lunlist = (((u64) ((iue->adapter->nvdevs + 1) * 8)) << 32);

	dbg("reporting %d luns\n", iue->adapter->nvdevs + 1);
	/* loop through the bus */
	for (bus = 0; bus < BUS_PER_ADAPTER; bus++) {
		/* If this bus exists */
		if (iue->adapter->vbus[bus]) {
			/* loop through the targets */
			for (target = 0; target < TARGETS_PER_BUS; target++) {
				/* If the target exists */
				if (iue->adapter->vbus[bus]->vdev[target]) {
					if ((index < listsize) &&
					    (!iue->adapter->vbus[bus]->
					     vdev[target]->disabled)) {
						lunlist[index++] =
						    iue->adapter->vbus[bus]->
						    vdev[target]->lun;
						dbg("  lun %16.16lx\n",
						    iue->adapter->vbus[bus]->
						    vdev[target]->lun);
					}
				}
			}
		}
	}

      send_lunlist:
	spin_unlock_irqrestore(&iue->adapter->lock, flags);

	bytes = send_cmd_data(data_token, (index * 8), iue);

	dma_free_coherent(iue->adapter->dev, listsize * 8, lunlist, data_token);

	if (bytes != (index * 8)) {
		err("Error sending report luns data. bytes %d, wanted %d\n",
		    bytes, index * 4);
		send_rsp(iue, SENSE_ABORT);
	} else {
		send_rsp(iue, SENSE_SUCCESS);
	}

	free_iu(iue);
	return;
}

/*
 * Process an IU.  
 *
 * Note that THIS routine is responsible for returning the IU from the pool
 * The current assumption is that all the process routines called from here
 * are, in turn, responsible for freeing the IU
 */
static void process_cmd(struct iu_entry *iue)
{
	union VIOSRP_IU *iu = iue->iu;

	iue->req.vd = find_device(iue);

	if ((iue->req.vd == NULL) &&
	    (iu->srp.cmd.cdb[0] != REPORT_LUNS) &&
	    (iu->srp.cmd.cdb[0] != INQUIRY)) {
		dbg("Cmd %2.2x for unknown LUN %16.16lx\n",
		    iu->srp.cmd.cdb[0], iue->iu->srp.cmd.lun);
		send_rsp(iue, SENSE_INVALID_ID);
		free_iu(iue);
		return;
	}

	iue->req.linked = getlink(iue);

	switch (iu->srp.cmd.cdb[0]) {
	case READ_6:
		processRead6(iue);
		break;
	case READ_10:
		processRead10(iue);
		break;
	case READ_12:
		processRead12(iue);
		break;
	case WRITE_6:
		processWrite6(iue);
		break;
	case WRITE_10:
		processWrite10(iue);
		break;
	case WRITE_12:
		processWrite12(iue);
		break;
	case REPORT_LUNS:
		dbg("REPORT LUNS lun %16.16lx\n", iue->iu->srp.cmd.lun);
		processReportLUNs(iue);
		break;
	case INQUIRY:
		dbg("INQUIRY lun %16.16lx\n", iue->iu->srp.cmd.lun);
		process_inquiry(iue);
		break;
	case READ_CAPACITY:
		dbg("READ CAPACITY lun %16.16lx\n", iue->iu->srp.cmd.lun);
		processReadCapacity(iue);
		break;
	case MODE_SENSE:
		dbg("MODE SENSE lun %16.16lx\n", iue->iu->srp.cmd.lun);
		processModeSense(iue);
		break;
	case TEST_UNIT_READY:
		/* we already know the device exists */
		dbg("TEST UNIT READY lun %16.16lx\n", iue->iu->srp.cmd.lun);
		send_rsp(iue, SENSE_SUCCESS);
		free_iu(iue);
		break;
	case START_STOP:
		/* just respond OK */
		dbg("START_STOP lun %16.16lx\n", iue->iu->srp.cmd.lun);
		send_rsp(iue, SENSE_SUCCESS);
		free_iu(iue);
		break;
	default:
		warn("Unsupported SCSI Command 0x%2.2x\n", iu->srp.cmd.cdb[0]);
		send_rsp(iue, SENSE_INVALID_CMD);
		free_iu(iue);
	}
}

u16 send_adapter_info(struct iu_entry *iue,
		      dma_addr_t remote_buffer, u16 length)
{
	dma_addr_t data_token;
	int bytes;
	struct device_node *rootdn;
	const char *partition_name = "";
	unsigned int *p_number_ptr;
	struct MAD_ADAPTER_INFO_DATA *info =
	    (struct MAD_ADAPTER_INFO_DATA *)dma_alloc_coherent(iue->adapter->
							       dev,
							       sizeof(*info),
							       &data_token, 0);

	dbg("in send_adapter_info\n ");
	rootdn = find_path_device("/");
	if ((info) && (!dma_mapping_error(data_token))) {
		memset(info, 0x00, sizeof(*info));

		dbg("building adapter_info\n ");
		strcpy(info->srp_version, "1.6a");
		partition_name =
		    get_property(rootdn, "ibm,partition-name", NULL);
		if (partition_name)
			strncpy(info->partition_name, partition_name,
				sizeof(info->partition_name));
		p_number_ptr =
		    (unsigned int *)get_property(rootdn, "ibm,partition-no",
						 NULL);
		if (p_number_ptr)
			info->partition_number = *p_number_ptr;
		info->mad_version = 1;
		info->os_type = 3;

		bytes = send_cmd_data(data_token, sizeof(*info), iue);

		dma_free_coherent(iue->adapter->dev,
				  sizeof(*info), info, data_token);
	} else {
		dbg("bad dma_alloc_cohereint in adapter_info\n ");
		bytes = -1;
	}

	if (bytes < 0) {
		return 1;
	} else {
		return 0;
	}

}

/* ==============================================================
 * SRP Processing Routines
 * ==============================================================
 */
/*
 * Process an incoming SRP Login request
 */
static void process_login(struct iu_entry *iue)
{
	union VIOSRP_IU *iu = iue->iu;
	u64 tag = iu->srp.generic.tag;

	/* TODO handle case that requested size is wrong and buffer format is wrong */
	memset(iu, 0x00, sizeof(struct SRP_LOGIN_RSP));
	iu->srp.login_rsp.type = SRP_LOGIN_RSP_TYPE;
	iu->srp.login_rsp.request_limit_delta = iue->adapter->pool.size;
	iu->srp.login_rsp.tag = tag;
	iu->srp.login_rsp.max_initiator_to_target_iulen = sizeof(union SRP_IU);
	iu->srp.login_rsp.max_target_to_initiator_iulen = sizeof(union SRP_IU);
	iu->srp.login_rsp.supported_buffer_formats = 0x0002;	/* direct and indirect */
	iu->srp.login_rsp.multi_channel_result = 0x00;	/* TODO fix if we were already logged in */

	send_srp(iue, sizeof(iu->srp.login_rsp));
}

/*
 * Send an SRP response that includes sense data
 */
static long send_rsp(struct iu_entry *iue, int status)
{
	u8 *sense = iue->iu->srp.rsp.sense_and_response_data;
	u64 tag = iue->iu->srp.generic.tag;
	union VIOSRP_IU *iu = iue->iu;

	if (status != SENSE_SUCCESS) {
		atomic_inc(&iue->adapter->errors);
	}

	/* If the linked bit is on and status is good */
	if ((iue->req.linked) && (status == SENSE_SUCCESS)) {
		status = SENSE_INTERMEDIATE;
	}

	memset(iu, 0x00, sizeof(struct SRP_RSP));
	iu->srp.rsp.type = SRP_RSP_TYPE;
	iu->srp.rsp.request_limit_delta = 1;
	iu->srp.rsp.tag = tag;

	iu->srp.rsp.diunder = iue->req.diunder;
	iu->srp.rsp.diover = iue->req.diover;
	iu->srp.rsp.dounder = iue->req.dounder;
	iu->srp.rsp.doover = iue->req.doover;

	iu->srp.rsp.data_in_residual_count = iue->req.data_in_residual_count;
	iu->srp.rsp.data_out_residual_count = iue->req.data_out_residual_count;

	iu->srp.rsp.rspvalid = 0;

	iu->srp.rsp.response_data_list_length = 0;

	if (status) {
		iu->srp.rsp.status = SAM_STAT_CHECK_CONDITION;
		iu->srp.rsp.snsvalid = 1;
		iu->srp.rsp.sense_data_list_length = 18;	/* TODO be smarter about this */

		/* Valid bit and 'current errors' */
		sense[0] = (0x1 << 7 | 0x70);

		/* Sense key */
		sense[2] = ibmvscsis_sense_data[status][0];

		/* Additional sense length */
		sense[7] = 0xa;	/* 10 bytes */

		/* Additional sense code */
		sense[12] = ibmvscsis_sense_data[status][1];

		/* Additional sense code qualifier */
		sense[13] = ibmvscsis_sense_data[status][2];
	} else {
		iu->srp.rsp.status = 0;
	}

	send_srp(iue, sizeof(iu->srp.rsp));

	return 0;
}

static void process_device_reset(struct iu_entry *iue)
{
	struct iu_entry *tmp_iue;
	unsigned long flags;
	union VIOSRP_IU *iu = iue->iu;

	info("device reset for lun %16.16lx\n", iu->srp.tsk_mgmt.lun);

	spin_lock_irqsave(&iue->adapter->lock, flags);

	list_for_each_entry(tmp_iue, &iue->adapter->inflight, next) {
		if (iu->srp.tsk_mgmt.lun == tmp_iue->iu->srp.cmd.lun) {
			{
				tmp_iue->aborted = 1;
			}
		}

	}

	spin_unlock_irqrestore(&iue->adapter->lock, flags);
}

static void process_abort(struct iu_entry *iue)
{
	struct iu_entry *tmp_iue;
	unsigned long flags;
	union VIOSRP_IU *iu = iue->iu;

	info("aborting task with tag %16.16lx, lun %16.16lx\n",
	     iu->srp.tsk_mgmt.managed_task_tag, iu->srp.tsk_mgmt.lun);

	spin_lock_irqsave(&iue->adapter->lock, flags);

	list_for_each_entry(tmp_iue, &iue->adapter->inflight, next) {
		if (tmp_iue->iu->srp.cmd.tag ==
		    iu->srp.tsk_mgmt.managed_task_tag) {
			{
				tmp_iue->aborted = 1;
				info("abort successful\n");
				spin_unlock_irqrestore(&iue->adapter->lock,
						       flags);
				send_rsp(iue, SENSE_SUCCESS);
				return;
			}
		}
	}
	info("unable to abort cmd\n");

	spin_unlock_irqrestore(&iue->adapter->lock, flags);
}

static void process_tsk_mgmt(struct iu_entry *iue)
{
	union VIOSRP_IU *iu = iue->iu;

	if (iu->srp.tsk_mgmt.task_mgmt_flags != 0x01) {
		process_abort(iue);
	} else if (iu->srp.tsk_mgmt.task_mgmt_flags != 0x08) {
		process_device_reset(iue);
	} else {
		send_rsp(iue, SENSE_INVALID_CMD);
	}
}

static void process_iu(struct VIOSRP_CRQ *crq, struct server_adapter *adapter)
{
	struct iu_entry *iue = get_iu(adapter);
	union VIOSRP_IU *iu;
	int queued = 0;
	long rc;

	if (iue == NULL) {
		/* TODO Yikes! */
		warn("Error getting IU from pool, other side exceeded limit\n");
		return;
	}

	iue->req.remote_token = crq->IU_data_ptr;

	rc = h_copy_rdma(crq->IU_length,
			 iue->adapter->riobn,
			 iue->req.remote_token, adapter->liobn, iue->iu_token);

	iu = iue->iu;

	if (rc) {
		err("Got rc %ld from h_copy_rdma\n", rc);
	}

	if (crq->format == VIOSRP_MAD_FORMAT) {
		switch (iu->mad.empty_iu.common.type) {
		case VIOSRP_EMPTY_IU_TYPE:
			warn("Unsupported EMPTY MAD IU\n");
			break;
		case VIOSRP_ERROR_LOG_TYPE:
			warn("Unsupported ERROR LOG MAD IU\n");
			iu->mad.error_log.common.status = 1;
			send_srp(iue, sizeof(iu->mad.error_log));
			break;
		case VIOSRP_ADAPTER_INFO_TYPE:
			iu->mad.adapter_info.common.status =
			    send_adapter_info(iue,
					      iu->mad.adapter_info.buffer,
					      iu->mad.adapter_info.common.
					      length);

			send_srp(iue, sizeof(iu->mad.adapter_info));
			break;
		case VIOSRP_HOST_CONFIG_TYPE:
			iu->mad.host_config.common.status = 1;
			send_srp(iue, sizeof(iu->mad.host_config));
			break;
		default:
			warn("Unsupported MAD type %d\n", iu->srp.generic.type);
		}
	} else {
		switch (iu->srp.generic.type) {
		case SRP_LOGIN_REQ_TYPE:
			dbg("SRP LOGIN\n");
			process_login(iue);
			break;
		case SRP_LOGIN_RSP_TYPE:
			warn("Unsupported LOGIN_RSP SRP IU\n");
			break;
		case SRP_I_LOGOUT_TYPE:
			warn("Unsupported I_LOGOUT SRP IU\n");
			break;
		case SRP_T_LOGOUT_TYPE:
			warn("Unsupported T_LOGOUT SRP IU\n");
			break;
		case SRP_TSK_MGMT_TYPE:
			process_tsk_mgmt(iue);
			break;
		case SRP_CMD_TYPE:
			process_cmd(iue);
			queued = 1;
			break;
		case SRP_RSP_TYPE:
			warn("Unsupported RSP SRP IU\n");
			break;
		case SRP_CRED_REQ_TYPE:
			warn("Unsupported CRED_REQ SRP IU\n");
			break;
		case SRP_CRED_RSP_TYPE:
			warn("Unsupported CRED_RSP SRP IU\n");
			break;
		case SRP_AER_REQ_TYPE:
			warn("Unsupported AER_REQ SRP IU\n");
			break;
		case SRP_AER_RSP_TYPE:
			warn("Unsupported AER_RSP SRP IU\n");
			break;
		default:
			warn("Unsupported SRP type %d\n", iu->srp.generic.type);
		}
	}

	/* 
	 * If no one has queued the IU for further work, free it 
	 * Note that this is kind of an ugly design based on setting
	 * this variable up above in cases where the routine we call
	 * is responsible for freeing the IU
	 */
	if (!queued)
		free_iu(iue);
}

/* ==============================================================
 * CRQ Processing Routines
 * ==============================================================
 */

/*
 * Handle a CRQ event
 */
static void handle_crq(struct VIOSRP_CRQ *crq, struct server_adapter *adapter)
{
	switch (crq->valid) {
	case 0xC0:		/* initialization */
		switch (crq->format) {
		case 0x01:
			info("Partner just initialized\n");
			plpar_hcall_norets(H_SEND_CRQ,
					   adapter->dma_dev->unit_address,
					   0xC002000000000000, 0);
			break;
		case 0x02:
			info("ibmvscsis: partner initialization complete\n");
			break;
		default:
			err("Unknwn CRQ format %d\n", crq->format);
		}
		return;
	case 0xFF:		/* transport event */
		info("ibmvscsis: partner closed\n");
		return;
	case 0x80:		/* real payload */
		{
			switch (crq->format) {
			case VIOSRP_SRP_FORMAT:
			case VIOSRP_MAD_FORMAT:
				process_iu(crq, adapter);
				break;
			case VIOSRP_OS400_FORMAT:
				warn("Unsupported OS400 format CRQ\n");
				break;

			case VIOSRP_AIX_FORMAT:
				warn("Unsupported AIX format CRQ\n");
				break;

			case VIOSRP_LINUX_FORMAT:
				warn("Unsupported LINUX format CRQ\n");
				break;

			case VIOSRP_INLINE_FORMAT:
				warn("Unsupported _INLINE_ format CRQ\n");
				break;

			default:
				err("Unsupported CRQ format %d\n", crq->format);
			}
		}
		break;
	default:
		err("ibmvscsis: got an invalid message type 0x%02x!?\n",
		    crq->valid);
		return;
	}

}

/*
 * Task to handle CRQs and completions
 */
static void crq_task(unsigned long data)
{
	struct server_adapter *adapter = (struct server_adapter *)data;
	struct VIOSRP_CRQ *crq;
	long rc;
	int done = 0;

	if (atomic_inc_return(&adapter->crq_task_count) > 1) {
		err("In crq_task twice!!!\n");
	}

	while (!done) {

		/* Loop through and process CRQs */
		while ((crq = crq_queue_next_crq(&adapter->queue)) != NULL) {
			atomic_inc(&adapter->crq_processed);
			handle_crq(crq, adapter);
			crq->valid = 0x00;
		}

		rc = h_vio_signal(adapter->dma_dev->unit_address, 1);
		if (rc != 0) {
			err("Error %ld enabling interrupts!!!\n", rc);
		}
		if ((crq = crq_queue_next_crq(&adapter->queue)) != NULL) {
			rc = h_vio_signal(adapter->dma_dev->unit_address, 0);
			if (rc != 0) {
				err("Error %ld enabling interrupts!!!\n", rc);
			}
			handle_crq(crq, adapter);
			crq->valid = 0x00;
		} else {
			done = 1;
		}
	}
	atomic_dec(&adapter->crq_task_count);
}

/*
 * Handle the interrupt that occurs when something is placed on our CRQ
 */
static irqreturn_t handle_interrupt(int irq, void *dev_instance,
				    struct pt_regs *regs)
{
	struct server_adapter *adapter = (struct server_adapter *)dev_instance;
	long rc;

	rc = h_vio_signal(adapter->dma_dev->unit_address, 0);
	if (rc != 0) {
		err(" Error %ld disabling interrupts!!!\n", rc);
	}

	atomic_inc(&adapter->interrupts);

	tasklet_schedule(&adapter->crq_tasklet);

	return IRQ_HANDLED;
}

/* 
 * Initialize our CRQ
 * return zero on success, non-zero on failure 
 */
static int initialize_crq_queue(struct crq_queue *queue,
				struct server_adapter *adapter)
{
	int rc;

	queue->msgs = (struct VIOSRP_CRQ *)get_zeroed_page(GFP_KERNEL);
	if (!queue->msgs)
		goto malloc_failed;
	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	queue->msg_token = dma_map_single(adapter->dev, queue->msgs,
					  queue->size * sizeof(*queue->msgs),
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(queue->msg_token))
		goto map_failed;

	rc = plpar_hcall_norets(H_REG_CRQ, adapter->dma_dev->unit_address,
				queue->msg_token, PAGE_SIZE);

	/* If we opened successfully, send an init message */
	if (rc == 0) {
		plpar_hcall_norets(H_SEND_CRQ, adapter->dma_dev->unit_address,
				   0xC001000000000000, 0);
	} else if (rc == 2) {
		/* Other end is still closed.  This is normal */
		info("connection registered, other end closed\n");
	} else {
		err("couldn't register crq--rc 0x%x\n", rc);
		goto reg_crq_failed;
	}

	if (request_irq
	    (adapter->dma_dev->irq, &handle_interrupt, SA_INTERRUPT,
	     "ibmvscsis", adapter) != 0)
		goto req_irq_failed;

	queue->cur = 0;
	queue->lock = SPIN_LOCK_UNLOCKED;

	return 0;

      req_irq_failed:
	plpar_hcall_norets(H_FREE_CRQ, adapter->dma_dev->unit_address);
      reg_crq_failed:
	dma_unmap_single(adapter->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
      map_failed:
	free_page((unsigned long)queue->msgs);
      malloc_failed:
	return -1;
}

/*
 * Release the CRQ
 */
static void release_crq_queue(struct crq_queue *queue,
			      struct server_adapter *adapter)
{
	info("releasing crq\n");
	free_irq(adapter->dma_dev->irq, adapter);
	plpar_hcall_norets(H_FREE_CRQ, adapter->dma_dev->unit_address);
	dma_unmap_single(adapter->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
	free_page((unsigned long)queue->msgs);
}

/* ==============================================================
 * Module Management
 * ==============================================================
 */
/*
 * Add a block device as a SCSI LUN
 */
static void add_block_device(int majo, int mino, int bus, int target,
			     struct server_adapter *adapter, int ro)
{
	struct vdev *vd;
	struct vbus *newbus = NULL;
	mode_t mode;
	unsigned long flags;
	dev_t kd = MKDEV(majo, mino);

	if (bus >= BUS_PER_ADAPTER) {
		err("Invalid bus %u specified\n", bus);
		return;
	}

	if (target >= TARGETS_PER_BUS) {
		err("Invalid target %u specified\n", bus);
		return;
	}

	vd = (struct vdev *)kmalloc(sizeof(struct vdev), GFP_KERNEL);
	if (vd == NULL) {
		err("Unable to allocate memory for vdev structure");
		return;
	}

	memset(vd, 0x00, sizeof(*vd));
	vd->type = 'B';
	vd->lun = make_lun(bus, target, 0);
	vd->b.dev = kd;
	vd->b.bdev = bdget(kd);
	vd->b.blksize = 512;
	vd->b.ro = ro;

	if (ro) {
		mode = FMODE_READ;
	} else {
		mode = FMODE_READ | FMODE_WRITE;
	}

	if (blkdev_get(vd->b.bdev, mode, 0) != 0) {
		err("Error opening block device\n");
		kfree(vd);
		return;
	}

	if (adapter->vbus[bus] == NULL) {
		newbus =
		    (struct vbus *)kmalloc(sizeof(struct vbus), GFP_KERNEL);
		memset(newbus, 0x00, sizeof(*newbus));
	}

	spin_lock_irqsave(&adapter->lock, flags);
	if ((newbus) && (adapter->vbus[bus] == NULL)) {
		adapter->vbus[bus] = newbus;
		newbus = NULL;
	}

	if (adapter->vbus[bus]->vdev[target] != NULL) {
		spin_unlock_irqrestore(&adapter->lock, flags);
		err("Error: Duplicate vdev as lun 0x%lx\n", vd->lun);
		kfree(vd);
		return;
	}

	adapter->vbus[bus]->vdev[target] = vd;
	adapter->nvdevs++;

	spin_unlock_irqrestore(&adapter->lock, flags);

	if (newbus)
		kfree(newbus);

	info("Adding block device %d:%d as %sLUN 0x%lx\n",
	     majo, mino, ro ? "read only " : "", vd->lun);
	return;
}

static void remove_block_device(int bus, int target,
				struct server_adapter *adapter)
{
	struct vdev *vd;
	unsigned long flags;

	spin_lock_irqsave(&adapter->lock, flags);

	if ((!adapter->vbus[bus]) || (!adapter->vbus[bus]->vdev[target])) {
		spin_unlock_irqrestore(&adapter->lock, flags);
		err("Error removing non-existant device at bus %d, target %d\n",
		    bus, target);
		return;
	}

	vd = adapter->vbus[bus]->vdev[target];

	if (vd->disabled) {
		spin_unlock_irqrestore(&adapter->lock, flags);
		err("Device at bus %d, target %d removed twice\n", bus, target);
		return;
	}

	adapter->nvdevs--;

	vd->disabled = 1;

	spin_unlock_irqrestore(&adapter->lock, flags);

	/* Wait while any users of this device finish.  Note there should
	 * be no new users, since we have marked this disabled
	 *
	 * We just poll here, since we are blocking a proc_write
	 */
	while (atomic_read(&vd->refcount)) {
		schedule_timeout(HZ / 4);	/* 1/4 second */
	}

	spin_lock_irqsave(&adapter->lock, flags);
	adapter->vbus[bus]->vdev[target] = NULL;
	spin_unlock_irqrestore(&adapter->lock, flags);

	if (blkdev_put(vd->b.bdev)) {
		err("Error closing block device!\n");
	}
	kfree(vd);

	info("Removed block device at %d:%d\n", bus, target);
}

/*
 * Handle read from our proc system file.  There is one of these
 * files per adapter
 */
static int ibmvscsis_proc_read(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	struct server_adapter *adapter = (struct server_adapter *)data;
	int len = 0;
	int bus;
	int target;
	struct vdev *vd;
	unsigned long flags;

	len += sprintf(page + len, "IBM VSCSI Server: %s\n", adapter->name);
	len += sprintf(page + len, "interrupts: %10d\t\tread ops:   %10d\n",
		       atomic_read(&adapter->interrupts),
		       atomic_read(&adapter->read_processed));
	len += sprintf(page + len, "crq msgs:   %10d\t\twrite ops:  %10d\n",
		       atomic_read(&adapter->crq_processed),
		       atomic_read(&adapter->write_processed));
	len += sprintf(page + len, "iu alloc:   %10d\t\tbio alloc:  %10d\n",
		       atomic_read(&adapter->iu_count),
		       atomic_read(&adapter->bio_count));

	len += sprintf(page + len, "buf alloc:  %10d\t\terrors:     %10d\n",
		       atomic_read(&adapter->buffers_allocated),
		       atomic_read(&adapter->errors));

	spin_lock_irqsave(&adapter->lock, flags);

	/* loop through the bus */
	for (bus = 0; bus < BUS_PER_ADAPTER; bus++) {
		/* If this bus exists */
		if (adapter->vbus[bus]) {
			/* loop through the targets */
			for (target = 0; target < TARGETS_PER_BUS; target++) {
				/* If the target exists */
				if (adapter->vbus[bus]->vdev[target]) {
					vd = adapter->vbus[bus]->vdev[target];
					if (vd->type == 'B') {
						len +=
						    sprintf(page + len,
							    "Block Device Major %d, Minor %d, Bus %d, Target %d LUN %d\n",
							    MAJOR(vd->b.dev),
							    MINOR(vd->b.dev),
							    GETBUS(vd->lun),
							    GETTARGET(vd->lun),
							    GETLUN(vd->lun));
					}
				}
			}
		}
	}

	spin_unlock_irqrestore(&adapter->lock, flags);

	*eof = 1;
	return len;
}

/*
 * Handle proc file system write.  One per adapter, currently used just
 * to add virtual devices to our adapters.
 */
static int ibmvscsis_proc_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
	int offset = 0;
	int bytes;
	char token[10];
	char type[4];
	int majo = -1;
	int mino = -1;
	unsigned int bus = -1;
	unsigned int target = -1;
	int ro = 0;

	struct server_adapter *adapter = (struct server_adapter *)data;

	if (sscanf(buffer + offset, "%9s%n", token, &bytes) != 1) {
		err("Error on read of proc file\n");
		return count;
	}
	offset += bytes;

	if (strcmp(token, "add") == 0) {

		if (sscanf(buffer + offset, "%3s%n", type, &bytes) != 1) {
			err("Error on read of proc file\n");
			return count;
		}
		offset += bytes;

		sscanf(buffer + offset, "%i%n", &majo, &bytes);
		offset += bytes;

		sscanf(buffer + offset, "%i%n", &mino, &bytes);
		offset += bytes;

		sscanf(buffer + offset, "%i%n", &bus, &bytes);
		offset += bytes;

		sscanf(buffer + offset, "%i%n", &target, &bytes);
		offset += bytes;

		if (strcmp(type, "b") == 0) {
			ro = 0;
		} else if (strcmp(type, "br") == 0) {
			ro = 1;
		} else {
			err("Invalid type %s on add request\n", type);
			return count;
		}

		if ((majo == -1) || (mino == -1)) {
			err("Ignoring command %s device %d::%ds type %s\n",
			    token, majo, mino, type);
			return count;
		}
		add_block_device(majo, mino, bus, target, adapter, ro);
	} else if (strcmp(token, "remove") == 0) {
		if (sscanf(buffer + offset, "%3s%n", type, &bytes) != 1) {
			err("Error on read of proc file\n");
			return count;
		}
		offset += bytes;

		sscanf(buffer + offset, "%i%n", &bus, &bytes);
		offset += bytes;

		sscanf(buffer + offset, "%i%n", &target, &bytes);
		offset += bytes;

		if ((strcmp(type, "b") != 0) && (strcmp(type, "br") != 0)) {
			err("Ignoring remove command for invalid type %s\n",
			    type);
			return count;
		}

		if ((bus == -1) || (target == -1)) {
			err("Ignoring remove command bus %d target%ds\n",
			    bus, target);
			return count;
		}

		remove_block_device(bus, target, adapter);
	} else if (strcmp(token, "debug") == 0) {
		ibmvscsis_debug = 1;
		dbg("debugging on\n");
	} else {
		err("Ignoring command %s\n", token);
	}

	return count;
}

static void ibmvscsis_proc_register_driver(void)
{
#ifdef CONFIG_PROC_FS
	ibmvscsis_proc_dir =
	    create_proc_entry(IBMVSCSIS_PROC_DIR, S_IFDIR, proc_root_driver);
#endif
}

static void ibmvscsis_proc_unregister_driver(void)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry(IBMVSCSIS_PROC_DIR, proc_root_driver);
#endif
}

static void ibmvscsis_proc_register_adapter(struct server_adapter *adapter)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *entry;
	entry = create_proc_entry(adapter->name, S_IFREG, ibmvscsis_proc_dir);
	entry->data = (void *)adapter;
	entry->read_proc = ibmvscsis_proc_read;
	entry->write_proc = ibmvscsis_proc_write;
#endif
}

static void ibmvscsis_proc_unregister_adapter(struct server_adapter *adapter)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry(adapter->name, ibmvscsis_proc_dir);
#endif
}

static int ibmvscsis_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	struct server_adapter *adapter;
	int rc;
	unsigned int *dma_window;
	unsigned int dma_window_property_size;

	info("entering probe for UA 0x%x\n", dev->unit_address);

	adapter = kmalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		err("couldn't kmalloc adapter structure\n");
		return -1;
	}
	memset(adapter, 0x00, sizeof(*adapter));
	adapter->dma_dev = dev;
	adapter->dev = &dev->dev;
	dev->driver_data = adapter;
	sprintf(adapter->name, "%x", dev->unit_address);
	adapter->lock = SPIN_LOCK_UNLOCKED;

	dma_window =
	    (unsigned int *)vio_get_attribute(dev, "ibm,my-dma-window",
					      &dma_window_property_size);
	if (!dma_window) {
		warn("Couldn't find ibm,my-dma-window property\n");
	}

	adapter->liobn = dma_window[0];
	/* RPA docs say that #address-cells is always 1 for virtual
	   devices, but some older boxes' OF returns 2.  This should
	   be removed by GA, unless there is legacy OFs that still
	   have 2 or 3 for #address-cells */
	/*adapter->riobn = dma_window[2+vio_num_address_cells]; */

	/* This is just an ugly kludge. Remove as soon as the OF for all
	   machines actually follow the spec and encodes the offset field
	   as phys-encode (that is, #address-cells wide) */
	if (dma_window_property_size == 24) {
		adapter->riobn = dma_window[3];
	} else if (dma_window_property_size == 40) {
		adapter->riobn = dma_window[5];
	} else {
		warn("ibmvscsis: Invalid size of ibm,my-dma-window=%i\n",
		     dma_window_property_size);
	}

	tasklet_init(&adapter->crq_tasklet, crq_task, (unsigned long)adapter);

	tasklet_init(&adapter->endio_tasklet,
		     endio_task, (unsigned long)adapter);

	INIT_LIST_HEAD(&adapter->inflight);

	/* Initialize the buffer cache */
	init_data_buffer(adapter);

	/* Arbitrarily support 16 IUs right now */
	rc = initialize_iu_pool(adapter, 16);
	if (rc) {
		kfree(adapter);
		return rc;
	}

	rc = initialize_crq_queue(&adapter->queue, adapter);
	if (rc != 0) {
		kfree(adapter);
		return rc;
	}

	rc = h_vio_signal(adapter->dma_dev->unit_address, 1);
	if (rc != 0) {
		err("Error %d enabling interrupts!!!\n", rc);
	}

	ibmvscsis_proc_register_adapter(adapter);

	return 0;
}

static int ibmvscsis_remove(struct vio_dev *dev)
{
	int bus;
	int target;

	struct server_adapter *adapter =
	    (struct server_adapter *)dev->driver_data;

	info("entering remove for UA 0x%x\n", dev->unit_address);

	ibmvscsis_proc_unregister_adapter(adapter);

	/* 
	 * No one should be adding any devices at this point because we blew 
	 * away the proc file system entry 
	 *
	 * Loop through the bus
	 */
	for (bus = 0; bus < BUS_PER_ADAPTER; bus++) {
		/* If this bus exists */
		if (adapter->vbus[bus]) {
			/* loop through the targets */
			for (target = 0; target < TARGETS_PER_BUS; target++) {
				/* If the target exists */
				if (adapter->vbus[bus]->vdev[target]) {
					remove_block_device(bus, target,
							    adapter);
				}
			}
		}
	}

	release_crq_queue(&adapter->queue, adapter);

	release_iu_pool(adapter);

	release_data_buffer(adapter);

	kfree(adapter);

	return 0;
}

static struct vio_device_id ibmvscsis_device_table[] __devinitdata = {
	{"v-scsi-host", "IBM,v-scsi-host"},
	{0,}
};

MODULE_DEVICE_TABLE(vio, ibmvscsis_device_table);

static struct vio_driver ibmvscsis_driver = {
	.name = "ibmvscss",
	.id_table = ibmvscsis_device_table,
	.probe = ibmvscsis_probe,
	.remove = ibmvscsis_remove,
};

static int mod_init(void)
{
	int rc;

	info("ibmvscsis initialized\n");

	ibmvscsis_proc_register_driver();

	rc = vio_register_driver(&ibmvscsis_driver);

	if (rc) {
		warn("rc %d from vio_register_driver\n", rc);
	}

	return rc;
}

static void mod_exit(void)
{
	info("terminated\n");

	vio_unregister_driver(&ibmvscsis_driver);

	ibmvscsis_proc_unregister_driver();
}

module_init(mod_init);
module_exit(mod_exit);

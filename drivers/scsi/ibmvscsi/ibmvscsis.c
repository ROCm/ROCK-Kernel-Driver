/************************************************************************

 IBM eServer i/pSeries Virtual SCSI Target Driver
 Copyright (C) 2003-2005 Dave Boutcher (boutcher@us.ibm.com) IBM Corp.
			 Santiago Leon (santil@us.ibm.com) IBM Corp.
			 Linda Xie (lxie@us.ibm.com) IBM Corp.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
     USA

  **********************************************************************
  This driver is a SCSI target that interoperates according to the PAPR
  (POWER Architecture Platform Requirements) document.  Currently it is
  specific to POWER logical partitioning, however in the future it would
  be nice to extend this to other virtualized environments.

  The architecture defines virtual adapters, whose configuration is
  reported in the Open Firmware device tree.  There area number of
  power hypervisor calls (such as h_reg_crq, to register the inter-OS
  queue) that support the virtual adapters.

  Messages are sent between partitions on a "Command/Response Queue"
  (CRQ), which is just a buffer of 16 byte entries in the receiver's
  Senders cannot access the buffer directly, but send messages by
  making a hypervisor call and passing in the 16 bytes.  The hypervisor
  puts the message in the next 16 byte space in round-robbin fashion,
  turns on the high order bit of the message (the valid bit), and
  generates an interrupt to the receiver (if interrupts are turned on.)
  The receiver just turns off the valid bit when they have copied out
  the message.

  The VSCSI client builds a SCSI Remote Protocol (SRP) Information Unit
  (IU) (as defined in the T10 standard available at www.t10.org), gets
  a DMA address for the message, and sends it to the target as the
  payload of a CRQ message.  The target DMAs the SRP IU and processes it,
  including doing any additional data transfers.  When it is done, it
  DMAs the SRP response back to the same address as the request came from
  and sends a CRQ message back to inform the client that the request has
  completed.

  This target interoperates not only with the Linux client (ibmvscsi.c)
  but also with AIX and OS/400 clients.  Thus, while the implementation
  can be changed, the underlying behaviour (protocol) is fixed.

  Configuration of the target is done via sysfs.  The target driver
  maps either block devices (e.g. IDE CD drive, loopback file, etc) to
  SCSI LUNs, in which case it emulates the SCSI protocol and issues
  kernel block device calls, or maps real SCSI devices, in which case
  the SCSI commands are just passed on to the real SCSI device.
************************************************************************/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/device.h>
#include <linux/cdrom.h>
#include <linux/delay.h>

#include <asm/hvcall.h>
#include <asm/vio.h>
#include <asm/iommu.h>
#include <asm/prom.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_request.h>

#include "viosrp.h"

#define IBMVSCSIS_VERSION "1.0.0"

#define DEFAULT_TIMEOUT (30*HZ)
#define TARGET_MAX_NAME_LEN 128
#define INITIAL_SRP_LIMIT 16
#define TARGETS_PER_BUS (64)
#define BUS_PER_ADAPTER (8)
#define DMA_BUFFER_CACHE_SIZE (16)
#define DMA_BUFFER_INIT_COUNT (4)
#define DMA_BUFFER_INIT_LEN (PAGE_SIZE*16)
#define MODE_SENSE_BUFFER_SIZE (512)
#define REFCOUNT_TIMEOUT_MS (250)	/* 1/4 second */
#define DEFAULT_MAX_SECTORS (512)       /* 256 kb */
#define MAX_H_COPY_RDMA (128*1024)

/*
 * The following are lifted from usb.h
 */
static int ibmvscsis_debug = 0;
#define dbg(format, arg...) \
	do {\
		if (ibmvscsis_debug) printk(KERN_WARNING __FILE__ ": " \
					    format , ## arg);\
	} while(0)
#define err(format, arg...) printk(KERN_ERR "ibmvscsis: " format , ## arg)
#define info(format, arg...) printk(KERN_INFO "ibmvscsis: " format  , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "ibmvscsis: " format , ## arg)

/*
 * Given an 8 byte LUN, return the first level bus/target/lun.
 * Today this doesn't support multi-level LUNs
 */
#define GETBUS(x) ((int)((((u64)(x)) >> 53) & 0x0007))
#define GETTARGET(x) ((int)((((u64)(x)) >> 56) & 0x003f))
#define GETLUN(x) ((int)((((u64)(x)) >> 48) & 0x001f))

/*
 * sysfs attributes macro
 */
#define ATTR(_type, _name, _mode)      \
	struct attribute vscsi_##_type##_##_name##_attr = {		  \
	.name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE \
	};

/*
 * Hypervisor calls.
 */
#define h_send_crq(ua, l, h) \
			plpar_hcall_norets(H_SEND_CRQ, ua, l, h)
#define h_reg_crq(ua, tok, sz)\
			plpar_hcall_norets(H_REG_CRQ, ua, tok, sz);
#define h_free_crq(ua) \
			plpar_hcall_norets(H_FREE_CRQ, ua);

MODULE_DESCRIPTION("IBM Virtual SCSI Target");
MODULE_AUTHOR("Dave Boutcher");
MODULE_LICENSE("GPL");
MODULE_VERSION(IBMVSCSIS_VERSION);

/*
 * These are fixed for the system and come from the Open Firmware device tree.
 * We just store them here to save getting them every time.
 */
static char system_id[64] = "";
static char partition_name[97] = "UNKNOWN";
static unsigned int partition_number = -1;

/*
 * SCSI defined structure for inquiry data
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
	char unique[158];
};

/*
 * an RPA command/response transport queue.  This is our structure
 * that points to the actual queue (not architected by firmware)
 */
struct crq_queue {
	struct viosrp_crq *msgs;
	int size, cur;
	dma_addr_t msg_token;
	spinlock_t lock;
};

enum iue_flags {
	V_IN_USE	= 0,
	V_DIOVER	= 1,
	V_WRITE		= 2,
	V_LINKED	= 3,
	V_ABORTED	= 4,
	V_FLYING	= 5,
	V_BARRIER	= 6,
	V_PARSED	= 7,
	V_DONE		= 8,
};

/*
 * This structure tracks our fundamental unit of work.  Whenever
 * an SRP Information Unit (IU) arrives, we track all the good stuff
 * here
 */
struct iu_entry {
	union viosrp_iu *iu;
	struct server_adapter *adapter;
	struct list_head next;
	dma_addr_t iu_token;
	struct {
		dma_addr_t remote_token;
		char *data_buffer;
		dma_addr_t data_token;
		long data_len;
		struct vdev *vd;
		unsigned long flags;
		char *sense;
		int data_out_residual_count;
		int data_in_residual_count;
		int ioerr;
		int timeout;
		struct scsi_request* sreq;
		struct iu_entry *child[2];
		struct iu_entry *parent;
		unsigned char child_status;
		int rw;
		long lba;
		long len;
	} req;
};

/*
 * a pool of ius for use
 */
struct iu_pool {
	spinlock_t lock;
	struct list_head iu_entries;
	struct iu_entry *list;
	union viosrp_iu *iu_storage;
	dma_addr_t iu_token;
	u32 size;
};

/*
 * Represents a single device that someone told us about
 * that we treat as a LUN
 */
struct vdev {
	struct list_head list;
	char direct_scsi;
	atomic_t refcount;
	int disabled;
	u64 lun;
	struct kobject kobj;
	char device_name[TARGET_MAX_NAME_LEN];
	struct {
		struct block_device *bdev;
		long blocksize;
		long sectsize;
		long lastlba;
		unsigned char scsi_type;
		int ro;
		int removable;
		int changed;
	} b;
	struct {
		struct scsi_device *sdev;
	} s;
};

/*
 * Represents a bus.  target #'s in SCSI are 6 bits long,
 * so you can have 64 targets per bus
 */
struct vbus {
	struct vdev *vdev[TARGETS_PER_BUS];
	atomic_t num_targets;
	struct kobject kobj;
	int bus_num;
};

/*
 * Cached buffer.  This is a data buffer that we have issued
 * dma_map_foo on.  Rather than do this every time we need a
 * data buffer, keep a cache of mapped buffers around.
 */
struct dma_buffer {
	dma_addr_t token;
	char *addr;
	size_t len;
};

/* all driver data associated with a host adapter */
struct server_adapter {
	struct device *dev;
	struct vio_dev *dma_dev;
	struct crq_queue queue;
	struct work_struct crq_task;
	struct tasklet_struct endio_tasklet;
	struct iu_pool pool;
	spinlock_t lock;
	struct bio *bio_done;
	struct bio *bio_donetail;
	struct list_head cmd_queue;
	struct vbus *vbus[BUS_PER_ADAPTER];
	int nvdevs;
	int next_rsp_delta;
	unsigned long liobn;
	unsigned long riobn;

	atomic_t num_buses;
	int max_sectors;
	struct kobject stats_kobj;
	DECLARE_BITMAP(dma_buffer_use, DMA_BUFFER_CACHE_SIZE);
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
 * We use the following struct, list, and lock to keep track of the scsi
 * devices and their mapping to targets in the vscsis adapters.
 */
struct scsi_dev_node {
	struct list_head node;
	struct scsi_device *sdev;
	struct vdev *vdev;
};

/* The state of a request */
enum ibmvscsis_iue_state {
	FREE_IU,
	INFLIGHT,
	RETRY,
	RETRY_SPLIT_BUF,
};

static LIST_HEAD(scsi_dev_list);
static spinlock_t sdev_list_lock = SPIN_LOCK_UNLOCKED;

/* ==============================================================
 * Utility Routines
 * ==============================================================
 */
/*
 * return an 8 byte lun given a bus, target, lun.
 * Today this only supports single level luns.
 */
u64 make_lun(unsigned int bus, unsigned int target, unsigned int lun)
{
	u16 result = (0x8000 |
		      ((target & 0x003f) << 8) |
		      ((bus & 0x0007) << 5) | (lun & 0x001f));
	return ((u64) result) << 48;
}

/*
 * Get the control byte from a SCSI CDB
 */
static u8 getcontrolbyte(u8 * cdb)
{
	return cdb[COMMAND_SIZE(cdb[0]) - 1];
}

/*
 * Get the "link" bit from a SCSI CDB
 */
static u8 getlink(struct iu_entry *iue)
{
	return (getcontrolbyte(iue->iu->srp.cmd.cdb) & 0x01);
}

static int data_out_desc_size(struct srp_cmd *cmd)
{
	switch (cmd->data_out_format) {
	case SRP_NO_BUFFER:
		return 0;
	case SRP_DIRECT_BUFFER:
		return sizeof(struct memory_descriptor);
	case SRP_INDIRECT_BUFFER:
		return sizeof(struct indirect_descriptor) + 
		    ((cmd->data_out_count - 
			1) * sizeof(struct memory_descriptor));
	default:
		err("client error. Invalid data_out_format %d\n",
		    cmd->data_out_format);
		return 0;
	}
}

/*
 * Given an SRP, figure out the "data in" or "data out" length
 */
static int vscsis_data_length(struct srp_cmd *cmd, int out)
{
	struct memory_descriptor *md;
	struct indirect_descriptor *id;
	int offset = cmd->additional_cdb_len * 4;
	int switch_value;

	if (out)
		switch_value = cmd->data_out_format;
	else {
		switch_value = cmd->data_in_format;
		offset += data_out_desc_size(cmd);
	}

	switch (switch_value) {
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
		err("invalid data format\n");
		return 0;
	}
}

/*
 * Helper function to create a direct buffer descriptor from an indirect
 * buffer descriptor of length 1
 */
static void make_direct_buffer(struct srp_cmd *cmd)
{
	struct indirect_descriptor *id = (struct indirect_descriptor *)
						(cmd->additional_data);
	struct memory_descriptor *md = (struct memory_descriptor *)
						(cmd->additional_data);
	unsigned int length = id->list[0].length;
	unsigned int address = id->list[0].virtual_address;

	if (cmd->data_out_format == SRP_INDIRECT_BUFFER)
		cmd->data_out_format = SRP_DIRECT_BUFFER;
	if (cmd->data_in_format == SRP_INDIRECT_BUFFER)
		cmd->data_in_format = SRP_DIRECT_BUFFER;

	md->length = length;
	md->virtual_address = address;
	cmd->data_in_count = cmd->data_out_count = 0;
}

/*
 * Find the vdev structure from the LUN field in an SRP IUE
 * Note that this routine bumps a refcount field in the vdev.
 * Normally this is done when free_iu is called.
 */
static struct vdev *find_vscsis_vdev(struct iu_entry *iue)
{
	u16 *lun = (u16 *) & iue->iu->srp.cmd.lun;
	u32 bus = (lun[0] & 0x00E0) >> 5;
	u32 target = (lun[0] & 0x3F00) >> 8;
	u32 slun = (lun[0] & 0x001F);
	struct vdev *vd = NULL;
	unsigned long flags;

	/* If asking for a lun other than 0, return nope */
	if (slun)
		return NULL;

	/* Only from SRP CMD */
	if (iue->iu->srp.generic.type != SRP_CMD_TYPE)
		return NULL;

	/* if not a recognized LUN format, return NULL */
	if ((lun[0] & 0xC000) != 0x8000)
		return NULL;

	spin_lock_irqsave(&iue->adapter->lock, flags);
	if (iue->adapter->vbus[bus] == NULL)
		goto out_unlock;

	vd = iue->adapter->vbus[bus]->vdev[target];

	if ((vd == NULL) || (vd->disabled)) {
		vd = NULL;
		goto out_unlock;
	}

	if (vd)
		atomic_inc(&vd->refcount);

out_unlock:
	spin_unlock_irqrestore(&iue->adapter->lock, flags);
	return vd;
}

static long h_copy_rdma(u64 length, unsigned long siobn, dma_addr_t saddr,
			unsigned long diobn, dma_addr_t daddr)
{
	u64 bytes_copied = 0;
	u64 rc;

	while (bytes_copied < length) {
		u64 bytes_to_copy = (length - bytes_copied) > MAX_H_COPY_RDMA ?
		    MAX_H_COPY_RDMA : (length - bytes_copied);
		rc = plpar_hcall_norets(H_COPY_RDMA, bytes_to_copy, siobn, 
					saddr, diobn, daddr);
		if (rc != H_Success)
			return rc;

		bytes_copied += bytes_to_copy;
	}
	return H_Success;
}

/* ==============================================================
 * Information Unit (IU) Pool Routines
 * ==============================================================
 */
/*
 * We keep a pool of IUs, this routine builds the pool.  The pool is
 * per-adapter.  The size of the pool is negotiated as part of the SRP
 * login, where we negotiate the number of requests (IUs) the client
 * can send us.  This routine is not synchronized, since it happens
 * only at probe time.
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
		err("Error: Cannot allocate memory for IU list\n");
		return -ENOMEM;
	}
	memset(pool->list, 0, pool->size * sizeof(*pool->list));

	pool->iu_storage =
	    dma_alloc_coherent(adapter->dev,
				  pool->size * sizeof(*pool->iu_storage),
				  &pool->iu_token, GFP_KERNEL);
	if (!pool->iu_storage) {
		err("Error: Cannot allocate memory for IU pool\n");
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
		if (test_bit(V_IN_USE, &pool->list[i].req.flags))
			++in_use;
	if (in_use)
		err("Releasing event pool with %d IUs still in use!\n", in_use);

	kfree(pool->list);
	dma_free_coherent(adapter->dev,
			  pool->size * sizeof(*pool->iu_storage),
			  pool->iu_storage, pool->iu_token);
}

/*
 * Get an IU from the pool.  Return NULL if the pool is empty.  This
 * routine is syncronized by the adapter lock.  The routine sets all the
 * important fields to 0
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

		if (test_bit(V_IN_USE, &e->req.flags))
			err("Found in-use iu in free pool!");

		memset(&e->req, 0, sizeof(e->req));

		__set_bit(V_IN_USE, &e->req.flags);
	} else
		e = NULL;

	spin_unlock_irqrestore(&adapter->pool.lock, flags);
	atomic_inc(&adapter->iu_count);
	return e;
}

/*
 * Return an IU to the pool.  This routine is synchronized by the
 * adapter lock
 */
static void free_iu(struct iu_entry *iue)
{
	/* iue's with parents are kmalloc'ed, not picked from the pool */
	if (iue->req.parent) {
		kfree(iue);
		return;
	}

	if (iue->req.vd)
		atomic_dec(&iue->req.vd->refcount);

	if (!test_bit(V_IN_USE, &iue->req.flags))
		warn("Internal error, freeing iue twice!\n");
	else {
		__clear_bit(V_IN_USE, &iue->req.flags);
		list_add_tail(&iue->next, &iue->adapter->pool.iu_entries);
	}
	atomic_dec(&iue->adapter->iu_count);
}

/*
 * Allocates two iue's and splits the buffer descriptors between them
 */
static int split_iu(struct iu_entry* iue)
{
	int length = 0, i, child1i = 0, count;
	struct iu_entry *child1, *child2;
	struct iu_entry *child_iue;
	struct srp_cmd *child_cmd;
	struct srp_cmd *cmd = &iue->iu->srp.cmd;
	struct indirect_descriptor *child_id;
	struct indirect_descriptor *id = (struct indirect_descriptor *)
					 (cmd->additional_data);

	if (cmd->data_out_format && cmd->data_in_format) {
		err("Don't support bidirectional buffers yet\n");
		return -EPERM;
	}

	dbg("splitting %p len %lx incount %x outcount %x lba %lx\n", iue,
	    iue->req.len, cmd->data_in_count, cmd->data_out_count,
	    iue->req.lba);

	if (iue->req.len < PAGE_SIZE) {
		err("Can't split buffers less than a page\n");
		return -EPERM;
	}

	child1 = kmalloc(sizeof(struct iu_entry) + sizeof(union viosrp_iu),
			 GFP_KERNEL);
	if (child1 == NULL)
		return -ENOMEM;

	child2 = kmalloc(sizeof(struct iu_entry) + sizeof(union viosrp_iu),
			 GFP_KERNEL);
	if (child2 == NULL) {
		free_iu(child1);
		return -ENOMEM;
	}

	child1->iu = (union viosrp_iu *)((char*)child1 + sizeof(*child1));
	child2->iu = (union viosrp_iu *)((char*)child2 + sizeof(*child2));
	child1->adapter = child2->adapter = iue->adapter;
	memset(&child1->req, 0, sizeof(child1->req));
	memset(&child2->req, 0, sizeof(child2->req));
	memset(&child1->iu->srp.cmd, 0, sizeof(struct srp_cmd));
	memset(&child2->iu->srp.cmd, 0, sizeof(struct srp_cmd));
	__set_bit(V_IN_USE, &child1->req.flags);
	__set_bit(V_IN_USE, &child2->req.flags);

	/* Split a direct buffer */
	if (cmd->data_out_format == SRP_DIRECT_BUFFER ||
	    cmd->data_in_format == SRP_DIRECT_BUFFER) {
		struct memory_descriptor *md = (struct memory_descriptor *)
							(cmd->additional_data);
		struct memory_descriptor *ch1_md = (struct memory_descriptor *)
					(child1->iu->srp.cmd.additional_data);
		struct memory_descriptor *ch2_md = (struct memory_descriptor *)
					(child2->iu->srp.cmd.additional_data);

		int npages = (md->length - 1) / PAGE_SIZE + 1;
		ch1_md->length = ((npages + 1) / 2) * PAGE_SIZE;
		ch2_md->length = md->length - ch1_md->length;
		ch1_md->virtual_address = md->virtual_address;
		ch2_md->virtual_address = md->virtual_address + ch1_md->length;
		child1->req.len = ch1_md->length;
		child2->req.len = ch2_md->length;
		goto splitted;
	}

	child_iue = child1;
	child_cmd = &child1->iu->srp.cmd;
	child_id = (struct indirect_descriptor *) (child_cmd->additional_data);
	count = iue->req.rw ? cmd->data_out_count : cmd->data_in_count;

	for (i = 0; i < count ; i++) {
		child_id->list[i - child1i].length = id->list[i].length;
		child_id->list[i - child1i].virtual_address =
						id->list[i].virtual_address;
		if (iue->req.rw)
			child_cmd->data_out_count++;
		else
			child_cmd->data_in_count++;

		child_id->total_length += id->list[i].length;
		child_id->head.length += sizeof(struct memory_descriptor);
		length += id->list[i].length;
		child_iue->req.len += id->list[i].length;
		if (!child1i && (length >= iue->req.len / 2 ||
		    i >= count - 2)) {
			child_iue = child2;
			child_cmd = &child2->iu->srp.cmd;
			child_id = (struct indirect_descriptor *)
						(child_cmd->additional_data);
			child1i = i + 1;
		}
	}

splitted:
	child1->iu->srp.cmd.data_out_format = iue->iu->srp.cmd.data_out_format;
	child1->iu->srp.cmd.data_in_format = iue->iu->srp.cmd.data_in_format;
	child2->iu->srp.cmd.data_out_format = iue->iu->srp.cmd.data_out_format;
	child2->iu->srp.cmd.data_in_format = iue->iu->srp.cmd.data_in_format;

	if (child1->iu->srp.cmd.data_out_count == 1 ||
	    child1->iu->srp.cmd.data_in_count == 1)
		make_direct_buffer(&child1->iu->srp.cmd);
	if (child2->iu->srp.cmd.data_out_count == 1 ||
	    child2->iu->srp.cmd.data_in_count == 1)
		make_direct_buffer(&child2->iu->srp.cmd);

	child1->req.rw = child2->req.rw = iue->req.rw;
	__set_bit(V_PARSED, &child1->req.flags);
	__set_bit(V_PARSED, &child2->req.flags);
	child1->req.lba = iue->req.lba;
	child2->req.lba = iue->req.lba + (child1->req.len >> 9);

	iue->req.child[0] = child1;
	iue->req.child[1] = child2;
	child1->req.parent = child2->req.parent = iue;
	child1->req.vd = child2->req.vd = iue->req.vd;

	return 0;
}

/* ==============================================================
 * Data buffer cache routines.  Note that we don't NEED a
 * data cache, but this eliminates mapping and unmapping DMA
 * addresses for data buffers on every request, which can be quite
 * expensive on a PPC64 system.  santi hates these routines (that
 * just do first-fit allocation) but they are Good Enough (tm) until
 * he writes something more elegant.
 * ==============================================================
 */
/*
 * Get some data buffers to start.  This doesn't lock the adapter structure!
 */
static void init_data_buffer(struct server_adapter *adapter)
{
	int i;

	for (i = 0; i < DMA_BUFFER_INIT_COUNT; i++) {
		if (adapter->dma_buffer[i].addr == NULL) {
			adapter->dma_buffer[i].addr =
			    dma_alloc_coherent(adapter->dev, 
					       DMA_BUFFER_INIT_LEN,
					       &adapter->dma_buffer[i].
					       token,
					       GFP_KERNEL);
			adapter->dma_buffer[i].len = DMA_BUFFER_INIT_LEN;
			atomic_inc(&adapter->buffers_allocated);
		}
	}
}

/*
 * Get a memory buffer that includes a mapped DMA address.  Just use first-fit
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
	*buffer = dma_alloc_coherent(adapter->dev, len, data_token, GFP_KERNEL);
	atomic_inc(&adapter->buffers_allocated);
	return;
}

/*
 * Free a memory buffer that includes a mapped DMA address.
 */
static void free_data_buffer(char *buffer, dma_addr_t data_token, size_t len,
			     struct server_adapter *adapter)
{
	int i;

	/* First see if this buffer is already in the cache */
	for (i = 0; i < DMA_BUFFER_CACHE_SIZE; i++) {
		if (adapter->dma_buffer[i].addr == buffer) {
			if (adapter->dma_buffer[i].token != data_token)
				err("Inconsistent data buffer pool info!\n");
			if (!test_and_clear_bit(i, adapter->dma_buffer_use))
				err("Freeing data buffer twice!\n");
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
				smp_mb__before_clear_bit();
				clear_bit(i, adapter->dma_buffer_use);
				return;
			} else
				clear_bit(i, adapter->dma_buffer_use);
		}
	}

	/* Now see if there is a smaller buffer we should throw out */
	for (i = 0; i < DMA_BUFFER_CACHE_SIZE; i++) {
		if (!test_and_set_bit(i, adapter->dma_buffer_use)) {
			if (adapter->dma_buffer[i].len < len) {
				dma_free_coherent(adapter->dev,
						  adapter->dma_buffer[i].len,
						  adapter->dma_buffer[i].addr,
						  adapter->dma_buffer[i].token);
				
				atomic_dec(&adapter->buffers_allocated);

				adapter->dma_buffer[i].addr = buffer;
				adapter->dma_buffer[i].token = data_token;
				adapter->dma_buffer[i].len = len;
				smp_mb__before_clear_bit();
				clear_bit(i, adapter->dma_buffer_use);
				return;
			} else
				clear_bit(i, adapter->dma_buffer_use);
		}
	}

	/* No space to cache this.  Give it back to the kernel */
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

	for (i = 0; i < DMA_BUFFER_CACHE_SIZE; i++) {
		if (adapter->dma_buffer[i].addr != NULL) {
			if (test_bit(i, adapter->dma_buffer_use))
				free_in_use++;
			dma_free_coherent(adapter->dev,
					  adapter->dma_buffer[i].len,
					  adapter->dma_buffer[i].addr,
					  adapter->dma_buffer[i].token);

			atomic_dec(&adapter->buffers_allocated);
		}
	}

	if (free_in_use)
		err("Freeing %d in-use data buffers\n", free_in_use);
	return;
}

/* ==============================================================
 * Inter-OS send and receive routines
 * ==============================================================
 */
/*
 * Get a CRQ from the inter-partition queue.
 */
static struct viosrp_crq *crq_queue_next_crq(struct crq_queue *queue)
{
	struct viosrp_crq *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
	}
	else
		crq = NULL;
	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}

/*
 * Send an IU to another partition using the CRQ.
 */
static int send_iu(struct iu_entry *iue, u64 length, u8 format)
{
	long rc, rc1;
	union {
		struct viosrp_crq cooked;
		u64 raw[2];
	} crq;

	/* First copy the SRP */
	rc = h_copy_rdma(length,
			 iue->adapter->liobn,
			 iue->iu_token,
			 iue->adapter->riobn, iue->req.remote_token);

	if (rc)
		err("Send_iu: Error %ld transferring data to client\n", rc);

	crq.cooked.valid = 0x80;
	crq.cooked.format = format;
	crq.cooked.reserved = 0x00;
	crq.cooked.timeout = 0x00;
	crq.cooked.IU_length = length;
	crq.cooked.IU_data_ptr = iue->iu->srp.generic.tag;

	if (rc == 0)
		crq.cooked.status = 0x99;	/* Just needs to be non-zero */
	else
		crq.cooked.status = 0x00;

	rc1 = h_send_crq(iue->adapter->dma_dev->unit_address,
			 crq.raw[0],
			 crq.raw[1]);

	if (rc1) {
		err("Error %ld sending response to client\n", rc1);
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
		err("send_md_data: Error %ld transferring data to client\n",
			rc);
		return -EIO;
	}

	return tosend;
}

/* Send data to a list of memory descriptors
 */
static int send_md_list(int num_entries, int tosendlen,
		dma_addr_t stoken,
		struct memory_descriptor *md,
		struct iu_entry *iue)
{
	int i, thislen, bytes;
	int sentlen = 0;

	for (i = 0; ((i < num_entries) && (tosendlen)); i++) {
		if (tosendlen > md[i].length)
			thislen = md[i].length;
		else
			thislen = tosendlen;

		bytes = send_md_data(stoken + sentlen, thislen,
				md + i, iue->adapter);
		if (bytes < 0)
			return bytes;

		if (bytes != thislen)
			warn("Error: Tried to send %d, sent %d\n", thislen,
			     bytes);

		sentlen += bytes;
		tosendlen -= bytes;
	}
	return sentlen;
}

/*
 * Send data to the SRP data_in buffers
 * Returns amount of data sent, or negative value on error
 */
static long send_cmd_data(dma_addr_t stoken, int len, struct iu_entry *iue)
{
	struct srp_cmd *cmd = &iue->iu->srp.cmd;
	struct memory_descriptor *md = NULL, *ext_list = NULL;
	struct indirect_descriptor *id = NULL;
	dma_addr_t data_token;
	int offset = 0;
	int sentlen = 0;
	int num_md, rc;

	offset = cmd->additional_cdb_len * 4 + data_out_desc_size(cmd);

	switch (cmd->data_in_format) {
	case SRP_NO_BUFFER:
		return 0;
	case SRP_DIRECT_BUFFER:
		md = (struct memory_descriptor *)(cmd->additional_data +
						  offset);
		sentlen = send_md_data(stoken, len, md, iue->adapter);
		len -= sentlen;
		if (len) {
			__set_bit(V_DIOVER, &iue->req.flags);
			iue->req.data_in_residual_count = len;
		}
		return sentlen;
	}

	if (cmd->data_in_format != SRP_INDIRECT_BUFFER) {
		err("client error Invalid data_in_format %d\n",
		    cmd->data_in_format);
		return 0;
	}

	id = (struct indirect_descriptor *)(cmd->additional_data + offset);
	num_md = id->head.length / sizeof(struct memory_descriptor);

	if (num_md == cmd->data_in_count)
		md = &id->list[0];

	else {
		ext_list = dma_alloc_coherent(iue->adapter->dev, 
					      id->head.length,
					      &data_token,
					      GFP_KERNEL);
		if (!ext_list) {
			err("Error dma_alloc_coherent indirect table!\n");
			return 0;
		}

		/* get indirect memory descriptor table from initiator */
		rc = h_copy_rdma(id->head.length,
				iue->adapter->riobn,
				id->head.virtual_address,
				iue->adapter->liobn,
				data_token);
		if (rc != H_Success) {
			err("Error copying indirect table rc %d\n", rc);
			return 0;
		}

		md = (struct memory_descriptor *)ext_list;
	}

	/* Work through the memory descriptor list */
	sentlen = send_md_list(num_md, len, stoken, md, iue);
	if (sentlen < 0 )
		return sentlen;

	len -= sentlen;

	if (len) {
		__set_bit(V_DIOVER, &iue->req.flags);
		iue->req.data_in_residual_count = len;
	}

	if (ext_list)
		dma_free_coherent(iue->adapter->dev,
			id->head.length, ext_list, data_token);

	return sentlen;
}

/*
 * Get data from the other partition from a single SRP memory descriptor
 * Returns amount of data received, or negative value on error
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
		err("get_md_data: Error %ld transferring data from client\n",
			rc);
		return -EIO;
	}

	return toget;
}

static int get_md_list(int num_entries, int togetlen,
			dma_addr_t stoken,
			struct memory_descriptor *md,
			struct iu_entry *iue)
{
	int i, thislen, bytes;
	int gotlen = 0;

	for (i = 0; ((i < num_entries) && (togetlen)); i++) {
		if (togetlen > md[i].length)
			thislen = md[i].length;
		else
			thislen = togetlen;

		bytes = get_md_data(stoken + gotlen, thislen, md + i, 
				    iue->adapter);
		if (bytes < 0)
			return bytes;

		if (bytes != thislen)
			err("Partial data got from client (%d/%d)\n",
				bytes, thislen);

		gotlen += bytes;
		togetlen -= bytes;
	}

	return gotlen;
}

/*
 * Get data from an SRP data in area.
 * Returns amount of data received, or negative value on error
 */
static long get_cmd_data(dma_addr_t stoken, int len, struct iu_entry *iue)
{
	struct srp_cmd *cmd = &iue->iu->srp.cmd;
	struct memory_descriptor *md, *ext_list;
	struct indirect_descriptor *id;
	dma_addr_t data_token;
	int offset = 0;
	int total_length = 0;
	int num_md, rc;
	int gotlen = 0;

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
		err("client error: Invalid data_out_format %d\n",
		    cmd->data_out_format);
		return 0;
	}

	id = (struct indirect_descriptor *)(cmd->additional_data + offset);

	total_length = id->total_length;

	num_md = id->head.length / sizeof(struct memory_descriptor);

	if (num_md == cmd->data_out_count) {
		/* Work through the partial memory descriptor list */
		gotlen = get_md_list(num_md, len,
				stoken, &id->list[0], iue);
		return gotlen;
	}

	/* get indirect table */

	ext_list = dma_alloc_coherent(iue->adapter->dev, 
				      id->head.length,
				      &data_token,
				      GFP_KERNEL);
	if (!ext_list) {
		err("Error dma_alloc_coherent indirect table!\n");
		return 0;
	}

	/* get indirect memory descriptor table */
	rc = h_copy_rdma(id->head.length,
			iue->adapter->riobn,
			id->head.virtual_address,
			iue->adapter->liobn,
			data_token);
	if (rc != H_Success) {
		err("Error copying indirect table rc %d\n", rc);
		dma_free_coherent(iue->adapter->dev,
				  id->head.length,
				  ext_list, data_token);
		return 0;
	}

	gotlen = get_md_list(num_md, len,
				stoken, ext_list, iue);
	dma_free_coherent(iue->adapter->dev,
			  id->head.length,
			  ext_list, data_token);
	
	return gotlen;
}

/*
 * Send an SRP response that includes sense data
 */
static long send_rsp(struct iu_entry *iue,
		     unsigned char status,
		     unsigned char asc)
{
	u8 *sense = iue->iu->srp.rsp.sense_and_response_data;
	u64 tag = iue->iu->srp.generic.tag;
	union viosrp_iu *iu = iue->iu;
	unsigned long flags;

	if (status != NO_SENSE)
		atomic_inc(&iue->adapter->errors);

	if (iue->req.parent) {
		struct iu_entry *parent = iue->req.parent;
		if (parent->req.child[0] == iue)
			parent->req.child[0] = NULL;
		else if (parent->req.child[1] == iue)
			parent->req.child[1] = NULL;
		else
			err("parent %p doesn't know child!\n", iue->req.parent);

		/* get only the first error */
		if (status && !parent->req.child_status)
			parent->req.child_status = status;

		/* all children are done, send response */
		if (!parent->req.child[0] && !parent->req.child[1]) {
			if (!test_bit(V_ABORTED, &parent->req.flags))
				send_rsp(parent, parent->req.child_status, 
					 0x00);
			else
				iue->adapter->next_rsp_delta++;

			__set_bit(V_DONE, &parent->req.flags);
			kblockd_schedule_work(&iue->adapter->crq_task);
		}
		return 0;
	}
	/* If the linked bit is on and status is good */
	if (test_bit(V_LINKED, &iue->req.flags) && (status == NO_SENSE))
		status = 0x10;

	memset(iu, 0, sizeof(struct srp_rsp));
	iu->srp.rsp.type = SRP_RSP_TYPE;
	spin_lock_irqsave(&iue->adapter->lock, flags);
	iu->srp.rsp.request_limit_delta = 1 + iue->adapter->next_rsp_delta;
	iue->adapter->next_rsp_delta = 0;
	spin_unlock_irqrestore(&iue->adapter->lock, flags);
	iu->srp.rsp.tag = tag;

	iu->srp.rsp.diover = test_bit(V_DIOVER, &iue->req.flags) ? 1 : 0;

	iu->srp.rsp.data_in_residual_count = iue->req.data_in_residual_count;
	iu->srp.rsp.data_out_residual_count = iue->req.data_out_residual_count;

	iu->srp.rsp.rspvalid = 0;

	iu->srp.rsp.response_data_list_length = 0;

	if (status && !iue->req.sense) {
		iu->srp.rsp.status = SAM_STAT_CHECK_CONDITION;
		iu->srp.rsp.snsvalid = 1;
		iu->srp.rsp.sense_data_list_length = 18;

		/* Valid bit and 'current errors' */
		sense[0] = (0x1 << 7 | 0x70);

		/* Sense key */
		sense[2] = status;

		/* Additional sense length */
		sense[7] = 0xa;	/* 10 bytes */

		/* Additional sense code */
		sense[12] = asc;
	} else {
		if (iue->req.sense) {
			iu->srp.rsp.snsvalid = 1;
			iu->srp.rsp.sense_data_list_length =
							SCSI_SENSE_BUFFERSIZE;
			memcpy(sense, iue->req.sense, SCSI_SENSE_BUFFERSIZE);
		}
		iu->srp.rsp.status = status;
	}

	send_iu(iue, sizeof(iu->srp.rsp), VIOSRP_SRP_FORMAT);

	return 0;
}

/* ==============================================================
 * Block device endio routines (top and bottom)
 * ==============================================================
 */
static void finish_iue(struct iu_entry *iue)
{
	int bytes;
	unsigned long flags;
	struct server_adapter *adapter = iue->adapter;

	/* Send back the SRP and data if this request was NOT
	 * aborted
	 */
	if (test_bit(V_ABORTED, &iue->req.flags)) {
		spin_lock_irqsave(&adapter->lock, flags);
		adapter->next_rsp_delta++;
		spin_unlock_irqrestore(&adapter->lock, flags);
		goto out;
	}

	if (iue->req.ioerr) {
		err("Block operation failed\n");
		send_rsp(iue, HARDWARE_ERROR, 0x00);
		goto out;
	}

	if (test_bit(V_WRITE, &iue->req.flags)) {
		send_rsp(iue, NO_SENSE, 0x00);
		goto out;
	}

	/* return data if this was a read */
	bytes = send_cmd_data(iue->req.data_token,
			      iue->req.data_len,
			      iue);
	if (bytes != iue->req.data_len) {
		err("Error sending data on response (tried %ld, sent %d\n",
		    iue->req.data_len, bytes);
		send_rsp(iue, ABORTED_COMMAND, 0x00);
	} else
		send_rsp(iue, NO_SENSE, 0x00);

out:	free_data_buffer(iue->req.data_buffer,
			 iue->req.data_token, iue->req.data_len,
			 adapter);
	spin_lock_irqsave(&adapter->lock, flags);
	free_iu(iue);
	spin_unlock_irqrestore(&adapter->lock, flags);
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

	if (bio->bi_size)
		return 1;

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		iue->req.ioerr = 1;

	/* Add the bio to the done queue */
	spin_lock_irqsave(&adapter->lock, flags);
	if (adapter->bio_donetail) {
		adapter->bio_donetail->bi_next = bio;
		adapter->bio_donetail = bio;
	} else
		adapter->bio_done = adapter->bio_donetail = bio;
	bio->bi_next = NULL;
	spin_unlock_irqrestore(&adapter->lock, flags);

	/* Schedule the task */
	tasklet_schedule(&adapter->endio_tasklet);

	return 0;
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
	unsigned long flags;

	do {
		iue = NULL;
		spin_lock_irqsave(&adapter->lock, flags);
		bio = adapter->bio_done;
		if (bio) {
			if (bio == adapter->bio_donetail)
				adapter->bio_donetail = NULL;
			adapter->bio_done = bio->bi_next;
			bio->bi_next = NULL;

			/* Remove this iue from the in-flight list */
			iue = (struct iu_entry *)bio->bi_private;
			if (!test_bit(V_IN_USE, &iue->req.flags)) {
				err("Internal error! freed iue in bio!!!\n");
				spin_unlock_irqrestore(&adapter->lock, flags);
				return;
			}

			list_del(&iue->next);
		}

		spin_unlock_irqrestore(&adapter->lock, flags);

		if (iue) {
			finish_iue(iue);
			bio_put(bio);
			atomic_dec(&adapter->bio_count);
		}
	} while (bio);
	kblockd_schedule_work(&adapter->crq_task);
}

/* ==============================================================
 * SCSI Command Emulation Routines
 * ==============================================================
 */
/*
 * Process an inquiry SCSI Command
 */
static int process_inquiry(struct iu_entry *iue)
{
	struct inquiry_data *id;
	dma_addr_t data_token;
	u8 *raw_id;
	int bytes;
	unsigned long flags;
	int genhd_flags;

	id = dma_alloc_coherent(iue->adapter->dev, sizeof(*id), &data_token, 
				GFP_KERNEL);

	if (id == NULL) {
		err("Not able to get inquiry buffer, retrying later\n");
		return RETRY;
	}

	raw_id = (u8 *)id;
	memset(id, 0, sizeof(*id));

	/* If we have a valid device */
	if (iue->req.vd) {
		genhd_flags = iue->req.vd->b.bdev->bd_disk->flags;
		/* Standard inquiry page */
		if ((iue->iu->srp.cmd.cdb[1] == 0x00) &&
		    (iue->iu->srp.cmd.cdb[2] == 0x00)) {
			dbg("  inquiry returning device\n");
			id->qual_type = iue->req.vd->b.scsi_type;
			id->rmb_reserve =
			    iue->req.vd->b.removable ? 0x80 : 0x00;
			id->version = 0x84;	/* ISO/IE		  */
			id->aerc_naca_hisup_format = 0x22;/* naca & fmt 0x02 */
			id->addl_len = sizeof(*id) - 4;
			id->bque_encserv_vs_multip_mchngr_reserved = 0x00;
			id->reladr_reserved_linked_cmdqueue_vs = 0x02;/*CMDQ*/
			memcpy(id->vendor, "IBM	    ", 8);
			/* Don't even ask about the next bit.  AIX uses
			 * hardcoded device naming to recognize device types
			 * and their client won't  work unless we use VOPTA and
			 * VDASD.
			 */
			if (id->qual_type == TYPE_ROM)
				memcpy(id->product, "VOPTA blkdev    ", 16);
			else
				memcpy(id->product, "VDASD blkdev    ", 16);
			memcpy(id->revision, "0001", 4);
			snprintf(id->unique,sizeof(id->unique),
				 "IBM-VSCSI-%s-P%d-%x-%d-%d-%d\n",
				 system_id,
				 partition_number,
				 iue->adapter->dma_dev->unit_address,
				 GETBUS(iue->req.vd->lun),
				 GETTARGET(iue->req.vd->lun),
				 GETLUN(iue->req.vd->lun));
		} else if ((iue->iu->srp.cmd.cdb[1] == 0x01) &&
			   (iue->iu->srp.cmd.cdb[2] == 0x00)) {
			/* Supported VPD pages */
			id->qual_type = iue->req.vd->b.scsi_type;
			raw_id[1] = 0x80; /* page */
			raw_id[2] = 0x00; /* reserved */
			raw_id[3] = 0x03; /* length */
			raw_id[4] = 0x00; /* page 0 */
			raw_id[5] = 0x80; /* serial number page */
		} else if ((iue->iu->srp.cmd.cdb[1] == 0x01) &&
			   (iue->iu->srp.cmd.cdb[2] == 0x80)) {
			/* serial number page */
			id->qual_type = iue->req.vd->b.scsi_type;
			raw_id[1] = 0x80; /* page */
			raw_id[2] = 0x00; /* reserved */
			snprintf((char *)(raw_id+4),
				 sizeof(*id)-4,
				 "IBM-VSCSI-%s-P%d-%x-%d-%d-%d\n",
				 system_id,
				 partition_number,
				 iue->adapter->dma_dev->unit_address,
				 GETBUS(iue->req.vd->lun),
				 GETTARGET(iue->req.vd->lun),
				 GETLUN(iue->req.vd->lun));
			raw_id[3] = strlen((char *)raw_id+4);
		} else {
			/* Some unsupported data */
			err("unknown inquiry page %d %d\n",
			    iue->iu->srp.cmd.cdb[1],
			    iue->iu->srp.cmd.cdb[2]);
			send_rsp(iue, ILLEGAL_REQUEST, 0x24);
			return FREE_IU;
		}
	} else {
		dbg("  inquiry returning no device\n");
		id->qual_type = 0x7F;	/* Not supported, no device */
	}

	if (test_bit(V_ABORTED, &iue->req.flags)) {
		spin_lock_irqsave(&iue->adapter->lock, flags);
		iue->adapter->next_rsp_delta++;
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		dma_free_coherent(iue->adapter->dev, sizeof(*id), id,
				  data_token);
		return FREE_IU;
	}

	bytes = send_cmd_data(data_token, iue->iu->srp.cmd.cdb[4], iue);

	dma_free_coherent(iue->adapter->dev, sizeof(*id), id, data_token);

	if (bytes < 0)
		send_rsp(iue, HARDWARE_ERROR, 0x00);
	else
		send_rsp(iue, NO_SENSE, 0x00);

	return FREE_IU;
}

/*
 * Handle an I/O.  Called by WRITE6, WRITE10, etc
 */
static int process_rw(char *cmd, int rw, struct iu_entry *iue, long lba,
		       long len)
{
	char *buffer;
	struct bio *bio;
	int bytes;
	int num_biovec;
	int cur_biovec;
	long flags;

	dbg("%s lba %ld, len %ld\n",cmd,lba,len);

	if (rw == WRITE)
		atomic_inc(&iue->adapter->write_processed);
	else if (rw == READ)
		atomic_inc(&iue->adapter->read_processed);
	else {
		err("Major internal error...rw not read or write\n");
		send_rsp(iue, HARDWARE_ERROR, 0x00);
		return FREE_IU;
	}

	if (len == 0) {
		warn("Zero length I/O\n");
		send_rsp(iue, ILLEGAL_REQUEST, 0x20);
		return FREE_IU;
	}

	/* Writing to a read-only device */
	if ((rw == WRITE) && (iue->req.vd->b.ro)) {
		warn("WRITE to read-only device\n");
		send_rsp(iue, DATA_PROTECT, 0x27);
		return FREE_IU;
	}

	iue->req.rw = rw;
	iue->req.lba = lba;
	iue->req.len = len;
	__set_bit(V_PARSED, &iue->req.flags);

	if (bdev_get_queue(iue->req.vd->b.bdev)->max_sectors < (len >> 9))
		return RETRY_SPLIT_BUF;

	get_data_buffer(&buffer, &iue->req.data_token, len, iue->adapter);
	iue->req.data_buffer = buffer;
	iue->req.data_len = len;

	if (buffer == NULL || dma_mapping_error(iue->req.data_token)) {
		err("Not able to get %lu pages for buffer, retrying later\n",
		    len / PAGE_SIZE);

		return RETRY_SPLIT_BUF;
	}

	/* if reladr */
	if (iue->iu->srp.cmd.cdb[1] & 0x01)
		lba = lba + iue->req.vd->b.lastlba;

	/* If this command is linked, Keep this lba */
	if (test_bit(V_LINKED, &iue->req.flags))
		iue->req.vd->b.lastlba = lba;
	else
		iue->req.vd->b.lastlba = 0;

	if (rw == WRITE) {
		__set_bit(V_WRITE, &iue->req.flags);
		/* Get the data */
		bytes = get_cmd_data(iue->req.data_token, len, iue);
		if (bytes != len) {
			err("Error transferring data\n");
			free_data_buffer(buffer, iue->req.data_token, len,
				iue->adapter);
			send_rsp(iue, HARDWARE_ERROR, 0x00);
			return FREE_IU;
		}
	}

	num_biovec = (len - 1) / iue->req.vd->b.blocksize + 1;

	bio = bio_alloc(GFP_KERNEL, num_biovec);
	if (!bio) {
		err("Not able to allocate a bio, retrying later\n");

		free_data_buffer(buffer, iue->req.data_token, len,
			iue->adapter);

		return RETRY;
	}

	if (test_bit(V_ABORTED, &iue->req.flags)) {
		spin_lock_irqsave(&iue->adapter->lock, flags);
		iue->adapter->next_rsp_delta++;
		free_data_buffer(buffer, iue->req.data_token, len,
			iue->adapter);
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		bio_put(bio);
		return FREE_IU;
	}

	atomic_inc(&iue->adapter->bio_count);
	bio->bi_size = len;
	bio->bi_bdev = iue->req.vd->b.bdev;
	bio->bi_sector = lba * (iue->req.vd->b.sectsize >> 9);
	bio->bi_end_io = &ibmvscsis_end_io;
	bio->bi_private = iue;
	bio->bi_rw = (rw == WRITE) ? 1 : 0;
	bio->bi_rw |= 1 << BIO_RW_SYNC;
	bio->bi_phys_segments = 1;
	bio->bi_hw_segments = 1;
	if (bdev_get_queue(bio->bi_bdev)->ordered != QUEUE_ORDERED_NONE 
	    && test_bit(V_BARRIER, &iue->req.flags))
		bio->bi_rw |= 1 << BIO_RW_BARRIER;


	/* This all assumes that the buffers we get are page-aligned */
	for (cur_biovec = 0; cur_biovec < num_biovec; cur_biovec++) {
		long thislen;

		if (len > iue->req.vd->b.blocksize)
			thislen = iue->req.vd->b.blocksize;
		else
			thislen = len;

		bio->bi_io_vec[cur_biovec].bv_page = virt_to_page(buffer);
		bio->bi_io_vec[cur_biovec].bv_len = thislen;
		bio->bi_io_vec[cur_biovec].bv_offset =
		    (unsigned long)buffer & (PAGE_SIZE-1);
		bio->bi_vcnt++;

		len -= thislen;
		buffer += thislen;
	}
	generic_make_request(bio);
	return INFLIGHT;
}

/*
 * Process a READ6
 */
static int process_read6(struct iu_entry *iue)
{
	long lba = (*((u32 *) (iue->iu->srp.cmd.cdb))) & 0x001FFFFF;
	long len = iue->iu->srp.cmd.cdb[4];

	/* Length of 0 indicates 256 */
	if (len == 0)
		len = 256;

	len = len * iue->req.vd->b.sectsize;

	return process_rw("Read6", READ, iue, lba, len);
}

/*
 * Process a {READ,WRITE}{6,10,12}
 */
static int process_read10(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u16 *) (iue->iu->srp.cmd.cdb + 7)) * iue->req.vd->b.sectsize;

	return process_rw("Read10", READ, iue, lba, len);
}

static int process_read12(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u32 *) (iue->iu->srp.cmd.cdb + 6)) * iue->req.vd->b.sectsize;

	return process_rw("Read12", READ, iue, lba, len);
}

static int process_write6(struct iu_entry *iue)
{
	long lba = (*((u32 *) (iue->iu->srp.cmd.cdb))) & 0x001FFFFF;
	long len = iue->iu->srp.cmd.cdb[4];

	/* Length of 0 indicates 256 */
	if (len == 0)
		len = 256;

	len = len * iue->req.vd->b.sectsize;

	return process_rw("Write6", WRITE, iue, lba, len);
}

static int process_write10(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u16 *) (iue->iu->srp.cmd.cdb + 7)) * iue->req.vd->b.sectsize;

	return process_rw("Write10", WRITE, iue, lba, len);
}

static int process_write12(struct iu_entry *iue)
{
	long lba = *((u32 *) (iue->iu->srp.cmd.cdb + 2));
	long len =
	    *((u32 *) (iue->iu->srp.cmd.cdb + 6)) * iue->req.vd->b.sectsize;

	return process_rw("Write12", WRITE, iue, lba, len);
}

/*
 * Handle Read Capacity
 */
static int process_read_capacity(struct iu_entry *iue)
{
	struct read_capacity_data {
		u32 blocks;
		u32 blocksize;
	} *cap;
	dma_addr_t data_token;
	int bytes;
	unsigned long flags;

	cap = dma_alloc_coherent(iue->adapter->dev, sizeof(*cap), &data_token, 
				 GFP_KERNEL);

	if (cap == NULL) {
		err("Not able to get capacity buffer, retrying later\n");
		return RETRY;
	}

	/* return block size and last valid block */
	cap->blocksize = iue->req.vd->b.sectsize;
	cap->blocks = 
	    iue->req.vd->b.bdev->bd_inode->i_size / cap->blocksize - 1;

	dbg("capacity %ld bytes, %d blocks, %d blocksize\n",
	    (long)iue->req.vd->b.bdev->bd_inode->i_size,
	    cap->blocks,
	    cap->blocksize);


	if (test_bit(V_ABORTED, &iue->req.flags)) {
		spin_lock_irqsave(&iue->adapter->lock, flags);
		iue->adapter->next_rsp_delta++;
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		dma_free_coherent(iue->adapter->dev, sizeof(*cap), cap,
				  data_token);
		return FREE_IU;
	}

	bytes = send_cmd_data(data_token, sizeof(*cap), iue);

	dma_free_coherent(iue->adapter->dev, sizeof(*cap), cap, data_token);

	if (bytes != sizeof(*cap))
		err("Error sending read capacity data. bytes %d, wanted %ld\n",
		    bytes, sizeof(*cap));

	send_rsp(iue, NO_SENSE, 0x00);

	return FREE_IU;
}

/*
 * Process Mode Sense
 */
static int process_mode_sense(struct iu_entry *iue)
{
	dma_addr_t data_token;
	int bytes;
	unsigned long flags;

	u8 *mode = dma_alloc_coherent(iue->adapter->dev, MODE_SENSE_BUFFER_SIZE,
					 &data_token, GFP_KERNEL);

	if (mode == NULL) {
		err("Not able to get mode buffer, retrying later\n");
		return RETRY;
	}

	/* which page */
	switch (iue->iu->srp.cmd.cdb[2]) {
	case 0:
	case 0x3f:
		mode[1] = 0x00;	/* Default medium */
		if (iue->req.vd->b.ro)
			mode[2] = 0x80;	/* device specific  */
		else
			mode[2] = 0x00;	/* device specific  */

		/* note the DPOFUA bit is set to zero! */
		mode[3] = 0x08;	/* block descriptor length */
		*((u32 *) & mode[4]) =
		    iue->req.vd->b.bdev->bd_inode->i_size
		    / iue->req.vd->b.sectsize - 1;

		*((u32 *) & mode[8]) = iue->req.vd->b.sectsize;
		bytes = mode[0] = 12;	/* length */
		break;

	case 0x08:		/* Cache page */
		/* length should be 4 */
		if (iue->iu->srp.cmd.cdb[4] != 4
		    && iue->iu->srp.cmd.cdb[4] != 0x20) {
			send_rsp(iue, ILLEGAL_REQUEST, 0x20);
			dma_free_coherent(iue->adapter->dev,
					  MODE_SENSE_BUFFER_SIZE,
					  mode, data_token);
			return FREE_IU;
		}

		mode[1] = 0x00;	/* Default medium */
		if (iue->req.vd->b.ro)
			mode[2] = 0x80;	/* device specific */
		else
			mode[2] = 0x00;	/* device specific */

		/* note the DPOFUA bit is set to zero! */
		mode[3] = 0x08;	/* block descriptor length */
		*((u32 *) & mode[4]) =
		    iue->req.vd->b.bdev->bd_inode->i_size
		    / iue->req.vd->b.sectsize - 1;
		*((u32 *) & mode[8]) = iue->req.vd->b.sectsize;

		/* Cache page */
		mode[12] = 0x08;    /* page */
		mode[13] = 0x12;    /* page length */
		mode[14] = 0x01;    /* no cache (0x04 for read/write cache) */

		bytes = mode[0] = 12 + mode[13];	/* length */
		break;
	default:
		warn("Request for unknown mode page %d\n",
		     iue->iu->srp.cmd.cdb[2]);
		send_rsp(iue, ILLEGAL_REQUEST, 0x20);
		dma_free_coherent(iue->adapter->dev,
				  MODE_SENSE_BUFFER_SIZE, mode, data_token);
		return FREE_IU;
	}

	if (test_bit(V_ABORTED, &iue->req.flags)) {
		spin_lock_irqsave(&iue->adapter->lock, flags);
		iue->adapter->next_rsp_delta++;
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		dma_free_coherent(iue->adapter->dev,
				  MODE_SENSE_BUFFER_SIZE, mode, data_token);
		return FREE_IU;
	}

	bytes = send_cmd_data(data_token, bytes, iue);

	dma_free_coherent(iue->adapter->dev,
			  MODE_SENSE_BUFFER_SIZE, mode, data_token);

	send_rsp(iue, NO_SENSE, 0x00);

	return FREE_IU;
}

/*
 * Report LUNS command.
 */
static int process_reportLUNs(struct iu_entry *iue)
{
	int listsize = vscsis_data_length(&iue->iu->srp.cmd, 0);
	dma_addr_t data_token;
	int index = 2;	/* Start after the two entries (length and LUN0) */
	int bus;
	int target;
	int bytes;
	unsigned long flags;

	u64 *lunlist = dma_alloc_coherent(iue->adapter->dev, listsize,
					  &data_token, GFP_KERNEL);

	if (lunlist == NULL) {
		err("Not able to get lunlist buffer, retrying later\n");
		return RETRY;
	}

	memset(lunlist, 0, listsize);

	/* work out list size in units of u64 */
	listsize = listsize / 8;

	if (listsize < 1) {
		send_rsp(iue, ILLEGAL_REQUEST, 0x20);
		return FREE_IU;
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
		if (!iue->adapter->vbus[bus])
			continue;
		/* loop through the targets */
		for (target = 0; target < TARGETS_PER_BUS; target++) {
			if (!iue->adapter->vbus[bus]->vdev[target])
				continue;
			/* If the target exists */
			if ((index < listsize) &&
			    (!iue->adapter->vbus[bus]->
			     vdev[target]->disabled)) {
				lunlist[index++] =
				    iue->adapter->vbus[bus]->vdev[target]->lun;
				dbg("  lun %16.16lx\n",
				    iue->adapter->vbus[bus]->vdev[target]->lun);
			}
			
		}
	}

      send_lunlist:
	spin_unlock_irqrestore(&iue->adapter->lock, flags);

	if (test_bit(V_ABORTED, &iue->req.flags)) {
		spin_lock_irqsave(&iue->adapter->lock, flags);
		iue->adapter->next_rsp_delta++;
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		dma_free_coherent(iue->adapter->dev, listsize * 8, lunlist,
				  data_token);
		return FREE_IU;
	}

	bytes = send_cmd_data(data_token, (index * 8), iue);

	dma_free_coherent(iue->adapter->dev, listsize * 8,
			  lunlist, data_token);

	if (bytes != (index * 8)) {
		err("Error sending report luns data. bytes %d, wanted %d\n",
		    bytes, index * 4);
		send_rsp(iue, ABORTED_COMMAND, 0x00);
	} else
		send_rsp(iue, NO_SENSE, 0x00);

	return FREE_IU;
}

/* For unrecognized SCSI commands, try passing them
 * through
 */
static int try_passthru(struct iu_entry *iue)
{
	request_queue_t *q = bdev_get_queue(iue->req.vd->b.bdev);
	struct request *rq;
	char *buffer;
	int dodlen = vscsis_data_length(&iue->iu->srp.cmd, 1);
	int didlen = vscsis_data_length(&iue->iu->srp.cmd, 0);
	int bytes, len, rw;
	int err = 0;

	if (dodlen && didlen)
		return -EIO;

	if (dodlen)
		rw = WRITE;
	else
		rw = READ;

	len = dodlen + didlen;

	if (len) {
		get_data_buffer(&buffer, &iue->req.data_token, len, iue->adapter);
		if (!buffer) {
			err("Unable to get data buffer of len %d\n",len);
			return -ENOMEM;
		}

		if (dodlen) {
			bytes = get_cmd_data(iue->req.data_token, len, iue);
			if (bytes != len) {
				err("Error transferring data\n");
				free_data_buffer(buffer,
						 iue->req.data_token,
						 len,
						 iue->adapter);
				return -ENOMEM;
			}
		}
	} else
		buffer = NULL;

	rq = blk_get_request(q, rw, __GFP_WAIT);
	rq->flags |= REQ_BLOCK_PC;
	rq->data = buffer;
	rq->data_len = len;
	rq->timeout = iue->req.timeout;

	memcpy(rq->cmd, iue->iu->srp.cmd.cdb, BLK_MAX_CDB);
	err = blk_execute_rq(q, iue->req.vd->b.bdev->bd_disk, rq, 
			     ELEVATOR_INSERT_BACK);
	blk_put_request(rq);
	if ((err == 0) && (rw == READ) && (len)) {
		bytes = send_cmd_data(iue->req.data_token,
				      iue->req.data_len,
				      iue);
		if (bytes != iue->req.data_len) {
			err("Error sending data "
			    "on response "
			    "(tried %ld, sent %d\n",
			    iue->req.data_len, bytes);
			free_data_buffer(buffer,
					 iue->req.data_token,
					 len,
					 iue->adapter);
			err = -EIO;
		}
	}

	if (buffer)
		free_data_buffer(buffer,
				 iue->req.data_token,
				 len,
				 iue->adapter);

	return err;
}

static void reset_changed(struct iu_entry *iue)
{
	if (iue->req.vd->b.changed) {
		bd_set_size(iue->req.vd->b.bdev,
			    (loff_t)get_capacity(iue->req.vd->b.bdev->bd_disk)
		<<9);
		iue->req.vd->b.changed = 0;
	}
}

/*
 * Process an IU when the target is a block device
 */
static int process_cmd_block(struct iu_entry *iue)
{
	union viosrp_iu *iu = iue->iu;
	unsigned long flags;

	if (test_bit(V_PARSED, &iue->req.flags))
		return process_rw("pre-parsed", iue->req.rw, iue, iue->req.lba,
				  iue->req.len);

	if (iu->srp.cmd.cdb[0] == INQUIRY) {
		dbg("INQUIRY lun %16.16lx\n", iue->iu->srp.cmd.lun);
		return process_inquiry(iue);
	}

	if (iue->req.vd &&
	    iue->req.vd->b.removable &&
	    check_disk_change(iue->req.vd->b.bdev)) {
		if (iue->req.vd->b.changed) {
			dbg("Media changed not ready!...cmd 0x%2.2x\n",
			    iu->srp.cmd.cdb[0]);
			send_rsp(iue, NOT_READY, 0x3a);
			return FREE_IU;
		}
		iue->req.vd->b.changed = 1;
		dbg("Media changed attention!...cmd 0x%2.2x\n",
		    iu->srp.cmd.cdb[0]);
		send_rsp(iue, UNIT_ATTENTION, 0x3a);
		return FREE_IU;
	}

	switch (iu->srp.cmd.cdb[0]) {
	case REPORT_LUNS:
		dbg("REPORT LUNS lun %16.16lx\n", iue->iu->srp.cmd.lun);
		return process_reportLUNs(iue);
	case READ_CAPACITY:
		dbg("READ CAPACITY lun %16.16lx\n", iue->iu->srp.cmd.lun);
		return process_read_capacity(iue);
	case MODE_SENSE:
		dbg("MODE SENSE lun %16.16lx\n", iue->iu->srp.cmd.lun);
		return process_mode_sense(iue);
	case TEST_UNIT_READY:
		/* we already know the device exists */
		dbg("TEST UNIT READY lun %16.16lx\n", iue->iu->srp.cmd.lun);
		if (!test_bit(V_ABORTED, &iue->req.flags)) {
			reset_changed(iue);
			send_rsp(iue, NO_SENSE, 0x00);
		}
		else {
			spin_lock_irqsave(&iue->adapter->lock, flags);
			iue->adapter->next_rsp_delta++;
			spin_unlock_irqrestore(&iue->adapter->lock, flags);
		}
		return FREE_IU;
	case START_STOP:
		dbg("START_STOP lun %16.16lx\n", iue->iu->srp.cmd.lun);

		if (!test_bit(V_ABORTED, &iue->req.flags)) {
			reset_changed(iue);
			if ((iu->srp.cmd.cdb[5] & 0x03) == 0x02) {
				/* Unload! */
				if ((iue->req.vd) &&
				    ioctl_by_bdev(iue->req.vd->b.bdev,
						  CDROMEJECT, 0) == 0)
					send_rsp(iue, NO_SENSE, 0x00);
				else
					send_rsp(iue, HARDWARE_ERROR, 0x00);
			} else if ((iu->srp.cmd.cdb[4] & 0x03) == 0x03) {
				iue->req.vd->b.changed = 0;
				if ((iue->req.vd) &&
				    ioctl_by_bdev(iue->req.vd->b.bdev,
						  CDROMCLOSETRAY, 0) == 0)
					send_rsp(iue, NO_SENSE, 0x00);
				else
					send_rsp(iue, HARDWARE_ERROR, 0x00);
			} else
				send_rsp(iue, NO_SENSE, 0x00);
		} else {
			spin_lock_irqsave(&iue->adapter->lock, flags);
			iue->adapter->next_rsp_delta++;
			spin_unlock_irqrestore(&iue->adapter->lock, flags);
		}
		return FREE_IU;
	case READ_6:
		return process_read6(iue);
	case READ_10:
		return process_read10(iue);
	case READ_12:
		return process_read12(iue);
	case WRITE_6:
		return process_write6(iue);
	case WRITE_10:
	case WRITE_VERIFY:
		return process_write10(iue);
	case WRITE_12:
	case WRITE_VERIFY_12:
		return process_write12(iue);
	default:
		dbg("unknown command 0x%2.2x\n`",iu->srp.cmd.cdb[0]);
		if (try_passthru(iue) == 0) {
			dbg("Successfully passed through command 0x%2.2x!\n",
			    iu->srp.cmd.cdb[0]);
			send_rsp(iue, NO_SENSE, 0x00);
			return FREE_IU;
		}

		dbg("Unsupported SCSI Command 0x%2.2x\n", iu->srp.cmd.cdb[0]);

		if (!test_bit(V_ABORTED, &iue->req.flags))
			send_rsp(iue, ILLEGAL_REQUEST, 0x20);
		else {
			spin_lock_irqsave(&iue->adapter->lock, flags);
			iue->adapter->next_rsp_delta++;
			spin_unlock_irqrestore(&iue->adapter->lock, flags);
		}
		return FREE_IU;
	}
}

/* ==============================================================
 * SCSI Redirection Routines
 * ==============================================================
 */
/*
 * Callback when the scsi command issued by process_cmd_scsi() is completed
 */
static void scsi_cmd_done(struct scsi_cmnd *cmd)
{
	struct iu_entry *iue = (struct iu_entry*)cmd->sc_request->
				upper_private_data;
	struct server_adapter *adapter = iue->adapter;
	unsigned long flags;
	int bytes;

	dbg("scsi_cmd_done got cmd %p iue %p\n", cmd, iue);

	spin_lock_irqsave(&adapter->lock, flags);
	list_del(&iue->next);
	spin_unlock_irqrestore(&adapter->lock, flags);

	if (test_bit(V_ABORTED, &iue->req.flags)) {
		dbg("scsi_cmd_done: aborted tag %16.16x\n", cmd->tag);
		spin_lock_irqsave(&iue->adapter->lock, flags);
		iue->adapter->next_rsp_delta++;
		spin_unlock_irqrestore(&iue->adapter->lock, flags);
		goto out;
	}

	if(!test_bit(V_WRITE, &iue->req.flags)) {
		bytes = send_cmd_data(iue->req.data_token,
					  iue->req.data_len, iue);
		if(bytes != iue->req.data_len) {
			err("Error sending data on response (%ld, sent %d)\n",
			    iue->req.data_len, bytes);
			send_rsp(iue, ABORTED_COMMAND, 0x00);
			goto out;
		}
	}

	if (cmd->result)
		iue->req.sense = cmd->sense_buffer;

	send_rsp(iue, cmd->result, 0x00);

out:	scsi_release_request(iue->req.sreq);
	if (iue->req.data_len) {
		free_data_buffer(iue->req.data_buffer, iue->req.data_token,
				 iue->req.data_len, adapter);
	}
	spin_lock_irqsave(&adapter->lock, flags);
	free_iu(iue);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/*
 * Process an IU when the target is a scsi device
 */
static int process_cmd_scsi(struct iu_entry *iue)
{
	union viosrp_iu *iu = iue->iu;
	struct scsi_request *req;
	char *buffer = NULL;
	int len = 0;

	dbg("%x %x %16.16lx[%d:%d:%d][%s] link %d iue %p\n",
	    iu->srp.cmd.cdb[0],
	    iu->srp.cmd.cdb[1],
	    iue->iu->srp.cmd.lun,
	    GETBUS(iue->iu->srp.cmd.lun),
	    GETTARGET(iue->iu->srp.cmd.lun),
	    GETLUN(iue->iu->srp.cmd.lun),
	    iue->req.vd->device_name,
	    test_bit(V_LINKED, &iue->req.flags), iue);

	req = scsi_allocate_request(iue->req.vd->s.sdev, GFP_KERNEL);
	if (req == NULL) {
		err("Not able to get scsi_request, retrying later\n");
		return RETRY;
	}

	memcpy(req->sr_cmnd, iu->srp.cmd.cdb, sizeof(iu->srp.cmd.cdb));

	req->sr_cmd_len = sizeof(iu->srp.cmd.cdb);
	if (iu->srp.cmd.data_out_format && iu->srp.cmd.data_in_format) {
		err("Invalid bidirectional buffer\n");
		send_rsp(iue, ABORTED_COMMAND, 0x00);
		scsi_release_request(req);
		return FREE_IU;
	} else if (iu->srp.cmd.data_out_format) { /* write */
		atomic_inc(&iue->adapter->write_processed);
		req->sr_data_direction = DMA_TO_DEVICE;
		len = vscsis_data_length(&iue->iu->srp.cmd, 1);
		__set_bit(V_WRITE, &iue->req.flags);
		if (iue->req.vd->b.ro) {
			warn("WRITE to read-only device\n");
			send_rsp(iue, DATA_PROTECT, 0x27);
			scsi_release_request(req);
			return FREE_IU;
		}
	} else if (iu->srp.cmd.data_in_format) { /* read */
		atomic_inc(&iue->adapter->read_processed);
		req->sr_data_direction = DMA_FROM_DEVICE;
		len = vscsis_data_length(&iue->iu->srp.cmd, 0);
	} else {
		dbg("No buffer command\n");
		req->sr_data_direction = DMA_NONE;
		goto nobuf;
	}

	get_data_buffer(&buffer, &iue->req.data_token, len, iue->adapter);
	iue->req.data_buffer = buffer;
	iue->req.data_len = len;

	if (test_bit(V_WRITE, &iue->req.flags)) {
		int bytes = get_cmd_data(iue->req.data_token, len, iue);

		if (bytes != len) {
			err("Error transferring data\n");
			free_data_buffer(buffer, iue->req.data_token, len,
					 iue->adapter);
			scsi_release_request(req);
			send_rsp(iue, HARDWARE_ERROR, 0x00);
			return FREE_IU;
		}
	}

nobuf:	req->sr_use_sg = 0;
	req->sr_bufflen = len;
	req->sr_buffer = buffer;
	req->sr_sense_buffer[0] = 0;
	req->sr_request->flags = 
	    test_bit(V_BARRIER, &iue->req.flags) ? REQ_HARDBARRIER : 0;
	req->upper_private_data = (void*)iue;
	iue->req.sreq = req;
	dbg("sending %s of %d bytes, buffer %p, timeout=%d\n",
	    test_bit(V_WRITE, &iue->req.flags) ? "write" : "read", len, buffer,
	    iue->req.timeout);

	scsi_do_req(req, iu->srp.cmd.cdb, buffer, len, scsi_cmd_done,
		    iue->req.timeout, 3);

	return INFLIGHT;

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
	union viosrp_iu *iu = iue->iu;
	u64 tag = iu->srp.generic.tag;

	/* TODO handle case that requested size is wrong and
	 * buffer format is wrong
	 */
	memset(iu, 0, sizeof(struct srp_login_rsp));
	iu->srp.login_rsp.type = SRP_LOGIN_RSP_TYPE;
	iu->srp.login_rsp.request_limit_delta = INITIAL_SRP_LIMIT;
	iu->srp.login_rsp.tag = tag;
	iu->srp.login_rsp.max_initiator_to_target_iulen = sizeof(union srp_iu);
	iu->srp.login_rsp.max_target_to_initiator_iulen = sizeof(union srp_iu);
	/* direct and indirect */
	iu->srp.login_rsp.supported_buffer_formats = 0x0006; 
	iu->srp.login_rsp.multi_channel_result = 0x00; 

	send_iu(iue, sizeof(iu->srp.login_rsp), VIOSRP_SRP_FORMAT);
}

/*
 * Process an incoming device_reset request
 */
static void process_device_reset(struct iu_entry *iue)
{
	struct iu_entry *tmp_iue;
	unsigned long flags;
	union viosrp_iu *iu = iue->iu;
	u64 lun = iu->srp.tsk_mgmt.lun;

	info("device reset for lun %16.16lx\n", lun);

	spin_lock_irqsave(&iue->adapter->lock, flags);

	list_for_each_entry(tmp_iue, &iue->adapter->cmd_queue, next)
		if (tmp_iue->iu->srp.cmd.lun == lun)
			__set_bit(V_ABORTED, &tmp_iue->req.flags);

	spin_unlock_irqrestore(&iue->adapter->lock, flags);
	send_rsp(iue, NO_SENSE, 0x00);
}

/*
 * Process an incoming abort request
 */
static void process_abort(struct iu_entry *iue)
{
	struct iu_entry *tmp_iue;
	unsigned long flags;
	union viosrp_iu *iu = iue->iu;
	u64 tag = iu->srp.tsk_mgmt.managed_task_tag;
	unsigned char status = ABORTED_COMMAND;

	info("aborting task with tag %16.16lx, lun %16.16lx\n",
	     tag, iu->srp.tsk_mgmt.lun);

	spin_lock_irqsave(&iue->adapter->lock, flags);

	list_for_each_entry(tmp_iue, &iue->adapter->cmd_queue, next) {
		if (tmp_iue->iu->srp.cmd.tag != tag)
			continue;

		__set_bit(V_ABORTED, &tmp_iue->req.flags);
		status = NO_SENSE;
		break;
	}

	spin_unlock_irqrestore(&iue->adapter->lock, flags);

	if (status == NO_SENSE)
		info("abort successful\n");
	else
		info("unable to abort cmd\n");

	send_rsp(iue, status, 0x14);
}

/*
 * Process an incoming task management request
 */
static void process_tsk_mgmt(struct iu_entry *iue)
{
	union viosrp_iu *iu = iue->iu;

	if (iu->srp.tsk_mgmt.task_mgmt_flags == 0x01)
		process_abort(iue);
	else if (iu->srp.tsk_mgmt.task_mgmt_flags == 0x08)
		process_device_reset(iue);
	else
		send_rsp(iue, ILLEGAL_REQUEST, 0x20);
}

/*
 * Process an incoming SRP command
 */
static int process_cmd(struct iu_entry *iue)
{
	union viosrp_iu *iu = iue->iu;

	if (!test_bit(V_PARSED, &iue->req.flags))
		iue->req.vd = find_vscsis_vdev(iue);

	if ((iue->req.vd == NULL) &&
	    (iu->srp.cmd.cdb[0] != REPORT_LUNS) &&
	    (iu->srp.cmd.cdb[0] != INQUIRY)) {
		dbg("Cmd %2.2x for unknown LUN %16.16lx\n",
		    iu->srp.cmd.cdb[0], iue->iu->srp.cmd.lun);
		send_rsp(iue, ABORTED_COMMAND, 0x14);
		return FREE_IU;
	}

	if (getlink(iue))
		__set_bit(V_LINKED, &iue->req.flags);

	switch (iu->srp.cmd.task_attribute) {
	case SRP_ORDERED_TASK:
		__set_bit(V_BARRIER, &iue->req.flags);
	case SRP_SIMPLE_TASK:
		break;
	default:
		__set_bit(V_BARRIER, &iue->req.flags);
		warn("Task attribute %d not supported, assuming barrier\n",
		     iu->srp.cmd.task_attribute);
	}

	if (!iue->req.vd || !iue->req.vd->direct_scsi)
		return process_cmd_block(iue);
	else
		return process_cmd_scsi(iue);
}

/*
 * Respond to the adapter_info request
 */
u16 send_adapter_info(struct iu_entry *iue,
		      dma_addr_t remote_buffer, u16 length)
{
	dma_addr_t data_token;
	struct mad_adapter_info_data *info = 
	    dma_alloc_coherent(iue->adapter->dev, sizeof(*info), &data_token, 
			       GFP_KERNEL);

	dbg("in send_adapter_info\n ");
	if (info != NULL) {
		int rc;

		/* Get remote info */
		rc = h_copy_rdma(sizeof(*info),
				 iue->adapter->riobn,
				 remote_buffer,
				 iue->adapter->liobn,
				 data_token);
		if (rc == H_Success) {
			info("Client connect: %s (%d)\n",
			     info->partition_name,
			     info->partition_number);
		}

		memset(info, 0, sizeof(*info));

		dbg("building adapter_info\n ");
		strcpy(info->srp_version, "16.a");
		strncpy(info->partition_name, partition_name,
			sizeof(info->partition_name));
		info->partition_number = partition_number;
		info->mad_version = 1;
		info->os_type = 2;
		info->port_max_txu[0] = iue->adapter->max_sectors << 9;

		/* Send our info to remote */
		rc = h_copy_rdma(sizeof(*info),
				 iue->adapter->liobn,
				 data_token,
				 iue->adapter->riobn,
				 remote_buffer);

		dma_free_coherent(iue->adapter->dev,
				  sizeof(*info), info, data_token);

		if (rc != H_Success) {
			err("Error sending adapter info rc %d\n",rc);
			return 1;
		}
	} else {
		dbg("bad dma_alloc_coherent in adapter_info\n ");
		return 1;
	}
	return 0;

}

/*
 * Process our queue of incoming commands
 */
static void run_cmd_queue(struct server_adapter *adapter)
{
	struct iu_entry *curr_iue;
	struct list_head *next = NULL;
	unsigned long flags;
	spin_lock_irqsave(&adapter->lock, flags);

	next = list_empty(&adapter->cmd_queue) ? NULL : adapter->cmd_queue.next;
	while (next) {
		curr_iue = list_entry(next, struct iu_entry, next);
		next = next->next == &adapter->cmd_queue ? NULL : next->next;
		if (test_bit(V_FLYING, &curr_iue->req.flags)) {
			if (test_bit(V_DONE, &curr_iue->req.flags)) {
				list_del(&curr_iue->next);
				free_iu(curr_iue);
			}
			continue;
		}
		if (test_bit(V_ABORTED, &curr_iue->req.flags)) {
			adapter->next_rsp_delta++;
			list_del(&curr_iue->next);
			free_iu(curr_iue);
		} else {
			int rc;
			__set_bit(V_FLYING, &curr_iue->req.flags);
			spin_unlock_irqrestore(&adapter->lock, flags);
			dbg("process_cmd sending %p\n", curr_iue);
			rc = process_cmd(curr_iue);
			spin_lock_irqsave(&adapter->lock, flags);

			/* if the iue is not in any list, we're racing with
			   endio, so we lost the cmd_queue */
			if (curr_iue->next.next == LIST_POISON1)
				goto out;

			next = curr_iue->next.next == &adapter->cmd_queue
				? NULL : curr_iue->next.next;

			switch (rc) {
			case FREE_IU:
				list_del(&curr_iue->next);
				free_iu(curr_iue);
				break;
			case INFLIGHT:
				if (!test_bit(V_IN_USE, &curr_iue->req.flags))
					/* this means that the request finished
					before process_cmd() returned, so we
					lost a handle of the cmd_queue list */
					goto out;
				break;
			case RETRY_SPLIT_BUF:
				if (!split_iu(curr_iue)) {
					list_add(&curr_iue->req.child[1]->next,
						 &curr_iue->next);
					list_add(&curr_iue->req.child[0]->next,
						 &curr_iue->next);
					next = curr_iue->next.next;
					break;
				}
			case RETRY:
				__clear_bit(V_FLYING, &curr_iue->req.flags);
				kblockd_schedule_work(&adapter->crq_task);

				/* if a barrier fails, we don't want anything
				new to go through, retry when new cmd arrives
				or when workqueue runs */
				if (test_bit(V_BARRIER, &curr_iue->req.flags))
					goto out;
				break;
			default:
				err("Invalid return code %i from process_cmd\n",
				    rc);
			}
		}
	}

out:
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/*
 * Process an incoming information unit.
 */
static void process_iu(struct viosrp_crq *crq, struct server_adapter *adapter)
{
	struct iu_entry *iue = get_iu(adapter);
	union viosrp_iu *iu;
	long rc;
	unsigned long flags;

	if (iue == NULL) {
		warn("Error getting IU from pool, other side exceeded limit\n");
		return;
	}

	iue->req.remote_token = crq->IU_data_ptr;
	iue->req.timeout= crq->timeout ? crq->timeout*HZ : DEFAULT_TIMEOUT;

	rc = h_copy_rdma(crq->IU_length,
			 iue->adapter->riobn,
			 iue->req.remote_token, adapter->liobn, iue->iu_token);

	iu = iue->iu;

	if (rc) {
		err("process_iu: Error %ld transferring data from client\n",
			rc);
	}

	if (crq->format == VIOSRP_MAD_FORMAT) {
		switch (iu->mad.empty_iu.common.type) {
		case VIOSRP_EMPTY_IU_TYPE:
			warn("Unsupported EMPTY MAD IU\n");
			break;
		case VIOSRP_ERROR_LOG_TYPE:
			warn("Unsupported ERROR LOG MAD IU\n");
			iu->mad.error_log.common.status = 1;
			send_iu(iue, sizeof(iu->mad.error_log),
				VIOSRP_MAD_FORMAT);
			break;
		case VIOSRP_ADAPTER_INFO_TYPE:
			iu->mad.adapter_info.common.status =
			    send_adapter_info(iue,
					      iu->mad.adapter_info.buffer,
					      iu->mad.adapter_info.common.
					      length);

			send_iu(iue, sizeof(iu->mad.adapter_info),
				VIOSRP_MAD_FORMAT);
			break;
		case VIOSRP_HOST_CONFIG_TYPE:
			iu->mad.host_config.common.status = 1;
			send_iu(iue, sizeof(iu->mad.host_config),
				VIOSRP_MAD_FORMAT);
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
			spin_lock_irqsave(&adapter->lock, flags);
			list_add_tail(&iue->next, &adapter->cmd_queue);
			spin_unlock_irqrestore(&adapter->lock, flags);
			run_cmd_queue(adapter);
			return;
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

	spin_lock_irqsave(&adapter->lock, flags);
	free_iu(iue);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/* ==============================================================
 * CRQ Processing Routines
 * ==============================================================
 */

/*
 * Handle a CRQ event
 */
static void handle_crq(struct viosrp_crq *crq, struct server_adapter *adapter)
{
	switch (crq->valid) {
	case 0xC0:		/* initialization */
		switch (crq->format) {
		case 0x01:
			h_send_crq(adapter->dma_dev->unit_address,
				   0xC002000000000000, 0);
			break;
		case 0x02:
			break;
		default:
			err("Client error: Unknwn msg format %d\n",
			    crq->format);
		}
		return;
	case 0xFF:		/* transport event */
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
				err("Client error: Unsupported msg format %d\n",
				    crq->format);
			}
		}
		break;
	default:
		err("Client error: unknown message type 0x%02x!?\n",
		    crq->valid);
		return;
	}

}

/*
 * Task to handle CRQs
 */
static void crq_task(void *data)
{
	struct server_adapter *adapter = (struct server_adapter *)data;
	struct viosrp_crq *crq;
	int done = 0;

	while (!done) {

		/* Loop through and process CRQs */
		while ((crq = crq_queue_next_crq(&adapter->queue)) != NULL) {
			atomic_inc(&adapter->crq_processed);
			handle_crq(crq, adapter);
			crq->valid = 0x00;
		}

		vio_enable_interrupts(adapter->dma_dev);
		if ((crq = crq_queue_next_crq(&adapter->queue)) != NULL) {
			vio_disable_interrupts(adapter->dma_dev);
			handle_crq(crq, adapter);
			crq->valid = 0x00;
		} else
			done = 1;
	}
	run_cmd_queue(adapter);
}

/*
 * Handle the interrupt that occurs when something is placed on our CRQ
 */
static irqreturn_t handle_interrupt(int irq, void *dev_instance,
				    struct pt_regs *regs)
{
	struct server_adapter *adapter = (struct server_adapter *)dev_instance;

	vio_disable_interrupts(adapter->dma_dev);

	atomic_inc(&adapter->interrupts);

	kblockd_schedule_work(&adapter->crq_task);

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

	queue->msgs = (struct viosrp_crq *)get_zeroed_page(GFP_KERNEL);
	if (!queue->msgs)
		goto malloc_failed;
	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	queue->msg_token = dma_map_single(adapter->dev, queue->msgs,
					  queue->size * sizeof(*queue->msgs),
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(queue->msg_token))
		goto map_failed;

	rc = h_reg_crq(adapter->dma_dev->unit_address, queue->msg_token,
		       PAGE_SIZE);

	/* If the adapter was left active for some reason (like kexec)
	 * try freeing and re-registering 
	 */
	if (rc == H_Resource) {
	    do {
		rc = h_free_crq(adapter->dma_dev->unit_address);
	    } while ((rc == H_Busy) || (H_isLongBusy(rc)));
	    rc = h_reg_crq(adapter->dma_dev->unit_address, queue->msg_token,
			   PAGE_SIZE);
	}

	if ((rc != H_Success) && (rc != 2)) {
		err("Error 0x%x opening virtual adapter\n", rc);
		goto reg_crq_failed;
	}

	if (request_irq
	    (adapter->dma_dev->irq, &handle_interrupt, SA_INTERRUPT,
	     "ibmvscsis", adapter) != 0)
		goto req_irq_failed;

	vio_enable_interrupts(adapter->dma_dev);

	h_send_crq(adapter->dma_dev->unit_address, 0xC001000000000000, 0);

	queue->cur = 0;
	queue->lock = SPIN_LOCK_UNLOCKED;

	return 0;

      req_irq_failed:
	do {
		rc = h_free_crq(adapter->dma_dev->unit_address);
	} while ((rc == H_Busy) || (H_isLongBusy(rc)));

      reg_crq_failed:
	dma_unmap_single(adapter->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
      map_failed:
	free_page((unsigned long)queue->msgs);
      malloc_failed:
	return -ENOMEM;
}

/*
 * Release the CRQ
 */
static void release_crq_queue(struct crq_queue *queue,
			      struct server_adapter *adapter)
{
	int rc;

	info("releasing adapter\n");
	free_irq(adapter->dma_dev->irq, adapter);
	do {
		rc = h_free_crq(adapter->dma_dev->unit_address);
	} while ((rc == H_Busy) || (H_isLongBusy(rc)));
	dma_unmap_single(adapter->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
	free_page((unsigned long)queue->msgs);
}

/* ==============================================================
 * Shared Device Management
 * ==============================================================
 */
/*
 * Add a block device as a SCSI LUN
 */
static int activate_device(struct vdev *vdev)
{
	struct block_device *bdev;
	char *name = vdev->device_name;
	int ro = vdev->b.ro;
	unsigned long flags;
	struct scsi_dev_node *tmp_sdn;

	bdev = open_bdev_excl(name, ro, activate_device);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);;

	spin_lock_irqsave(&sdev_list_lock, flags);
	list_for_each_entry(tmp_sdn, &scsi_dev_list, node) {
		struct scsi_device *sdev = tmp_sdn->sdev;
		/* if the block device is a known scsi_device and
		   device is not a partition */
		if (sdev->request_queue == bdev->bd_disk->queue &&
		    bdev == bdev->bd_contains) {
			vdev->s.sdev = sdev;
			tmp_sdn->vdev = vdev;
			spin_unlock_irqrestore(&sdev_list_lock, flags);
			close_bdev_excl(bdev);
			vdev->direct_scsi = (char)1;
			vdev->disabled = 0;
			info("Activating %s (scsi %d:%d:%d:%d) as LUN 0x%lx\n",
			     name, sdev->host->host_no, sdev->channel,
			     sdev->id, sdev->lun, vdev->lun);
			return 0;
		}
	}
	spin_unlock_irqrestore(&sdev_list_lock, flags);

	vdev->direct_scsi = 0;
	vdev->b.bdev = bdev;
	vdev->disabled = 0;
	vdev->b.sectsize = bdev_hardsect_size(bdev);
	vdev->b.blocksize = bdev->bd_block_size;
	if (bdev->bd_disk->flags & GENHD_FL_CD)
		vdev->b.scsi_type = TYPE_ROM; /* CD/DVD */
	else
		vdev->b.scsi_type = TYPE_DISK; /* disk */

	if (bdev->bd_disk->flags & GENHD_FL_REMOVABLE) {
		vdev->b.removable = 1; /* rmb bit of inquiry */
		vdev->b.changed = 1;
	} else
		vdev->b.removable = 0;

	info("Activating block device %s as %s %s LUN 0x%lx sector size %ld\n",
	     name, ro ? "read only " : "",
	     vdev->b.scsi_type ? "CD" : "disk", vdev->lun,
	     vdev->b.sectsize);

	return 0;
}

static void deactivate_scsi_device(struct vdev *vdev)
{
	struct scsi_dev_node *tmp_sdn;

	vdev->disabled = 1;
	vdev->s.sdev = NULL;

	list_for_each_entry(tmp_sdn, &scsi_dev_list, node)
		if (tmp_sdn->vdev == vdev)
			tmp_sdn->vdev = NULL;
}

static void deactivate_device(struct vdev *vdev)
{
	info("Deactivating block device, LUN 0x%lx\n", vdev->lun);

	/* Wait while any users of this device finish.  Note there should
	 * be no new users, since we have marked this disabled
	 *
	 * We just poll here, since we are blocking write
	 */
	while (atomic_read(&vdev->refcount)) {
		msleep(REFCOUNT_TIMEOUT_MS);
	}

	vdev->disabled = 1;

	if (!vdev->direct_scsi)
		close_bdev_excl(vdev->b.bdev);
	else
		deactivate_scsi_device(vdev);
}

/*
 * Callback when a scsi_device gets added to the system
 */
static int add_scsi_device(struct class_device *cdev, 
			   struct class_interface *cl_intf)
{
	struct scsi_device *sdev = to_scsi_device(cdev->dev);
	struct scsi_dev_node * sdevnode =
			kmalloc(sizeof(struct scsi_dev_node), GFP_ATOMIC);
	unsigned long flags;

	dbg("add_scsi_device got %p, %d:%d:%d:%d, sdn=%p\n", sdev,
	    sdev->host->host_no, sdev->channel, sdev->id, sdev->lun, sdevnode);

	sdevnode->sdev = sdev;
	sdevnode->vdev = NULL;

	spin_lock_irqsave(&sdev_list_lock, flags);
	list_add_tail(&sdevnode->node, &scsi_dev_list);
	spin_unlock_irqrestore(&sdev_list_lock, flags);
	return 0;
}

/*
 * Callback when a scsi_device gets removed from the system
 */
static void rem_scsi_device(struct class_device *cdev, 
			    struct class_interface *cl_intf)
{
	struct scsi_dev_node *tmp_sdn;
	struct scsi_device *sdev = to_scsi_device(cdev->dev);
	unsigned long flags;

	spin_lock_irqsave(&sdev_list_lock, flags);
	list_for_each_entry(tmp_sdn, &scsi_dev_list, node) {
		if (sdev == tmp_sdn->sdev) {
			if (tmp_sdn->vdev && !tmp_sdn->vdev->disabled)
				deactivate_scsi_device(tmp_sdn->vdev);
			list_del(&tmp_sdn->node);
			kfree(tmp_sdn);
			goto out;
		}
	}

	warn("rem_scsi_device: Couldn't find scsi_device %p %d:%d:%d:%d\n",
		sdev, sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
out:	spin_unlock_irqrestore(&sdev_list_lock, flags);
	return;
}

/* ==============================================================
 * SYSFS Routines
 * ==============================================================
 */
static struct class_interface vscsis_interface = {
	.add = add_scsi_device,
	.remove = rem_scsi_device,
};

static struct kobj_type ktype_vscsi_target;
static struct kobj_type ktype_vscsi_bus;
static struct kobj_type ktype_vscsi_stats;

static void vscsi_target_release(struct kobject *kobj) {
	struct vdev *tmpdev =
		container_of(kobj,struct vdev,kobj);
	kfree(tmpdev);
}

static void vscsi_bus_release(struct kobject *kobj) {
	struct vbus *tmpbus =
		container_of(kobj,struct vbus,kobj);
	kfree(tmpbus);
}

static void set_num_targets(struct vbus* vbus, long value)
{
	struct device *dev =
		container_of(vbus->kobj.parent, struct device , kobj);
	struct server_adapter *adapter =
				(struct server_adapter *)dev->driver_data;
	int cur_num_targets = atomic_read(&vbus->num_targets);
	unsigned long flags;
	struct vdev *tmpdev;

	/* Growing */
	if (cur_num_targets < value) {
		int i;
		for (i = cur_num_targets; i < value; i++) {
			tmpdev = (struct vdev *)kmalloc(sizeof(struct vdev),
							GFP_KERNEL);
			if (!tmpdev) {
				err("Couldn't allocate target memory %d\n", i);
				return;
			}
			memset(tmpdev, 0, sizeof(struct vdev));

			tmpdev->lun = make_lun(vbus->bus_num, i, 0);
			tmpdev->b.blocksize = PAGE_CACHE_SIZE;
			tmpdev->b.sectsize = 512;
			tmpdev->disabled = 1;

			tmpdev->kobj.parent = &vbus->kobj;
			sprintf(tmpdev->kobj.name, "target%d", i);
			tmpdev->kobj.ktype = &ktype_vscsi_target;
			kobject_register(&tmpdev->kobj);

			spin_lock_irqsave(&adapter->lock, flags);
			if (vbus->vdev[i]) {
				/* Race!!! */
				spin_unlock_irqrestore(&adapter->lock, flags);
				kobject_unregister(&tmpdev->kobj);
				continue;
			}

			adapter->nvdevs++;
			atomic_inc(&vbus->num_targets);
			vbus->vdev[i] = tmpdev;
			spin_unlock_irqrestore(&adapter->lock, flags);
		}
	} else { /* shrinking */
		int i;
		for (i = cur_num_targets - 1; i >= value; i--)
		{
			if (!vbus->vdev[i]->disabled) {
				err("Can't remove active target %d\n", i);
				return;
			}

			spin_lock_irqsave(&adapter->lock, flags);
			tmpdev = vbus->vdev[i];
			vbus->vdev[i] = NULL;
			spin_unlock_irqrestore(&adapter->lock, flags);

			if (tmpdev)
				kobject_unregister(&tmpdev->kobj);

			adapter->nvdevs--;
			atomic_dec(&vbus->num_targets);
		}
	}
}

static void set_num_buses(struct device *dev, long value)
{
	struct server_adapter *adapter =
				(struct server_adapter *)dev->driver_data;
	int cur_num_buses = atomic_read(&adapter->num_buses);
	unsigned long flags;
	struct vbus *tmpbus;

	if (cur_num_buses < value) { /* growing */
		int i;
		for (i = cur_num_buses; i < value; i++) {
			tmpbus = (struct vbus *) kmalloc(sizeof(struct vbus),
							 GFP_KERNEL);
			if (!tmpbus) {
				err("Couldn't allocate bus %d memory\n", i);
				return;
			}

			memset(tmpbus, 0, sizeof(struct vbus));
			tmpbus->bus_num = i;
			tmpbus->kobj.parent = &dev->kobj;
			sprintf(tmpbus->kobj.name, "bus%d", i);
			tmpbus->kobj.ktype = &ktype_vscsi_bus;
			kobject_register(&tmpbus->kobj);

			spin_lock_irqsave(&adapter->lock, flags);

			if (adapter->vbus[i] != NULL) {
				/* Race condition! */
				spin_unlock_irqrestore(&adapter->lock, flags);
				kobject_unregister(&tmpbus->kobj);
				continue;
			}

			adapter->vbus[i] = tmpbus;

			atomic_inc(&adapter->num_buses);
			spin_unlock_irqrestore(&adapter->lock, flags);

			set_num_targets(adapter->vbus[i], 1);
		}

	} else if (cur_num_buses > value) { /* shrinking */
		int i, j, active_target;
		for (i = cur_num_buses - 1; i >= value; i--) {
			active_target = -1;
			for (j = 0; j < TARGETS_PER_BUS; j++) {
				if (adapter->vbus[i]->vdev[j] &&
				    !adapter->vbus[i]->vdev[j]->disabled) {
					active_target = j;
					break;
				}
			}
			if (active_target != -1) {
				err("Can't remove bus%d, target%d active\n",
					i, active_target);
				return ;
			}

			set_num_targets(adapter->vbus[i], 0);

			spin_lock_irqsave(&adapter->lock, flags);
			atomic_dec(&adapter->num_buses);
			tmpbus = adapter->vbus[i];
			adapter->vbus[i] = NULL;
			spin_unlock_irqrestore(&adapter->lock, flags);

			/* If we race this could already be NULL */
			if (tmpbus)
				kobject_unregister(&tmpbus->kobj);
		}
	}
}

/* Target sysfs stuff */
static ATTR(target, device, 0644);
static ATTR(target, active, 0644);
static ATTR(target, ro, 0644);

static ssize_t vscsi_target_show(struct kobject * kobj,
				 struct attribute * attr, char * buf)
{
	struct vdev *vdev = container_of(kobj, struct vdev, kobj);
	struct device *dev = container_of(kobj->parent->parent,
					  struct device, kobj);
	struct server_adapter *adapter =
				(struct server_adapter *)dev->driver_data;
	unsigned long flags;
	ssize_t returned;

	spin_lock_irqsave(&adapter->lock, flags);

	if (attr == &vscsi_target_device_attr)
		returned = sprintf(buf, "%s\n", vdev->device_name);
	else if (attr == &vscsi_target_active_attr)
		returned = sprintf(buf, "%d\n", !vdev->disabled);
	else if (attr == &vscsi_target_ro_attr)
		returned = sprintf(buf, "%d\n", vdev->b.ro);
	else {
		returned = -EFAULT;
		BUG();
	}

	spin_unlock_irqrestore(&adapter->lock, flags);

	return returned;
}

static ssize_t vscsi_target_store(struct kobject * kobj,
				  struct attribute * attr,
				  const char * buf, size_t count)
{
	struct vdev *vdev = container_of(kobj, struct vdev, kobj);
	struct device *dev = container_of(kobj->parent->parent,
					  struct device, kobj);
	struct server_adapter *adapter =
				(struct server_adapter *)dev->driver_data;
	long flags;
	long value = simple_strtol(buf, NULL, 10);

	if (attr != &vscsi_target_active_attr && !vdev->disabled) {
		err("Error: Can't modify properties while target is active.\n");
		return -EPERM;
	}

	if (attr == &vscsi_target_device_attr) {
		int i;
		spin_lock_irqsave(&adapter->lock, flags);
		i  = strlcpy(vdev->device_name, buf, TARGET_MAX_NAME_LEN);
		for (; i >= 0; i--)
			if (vdev->device_name[i] == '\n')
				vdev->device_name[i] = '\0';
		spin_unlock_irqrestore(&adapter->lock, flags);
	} else if (attr == &vscsi_target_active_attr) {
		if (value) {
			int rc;
			if (!vdev->disabled) {
				warn("Warning: Target was already active\n");
				return -EINVAL;
			}
			rc = activate_device(vdev);
			if (rc) {
				err("Error opening device=%d\n", rc);
				return rc;
			}
		} else {
			if (!vdev->disabled)
				deactivate_device(vdev);
		}
	} else if (attr == &vscsi_target_ro_attr)
		vdev->b.ro = value > 0 ? 1 : 0;
	else
		BUG();

	return count;
}

static struct attribute * vscsi_target_attrs[] = {
	&vscsi_target_device_attr,
	&vscsi_target_active_attr,
	&vscsi_target_ro_attr,
	NULL,
};

static struct sysfs_ops vscsi_target_ops = {
	.show	= vscsi_target_show,
	.store	= vscsi_target_store,
};

static struct kobj_type ktype_vscsi_target = {
	.release	= vscsi_target_release,
	.sysfs_ops	= &vscsi_target_ops,
	.default_attrs	= vscsi_target_attrs,
};

/* Bus sysfs stuff */
static ssize_t vscsi_bus_show(struct kobject * kobj,
			      struct attribute * attr, char * buf)
{
	struct vbus *vbus = container_of(kobj, struct vbus, kobj);
	return sprintf(buf, "%d\n", atomic_read(&vbus->num_targets));
}

static ssize_t vscsi_bus_store(struct kobject * kobj, struct attribute * attr,
const char * buf, size_t count)
{
	struct vbus *vbus = container_of(kobj, struct vbus, kobj);
	long value = simple_strtol(buf, NULL, 10);

	if (value < 0 || value > TARGETS_PER_BUS)
		return -EINVAL;

	set_num_targets(vbus, value);

	return count;
}

static ATTR(bus, num_targets, 0644);

static struct attribute * vscsi_bus_attrs[] = {
	&vscsi_bus_num_targets_attr,
	NULL,
};

static struct sysfs_ops vscsi_bus_ops = {
	.show	= vscsi_bus_show,
	.store	= vscsi_bus_store,
};

static struct kobj_type ktype_vscsi_bus = {
	.release	= vscsi_bus_release,
	.sysfs_ops	= &vscsi_bus_ops,
	.default_attrs	= vscsi_bus_attrs,
};

/* Device attributes */
static ssize_t vscsi_dev_bus_show(struct device * dev,
				  struct device_attribute *attr,
				  char * buf)
{
	struct server_adapter *adapter =
				(struct server_adapter *)dev->driver_data;

	return sprintf(buf, "%d\n", atomic_read(&adapter->num_buses));
}

static ssize_t vscsi_dev_sector_show(struct device * dev,
				     struct device_attribute *attr,
				     char * buf)
{
	struct server_adapter *adapter =
				(struct server_adapter *)dev->driver_data;

	return sprintf(buf, "%d\n", adapter->max_sectors);
}

static ssize_t vscsi_dev_bus_store(struct device * dev,
				   struct device_attribute *attr,
				   const char * buf, size_t count)
{
	long value = simple_strtol(buf, NULL, 10);

	if (value < 0 || value > BUS_PER_ADAPTER)
		return -EINVAL;

	set_num_buses(dev, value);
	return count;
}

static ssize_t vscsi_dev_sector_store(struct device * dev,
				      struct device_attribute *attr,
				      const char * buf, size_t count)
{
	long value = simple_strtol(buf, NULL, 10);
	struct server_adapter *adapter =
				(struct server_adapter *)dev->driver_data;

	if (value <= 8 || value > SCSI_DEFAULT_MAX_SECTORS)
		return -EINVAL;

	adapter->max_sectors = value;

	return count;
}

static ssize_t vscsi_dev_debug_store(struct device * dev,
				     struct device_attribute *attr,
				     const char * buf, size_t count)
{
	long value = simple_strtol(buf, NULL, 10);

	ibmvscsis_debug = value;
	return count;
}

static ssize_t vscsi_dev_debug_show(struct device * dev,
				    struct device_attribute *attr,
				    char * buf)
{
	return sprintf(buf, "%d\n", ibmvscsis_debug);
}

static DEVICE_ATTR(debug, 0644, vscsi_dev_debug_show, vscsi_dev_debug_store);
static DEVICE_ATTR(num_buses, 0644, vscsi_dev_bus_show, vscsi_dev_bus_store);
static DEVICE_ATTR(max_sectors, 0644, vscsi_dev_sector_show,
		   vscsi_dev_sector_store);

/* Stats kobj stuff */

static ATTR(stats, interrupts, 0444);
static ATTR(stats, read_ops, 0444);
static ATTR(stats, write_ops, 0444);
static ATTR(stats, crq_msgs, 0444);
static ATTR(stats, iu_allocs, 0444);
static ATTR(stats, bio_allocs, 0444);
static ATTR(stats, buf_allocs, 0444);
static ATTR(stats, errors, 0444);

static struct attribute * vscsi_stats_attrs[] = {
	&vscsi_stats_interrupts_attr,
	&vscsi_stats_read_ops_attr,
	&vscsi_stats_write_ops_attr,
	&vscsi_stats_crq_msgs_attr,
	&vscsi_stats_iu_allocs_attr,
	&vscsi_stats_bio_allocs_attr,
	&vscsi_stats_buf_allocs_attr,
	&vscsi_stats_errors_attr,
	NULL,
};

static ssize_t vscsi_stats_show(struct kobject * kobj,
				struct attribute * attr, char * buf)
{
	struct server_adapter *adapter= container_of(kobj,
						     struct server_adapter,
						     stats_kobj);
	if (attr == &vscsi_stats_interrupts_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->interrupts));
	if (attr == &vscsi_stats_read_ops_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->read_processed));
	if (attr == &vscsi_stats_write_ops_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->write_processed));
	if (attr == &vscsi_stats_crq_msgs_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->crq_processed));
	if (attr == &vscsi_stats_iu_allocs_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->iu_count));
	if (attr == &vscsi_stats_bio_allocs_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->bio_count));
	if (attr == &vscsi_stats_buf_allocs_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->buffers_allocated));
	if (attr == &vscsi_stats_errors_attr)
		return sprintf(buf, "%d\n",
		 atomic_read(&adapter->errors));

	BUG();
	return 0;
}

static struct sysfs_ops vscsi_stats_ops = {
	.show	= vscsi_stats_show,
	.store	= NULL,
};

static struct kobj_type ktype_vscsi_stats = {
	.release	= NULL,
	.sysfs_ops	= &vscsi_stats_ops,
	.default_attrs	= vscsi_stats_attrs,
};

/* ==============================================================
 * Module load and unload
 * ==============================================================
 */
static int ibmvscsis_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	struct server_adapter *adapter;
	int rc;
	unsigned int *dma_window;
	unsigned int dma_window_property_size;

	adapter = kmalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		err("couldn't allocate adapter memory\n");
		return -ENOMEM;
	}
	memset(adapter, 0, sizeof(*adapter));
	adapter->dma_dev = dev;
	adapter->dev = &dev->dev;
	adapter->dev->driver_data = adapter;
	adapter->next_rsp_delta = 0;
	adapter->lock = SPIN_LOCK_UNLOCKED;

	dma_window =
	    (unsigned int *)vio_get_attribute(dev, "ibm,my-dma-window",
					      &dma_window_property_size);
	if ((!dma_window) || (dma_window_property_size != 40)) {
		err("Couldn't get ibm,my-dma-window property\n");
		return -EIO;
	}

	adapter->liobn = dma_window[0];
	adapter->riobn = dma_window[5];

	INIT_WORK(&adapter->crq_task, crq_task, adapter);

	tasklet_init(&adapter->endio_tasklet,
		     endio_task, (unsigned long)adapter);

	INIT_LIST_HEAD(&adapter->cmd_queue);

	/* Initialize the buffer cache */
	init_data_buffer(adapter);

	/* Arbitrarily support 16 IUs right now */
	rc = initialize_iu_pool(adapter, INITIAL_SRP_LIMIT);
	if (rc) {
		kfree(adapter);
		return rc;
	}

	rc = initialize_crq_queue(&adapter->queue, adapter);
	if (rc != 0) {
		kfree(adapter);
		return rc;
	}

	set_num_buses(&dev->dev, 1);
	adapter->max_sectors = DEFAULT_MAX_SECTORS;
	device_create_file(&dev->dev, &dev_attr_debug);
	device_create_file(&dev->dev, &dev_attr_num_buses);
	device_create_file(&dev->dev, &dev_attr_max_sectors);

	adapter->stats_kobj.parent = &dev->dev.kobj;
	strcpy(adapter->stats_kobj.name, "stats");
	adapter->stats_kobj.ktype = & ktype_vscsi_stats;
	kobject_register(&adapter->stats_kobj);

	return 0;
}

static int ibmvscsis_remove(struct vio_dev *dev)
{
	int bus;
	int target;
	unsigned long flags;
	struct server_adapter *adapter =
	    (struct server_adapter *)dev->dev.driver_data;

	spin_lock_irqsave(&adapter->lock, flags);

	/*
	 * Loop through the bus
	 */
	for (bus = 0; bus < BUS_PER_ADAPTER; bus++) {
		/* If this bus exists */
		if (adapter->vbus[bus]) {
			/* loop through the targets */
			for (target = 0; target < TARGETS_PER_BUS; target++) {
				/* If the target exists */
				struct vdev *vdev =
					       adapter->vbus[bus]->vdev[target];
				if (vdev && !vdev ->disabled)
					deactivate_device(vdev);
			}
			spin_unlock_irqrestore(&adapter->lock, flags);
			set_num_targets(adapter->vbus[bus], 0);
			spin_lock_irqsave(&adapter->lock, flags);
		}
	}

	spin_unlock_irqrestore(&adapter->lock, flags);
	set_num_buses(adapter->dev, 0);
	release_crq_queue(&adapter->queue, adapter);

	release_iu_pool(adapter);

	release_data_buffer(adapter);

	kobject_unregister(&adapter->stats_kobj);
	device_remove_file(&dev->dev, &dev_attr_debug);
	device_remove_file(&dev->dev, &dev_attr_num_buses);
	device_remove_file(&dev->dev, &dev_attr_max_sectors);

	kfree(adapter);

	return 0;
}

static struct vio_device_id ibmvscsis_device_table[] __devinitdata = {
	{"v-scsi-host", "IBM,v-scsi-host"},
	{"",""}
};

MODULE_DEVICE_TABLE(vio, ibmvscsis_device_table);

static struct vio_driver ibmvscsis_driver = {
	.id_table = ibmvscsis_device_table,
	.probe = ibmvscsis_probe,
	.remove = ibmvscsis_remove,
        .driver = {
		.name = "ibmvscsis",
		.owner = THIS_MODULE,
        },
};

static int mod_init(void)
{
	struct device_node *rootdn;
	char *ppartition_name;
	char *psystem_id;
	char *pmodel;
	unsigned int *p_number_ptr;
	int rc;

	/* Retrieve information about this partition */
	rootdn = find_path_device("/");
	if (rootdn) {
		pmodel = get_property(rootdn, "model", NULL);
		psystem_id = get_property(rootdn, "system-id", NULL);
		if (pmodel && psystem_id)
			snprintf(system_id,sizeof(system_id),
				 "%s-%s",
				 pmodel, psystem_id);
		ppartition_name =
			get_property(rootdn, "ibm,partition-name", NULL);
		if (ppartition_name)
			strncpy(partition_name, ppartition_name,
				sizeof(partition_name));
		p_number_ptr =
			(unsigned int *)get_property(rootdn, "ibm,partition-no",
						     NULL);
		if (p_number_ptr)
			partition_number = *p_number_ptr;
	}

	info("initialized version "IBMVSCSIS_VERSION"\n");

	rc = vio_register_driver(&ibmvscsis_driver);

	if (rc) {
		warn("rc %d from vio_register_driver\n", rc);
		return rc;
	}

	rc = scsi_register_interface(&vscsis_interface);

	if (rc)
		warn("rc %d from scsi_register_interface\n", rc);

	return rc;
}

static void mod_exit(void)
{
	info("terminated\n");

	scsi_unregister_interface(&vscsis_interface);
	vio_unregister_driver(&ibmvscsis_driver);
}

module_init(mod_init);
module_exit(mod_exit);

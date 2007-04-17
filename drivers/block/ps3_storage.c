/*
 * Copyright (C) 2006 Sony Computer Entertainment Inc.
 * Copyright 2006, 2007 Sony Corporation
 * storage support for PS3
 *
 * based on scsi_debug.c
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

#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/rwsem.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <linux/cdrom.h>
#include <asm/lv1call.h>
#include <asm/abs_addr.h>
#include <asm/ps3.h>
#include "ps3_storage.h"

#undef _DEBUG
#if defined(_DEBUG)
#define DPRINTK(x...) printk(x)
#else
#define DPRINTK(x...) do {} while(0)
#endif

#undef _DEBUG_CALLTREE
#if defined(_DEBUG_CALLTREE)
static int func_level;
#define FUNC_START     printk(KERN_ERR "%s:%d start\n", __FUNCTION__, func_level++)
#define FUNC_STEP_C(x) printk(KERN_ERR "%s:%d step %s\n", __FUNCTION__, func_level, x)
#define FUNC_END       printk(KERN_ERR "%s:%d end\n", __FUNCTION__, --func_level)
#define FUNC_END_C(x)  printk(KERN_ERR "%s:%d end %s\n", __FUNCTION__, --func_level, x)
#else
#define FUNC_START     do {} while(0)
#define FUNC_END       FUNC_START
#define FUNC_STEP_C(x) FUNC_START
#define FUNC_END_C(x)  FUNC_START
#endif

#define FLASH_ALIGN    (0x00040000) /* flash safe write size (256KB); should be powers of 2 */

static int ps3_stor_add_host = 2;
static int ps3_stor_wait_time = CONFIG_PS3_STORAGE_MAX_SPINUP_WAIT_TIME;
static int ps3_stor_wait_num_storages = CONFIG_PS3_STORAGE_EXPECTED_NUM_DRIVES + 1;

#define CEIL_ALIGN_16M(mem)  ((((mem - 1) >> 24) + 1 ) << 24) /* 2^24=16M */
#define CEIL_ALIGN_1M(mem)   ((((mem - 1) >> 20) + 1 ) << 20) /* 2^20=1M */
#define CEIL_ALIGN_64K(mem)  ((((mem - 1) >> 16) + 1 ) << 16) /* 2^16=64K */
#define CEIL_ALIGN_4K(mem)   ((((mem - 1) >> 12) + 1 ) << 12) /* 2^12=4K */

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PS3 storage driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(PS3_STOR_VERSION);

module_param_named(wait_num_storages, ps3_stor_wait_num_storages, int, 0);
module_param_named(wait_time, ps3_stor_wait_time, int, 0);
MODULE_PARM_DESC(wait_num_storages, "Number of expected (wanted) drives to wait spin up (default=3 drives)");
MODULE_PARM_DESC(wait_time, "Maximum time to wait spinup (default=10sec)");

static struct ps3_stor_lv1_bus_info ps3_stor_lv1_bus_info_array[PS3_STORAGE_NUM_OF_BUS_TYPES];

static struct ps3_stor_lv1_dev_info * ps3_stor_lv1_dev_info_array;
static int ps3_stor_lv1_devnum ; /* number of configured(used) lv1 devices */

static LIST_HEAD(ps3_stor_host_list);
static DEFINE_SPINLOCK(ps3_stor_host_list_lock);

static u64 ps3_stor_virtual_to_lpar(struct ps3_stor_dev_info *dev_info,
				    void *va);

/*
 * fill buf with MODE SENSE page 8 (caching parameter)
 * changable: 0 fills current value, otherwise fills 0
 * returns length of this page
 */
const static unsigned char page_data_6[] =
{
	0x06,    2, /* page 6, length =2                         */
	0x01,       /* 0: write cache disabled                   */
	0x00        /* reserved                                  */
};
const static unsigned char page_data_8[] =
{
	0x08,   10, /* page 8, length =10                        */
	0x04,       /* 0:read cache, 1:mult factor, 2:write cache*/
	0x00,       /* 0..3:write retantion, 4..7:read retantion */
	0xff, 0xff, /* disable prefech block length              */
	0x00, 0x00, /* minimum prefech                           */
	0xff, 0xff, /* maximum prefech                           */
	0xff, 0xff  /* maximum prefech ceiling                   */
};

/*
 * returns 0: decoded
 *        -1: not sense info, issue REQUEST_SENSE needed
 */
static int decode_lv1_status(u64 status, unsigned char * sense_key,
			     unsigned char * asc, unsigned char * ascq)
{
	if (((status >> 24) & 0xff) != 0x02)
		return -1;

	*sense_key = (status >> 16) & 0xff;
	*asc       = (status >>  8) & 0xff;
	*ascq      =  status        & 0xff;
	return 0;
}


static void ps3_stor_srb_done(struct ps3_stor_dev_info * dev_info)
{
	struct scsi_cmnd * srb = dev_info->srb;
	unsigned long flags;

	spin_lock_irqsave(&(dev_info->srb_lock), flags);
	{
		dev_info->srb = NULL;
		srb->scsi_done(srb);
	}
	spin_unlock_irqrestore(&(dev_info->srb_lock), flags);
}

static void ps3_stor_process_srb(struct scsi_cmnd * srb)
{
	struct ps3_stor_dev_info * dev_info;
	int (*command_handler)(struct ps3_stor_dev_info *, struct scsi_cmnd *);

	dev_info = (struct ps3_stor_dev_info*) srb->device->hostdata;
	command_handler = dev_info->handler_info[srb->cmnd[0]].cmnd_handler;

	if (command_handler) {
		(*command_handler)(dev_info, srb);
	} else {
		srb->result = (DID_ERROR << 16);
		memset(srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		srb->sense_buffer[0] = 0x70;
		srb->sense_buffer[2] = ILLEGAL_REQUEST;
		ps3_stor_srb_done(dev_info);
	}
}

/*
 * main thread to process srb.
 * thread is created per device basis.
 * srb are often passed in interrupt context (softirq), so
 * we can't sleep at queuecommand().  just receive it
 * at queucommand(), then passed it to other thread
 * to process it under non-interrupt context.
 */
static int ps3_stor_main_thread(void * parm)
{
	struct ps3_stor_dev_info * dev_info = (struct ps3_stor_dev_info *)parm;
	int reason = 0;

	current->flags |= PF_NOFREEZE; /* jugemu jugemu */

	while (!reason) {
		down_interruptible(&(dev_info->thread_sema));
		switch (dev_info->thread_wakeup_reason) {
		case SRB_QUEUED:
			ps3_stor_process_srb(dev_info->srb);
			break;
		case THREAD_TERMINATE:
			reason =  THREAD_TERMINATE;
			break;
		default:
			printk(KERN_ERR "%s: unknown wakeup reason %d\n", __FUNCTION__,
			       dev_info->thread_wakeup_reason);
			break;
		}
	}

	complete_and_exit(&(dev_info->thread_terminated), reason);
}

/*
 * copy data from device into scatter/gather buffer
 */
static int fill_from_dev_buffer(struct scsi_cmnd * srb,
				const unsigned char * arr,
				int arr_len)
{
	int k, req_len, act_len, len, active;
	void * kaddr;
	void * kaddr_off;
	struct scatterlist * sgpnt;

	if (0 == srb->request_bufflen)
		return 0;
	if (NULL == srb->request_buffer)
		return (DID_ERROR << 16);
	if (! ((srb->sc_data_direction == DMA_BIDIRECTIONAL) ||
	      (srb->sc_data_direction == DMA_FROM_DEVICE)))
		return (DID_ERROR << 16);
	if (0 == srb->use_sg) {
		req_len = srb->request_bufflen;
		act_len = (req_len < arr_len) ? req_len : arr_len;
		memcpy(srb->request_buffer, arr, act_len);
		srb->resid = req_len - act_len;
		return 0;
	}
	sgpnt = (struct scatterlist *)srb->request_buffer;
	active = 1;
	for (k = 0, req_len = 0, act_len = 0; k < srb->use_sg; ++k, ++sgpnt) {
		if (active) {
			kaddr = kmap_atomic(sgpnt->page, KM_USER0);
			if (NULL == kaddr)
				return (DID_ERROR << 16);
			kaddr_off = kaddr + sgpnt->offset;
			len = sgpnt->length;
			if ((req_len + len) > arr_len) {
				active = 0;
				len = arr_len - req_len;
			}
			memcpy(kaddr_off, arr + req_len, len);
			kunmap_atomic(kaddr, KM_USER0);
			act_len += len;
		}
		req_len += sgpnt->length;
	}
	srb->resid = req_len - act_len;
	return 0;
}

/*
 * copy data from scatter/gather into device's buffer
 */
static int fetch_to_dev_buffer(struct scsi_cmnd * srb,
			       unsigned char * arr,
			       int max_arr_len)
{
	int k, req_len, len, fin;
	void * kaddr;
	void * kaddr_off;
	struct scatterlist * sgpnt;

	if (0 == srb->request_bufflen)
		return 0;
	if (NULL == srb->request_buffer)
		return -1;
	if (! ((srb->sc_data_direction == DMA_BIDIRECTIONAL) ||
	      (srb->sc_data_direction == DMA_TO_DEVICE)))
		return -1;
	if (0 == srb->use_sg) {
		req_len = srb->request_bufflen;
		len = (req_len < max_arr_len) ? req_len : max_arr_len;
		memcpy(arr, srb->request_buffer, len);
		return len;
	}

	sgpnt = (struct scatterlist *)srb->request_buffer;
	for (k = 0, req_len = 0, fin = 0; k < srb->use_sg; ++k, ++sgpnt) {
		kaddr = kmap_atomic(sgpnt->page, KM_USER0);
		if (NULL == kaddr)
			return -1;
		kaddr_off = kaddr + sgpnt->offset;
		len = sgpnt->length;
		if ((req_len + len) > max_arr_len) {
			len = max_arr_len - req_len;
			fin = 1;
		}
		memcpy(arr + req_len, kaddr_off, len);
		kunmap_atomic(kaddr, KM_USER0);
		if (fin)
			return req_len + len;
		req_len += sgpnt->length;
	}
	return req_len;
}

/*
 * copy data into device buffer to write.
 * byte offset 'from' until byte offset 'to'
 * data always copied into 'arr'
 */
static off_t fetch_to_dev_buffer_abs(struct scsi_cmnd * srb,
				     unsigned char * arr,
				     off_t from,
				     off_t to)
{
	int i, fin;
	void * kaddr;
	void * kaddr_off;
	off_t cur_pos, end_pos, start_pos, len;
	struct scatterlist * sg;

	if (0 == srb->request_bufflen)
		return 0;
	if (NULL == srb->request_buffer)
		return -1;
	if (! ((srb->sc_data_direction == DMA_BIDIRECTIONAL) ||
	      (srb->sc_data_direction == DMA_TO_DEVICE)))
		return -1;
	if (to < from)
		return -1;

	DPRINTK(KERN_ERR "%s: from=%#lx(%ld) to=%#lx(%ld) sg=%d\n", __FUNCTION__,
		from, from , to, to, srb->use_sg);

	if (0 == srb->use_sg) {
		len = (srb->request_bufflen < to) ? (srb->request_bufflen - from) : (to - from);
		memcpy(arr, srb->request_buffer + from, len);
		return len;
	}


	len = 0;
	sg = (struct scatterlist *)srb->request_buffer;
	for (i = 0, cur_pos = 0, fin = 0;
	     (i < srb->use_sg) && !fin;
	     cur_pos += sg->length, i++, sg++) {
		kaddr = kmap_atomic(sg->page, KM_USER0);
		kaddr_off = kaddr + sg->offset;

		//DPRINTK(KERN_ERR "%s: cur_pos=%ld, sglen=%d kadoff=%p\n", __FUNCTION__,
		//cur_pos, sg->length, kaddr_off);
		if (NULL == kaddr)
			return -1;

		if (from <= cur_pos) {
			start_pos = cur_pos;
		}  else {
			if (from < (cur_pos + sg->length)) {
				/* copy start with middle of this segment */
				start_pos = from;
				kaddr_off += from - start_pos;
			} else {
				/* this segment does not have any desired data */
				kunmap_atomic(kaddr, KM_USER0);
				continue;
			}
		}

		if (to < (cur_pos + sg->length)) {
			/* copy end with middle of this segment */
			end_pos = to;
			fin = 1;
		} else {
			end_pos = cur_pos + sg->length;
		}

		if (start_pos < end_pos) {
			//DPRINTK(KERN_ERR "%s: COPY start=%ld end=%ld kaddoff=%p\n", __FUNCTION__,
			//start_pos, end_pos, kaddr_off);
			memcpy(arr + len, kaddr_off, end_pos - start_pos);
			len += end_pos - start_pos;
		}
		kunmap_atomic(kaddr, KM_USER0);

	}
	DPRINTK(KERN_ERR "%s: return ren=%ld\n", __FUNCTION__, len);
	return len;
}


/*
 * issue PACKET command according to passed SRB
 * caller will block until the command completed.
 * returns 0 if command sucessfully done,
 * otherwise error detected.
 * if auto_sense is on, request_sense is automatically issued and
 * return the sense data into srb->sensebuffer[SCSI_SENSE_BUFFERSIZE]
 * srb->result will be set.
 * caller should call done(srb) to inform mid layer the command completed.
 */
static int issue_atapi_by_srb(struct ps3_stor_dev_info * dev_info,
			      int auto_sense)
{
	struct ps3_stor_lv1_dev_info *lv1_dev_info = dev_info->lv1_dev_info;
	struct lv1_atapi_cmnd_block atapi_cmnd;
	const struct scsi_command_handler_info * handler_info;
	unsigned char * cmnd = dev_info->srb->cmnd;
	int bounce_len = 0;
	int error;
	unsigned char keys[4];

	handler_info = &(dev_info->handler_info[cmnd[0]]);

	/* check buffer size */
	switch (handler_info->buflen) {
	case USE_SRB_6:
		bounce_len = cmnd[4];
		break;
	case USE_SRB_10:
 		bounce_len = (cmnd[7] << 8) | cmnd[8];
		break;
	case USE_CDDA_FRAME_RAW:
		bounce_len = ((cmnd[6] << 16) |
		       (cmnd[7] <<  8) |
		       (cmnd[8] <<  0)) * CD_FRAMESIZE_RAW;
		break;
	default:
		bounce_len = handler_info->buflen;
	}

	if (dev_info->dedicated_bounce_size < bounce_len ) {
		static int printed;
		if (!printed++)
			printk(KERN_ERR "%s: data size too large %#x<%#x\n",
			       __FUNCTION__,
			       dev_info->dedicated_bounce_size,
			       bounce_len);
		dev_info->srb->result = DID_ERROR << 16;
		memset(dev_info->srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		dev_info->srb->sense_buffer[0] = 0x70;
		dev_info->srb->sense_buffer[2] = ILLEGAL_REQUEST;
		return -1;
	}

	memset(&atapi_cmnd, 0, sizeof(struct lv1_atapi_cmnd_block));
	memcpy(&(atapi_cmnd.pkt), cmnd, 12);
	atapi_cmnd.pktlen = 12;
	atapi_cmnd.proto = handler_info->proto;
	if (handler_info->in_out != DIR_NA)
		atapi_cmnd.in_out = handler_info->in_out;

	if (atapi_cmnd.in_out == DIR_WRITE)
		fetch_to_dev_buffer(dev_info->srb, dev_info->bounce_buf, bounce_len);

	atapi_cmnd.block_size = 1; /* transfer size is block_size * blocks */

	atapi_cmnd.blocks = atapi_cmnd.arglen = bounce_len;
	atapi_cmnd.buffer = ps3_stor_virtual_to_lpar(dev_info, dev_info->bounce_buf);

	/* issue command */
	init_completion(&(dev_info->irq_done));
	error = lv1_storage_send_device_command(lv1_dev_info->repo.did.dev_id,
						LV1_STORAGE_SEND_ATAPI_COMMAND,
						ps3_mm_phys_to_lpar(__pa(&atapi_cmnd)),
						sizeof(struct lv1_atapi_cmnd_block),
						atapi_cmnd.buffer,
						atapi_cmnd.arglen,
						&lv1_dev_info->current_tag);
	if (error) {
		printk(KERN_ERR "%s: send_device failed lv1dev=%u ret=%d\n",
		       __FUNCTION__, lv1_dev_info->repo.did.dev_id, error);
		dev_info->srb->result = DID_ERROR << 16; /* FIXME: is better other error code ? */
		return -1;
	}

	/* wait interrupt */
	wait_for_completion(&(dev_info->irq_done));

	/* check error */
	if (!dev_info->lv1_status) {
		/* OK, completed */
		if (atapi_cmnd.in_out == DIR_READ)
			fill_from_dev_buffer(dev_info->srb, dev_info->bounce_buf, bounce_len);
		dev_info->srb->result = DID_OK << 16;
		return 0;
	}

	/* error */
	if (!auto_sense) {
		dev_info->srb->result = (DID_ERROR << 16) | (CHECK_CONDITION << 1);
		printk(KERN_ERR "%s: end error withtout autosense\n", __FUNCTION__);
		return 1;
	}

	if (!decode_lv1_status(dev_info->lv1_status,
			       &(keys[0]), &(keys[1]), &(keys[2]))) {
		/* lv1 may have issued autosense ... */
		dev_info->srb->sense_buffer[0]  = 0x70;
		dev_info->srb->sense_buffer[2]  = keys[0];
		dev_info->srb->sense_buffer[7]  = 16 - 6;
		dev_info->srb->sense_buffer[12] = keys[1];
		dev_info->srb->sense_buffer[13] = keys[2];
		dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
	} else {
		/* do auto sense by our selves*/
		memset(&atapi_cmnd, 0, sizeof(struct lv1_atapi_cmnd_block));
		atapi_cmnd.pkt[0] = REQUEST_SENSE;
		atapi_cmnd.pkt[4] = 18;
		atapi_cmnd.pktlen = 12;
		atapi_cmnd.arglen = atapi_cmnd.blocks = atapi_cmnd.pkt[4];
		atapi_cmnd.block_size = 1;
		atapi_cmnd.proto = DMA_PROTO;
		atapi_cmnd.in_out = DIR_READ;
		atapi_cmnd.buffer = ps3_stor_virtual_to_lpar(dev_info,dev_info->bounce_buf);

		/* issue REQUEST_SENSE command */
		init_completion(&(dev_info->irq_done));
		error = lv1_storage_send_device_command(lv1_dev_info->repo.did.dev_id,
							LV1_STORAGE_SEND_ATAPI_COMMAND,
							ps3_mm_phys_to_lpar(__pa(&atapi_cmnd)),
							sizeof(struct lv1_atapi_cmnd_block),
							atapi_cmnd.buffer,
							atapi_cmnd.arglen,
							&lv1_dev_info->current_tag);
		if (error) {
			printk(KERN_ERR "%s: send_device for request sense failed lv1dev=%u ret=%d\n", __FUNCTION__,
			       lv1_dev_info->repo.did.dev_id, error);
			dev_info->srb->result = DID_ERROR << 16; /* FIXME: is better other error code ? */
			return -1;
		}

		/* wait interrupt */
		wait_for_completion(&(dev_info->irq_done));

		/* scsi spec says request sense should never get error */
		if (dev_info->lv1_status) {
			decode_lv1_status(dev_info->lv1_status,
					  &(keys[0]), &(keys[1]), &(keys[2]));
			printk(KERN_ERR "%s: auto REQUEST_SENSE error %#x %#x %#x\n", __FUNCTION__,
			       keys[0], keys[1], keys[2]);
		}

		memcpy(dev_info->srb->sense_buffer, dev_info->bounce_buf,
		       min((int)atapi_cmnd.pkt[4], SCSI_SENSE_BUFFERSIZE));
		dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
	}

	return 1;
}

/*
 * just send command with auto REQUEST_SENSE
 */
static int ps3_stor_atapi_handle_simple(struct ps3_stor_dev_info * dev_info,
				        struct scsi_cmnd * srb)
{
	int ret;

	ret = issue_atapi_by_srb(dev_info, 1);
	ps3_stor_srb_done(dev_info);
	return ret;
}

/*
 * just send command WITHOUT auto REQUEST_SENSE
 */
static int ps3_stor_atapi_handle_request_sense(struct ps3_stor_dev_info * dev_info, struct scsi_cmnd * srb)
{
	int ret;

	ret = issue_atapi_by_srb(dev_info, 0);
	ps3_stor_srb_done(dev_info);
	return ret;
}

/******************************************************
 * handlers for HDD
 */

static int ps3_stor_hdd_handle_inquiry(struct ps3_stor_dev_info * dev_info,
				       struct scsi_cmnd * srb)
{
	struct ps3_stor_lv1_dev_info *lv1_dev_info = dev_info->lv1_dev_info;
	unsigned char inquiry_reply[PS3_STOR_MAX_INQUIRY_DATA_SIZE];
	unsigned char *cmd = srb->cmnd;
	const char * msg;
	int alloc_len;
	int ret;

	FUNC_START;
	alloc_len = (cmd[3] << 8) + cmd[4];
	memset(inquiry_reply, 0, PS3_STOR_MAX_INQUIRY_DATA_SIZE);
	inquiry_reply[0] = lv1_dev_info->device_type;
	inquiry_reply[1] = 0;  /* Removable flag */
	inquiry_reply[2] = 2;  /* ANSI version */
	inquiry_reply[3] = 2;  /* response_data_format==2 */
	inquiry_reply[4] = PS3_STOR_INQUIRY_DATA_SIZE - 5;

	sprintf(&inquiry_reply[8], "%-8s", "SCEI");
	if (lv1_dev_info->device_type == PS3_DEV_TYPE_STOR_DISK) {
		switch (lv1_dev_info->attached_port) {
		case 0:
			msg = "Pri:Master";
			break;
		case 1:
			msg = "Pri:Slave";
			break;
		case 2:
			msg = "Sec:Master";
			break;
		case 3:
			msg = "Sec:Slave";
			break;
		default:
			msg = "Unknown";
			break;

		}
	} else {
		msg = "Flash";
	}

	/* SCSI spec requires model name left aligned, spece padded */
	ret = sprintf(&inquiry_reply[16], "%s-%d", msg,
		      lv1_dev_info->region_info_array[srb->cmnd[1]>>5].region_index);
	if (ret < 16)
		memset(&(inquiry_reply[16 + ret]), ' ', 16 - ret);

	sprintf(&inquiry_reply[32], "%-4d", 4989);

	inquiry_reply[58] = 0x0; inquiry_reply[59] = 0x40; /* SAM-2 */
	inquiry_reply[60] = 0x3; inquiry_reply[61] = 0x0;  /* SPC-3 */
	inquiry_reply[62] = 0x1; inquiry_reply[63] = 0x80; /* SBC */

	ret = fill_from_dev_buffer(dev_info->srb, inquiry_reply, min(alloc_len, PS3_STOR_INQUIRY_DATA_SIZE));

	srb->result = DID_OK << 16;
	ps3_stor_srb_done(dev_info);
	FUNC_END;
	return ret;
}


static int ps3_stor_hdd_handle_request_sense(struct ps3_stor_dev_info * dev_info, struct scsi_cmnd * srb)
{
	unsigned char sense_data[PS3_STOR_SENSE_LEN];
	int len = 18;

	memset(sense_data, 0, PS3_STOR_SENSE_LEN);

	if (dev_info->lv1_status) {
		if (!decode_lv1_status(dev_info->lv1_status,
				       &(sense_data[2]),
				       &(sense_data[12]),
				       &(sense_data[13]))) {
		} else {
			/* unknown error */
			printk(KERN_ERR "%s: FIXME issue real RS %#lx %d\n",
			       __FUNCTION__, dev_info->lv1_status,
			       dev_info->lv1_retval);
			sense_data[2] = HARDWARE_ERROR;
			dev_info->srb->result = DID_OK << 16;
		}
		sense_data[0] = 0x70;
	} else {
		/* no sense */
		sense_data[0] = 0x70;
		dev_info->srb->result = DID_OK << 16;
	}

	fill_from_dev_buffer(dev_info->srb, sense_data, len);
	ps3_stor_srb_done(dev_info);
	return 0;
}

static int ps3_stor_hdd_handle_just_ok(struct ps3_stor_dev_info * dev_info,
				       struct scsi_cmnd * srb)
{
	dev_info->srb->result = DID_OK << 16;
	ps3_stor_srb_done(dev_info);
	return 0;
}

static int ps3_stor_hdd_handle_sync_cache(struct ps3_stor_dev_info * dev_info,
					  struct scsi_cmnd * srb)
{
	struct ps3_stor_lv1_dev_info *lv1_dev_info = dev_info->lv1_dev_info;
	unsigned char keys[4];
	int error;

	/* issue command */
	init_completion(&(dev_info->irq_done));
	error = lv1_storage_send_device_command(lv1_dev_info->repo.did.dev_id,
						LV1_STORAGE_ATA_HDDOUT,
						0,
						0,
						0,
						0,
						&lv1_dev_info->current_tag);
	if (error) {
		/* error */
		printk(KERN_ERR "%s: send_device failed. lv1dev=%u ret=%d\n",
		       __FUNCTION__, lv1_dev_info->repo.did.dev_id, error);
		dev_info->srb->result = DID_ERROR << 16; /* FIXME: is better other error code? */
	} else {
		/* wait interrupt */
		wait_for_completion(&(dev_info->irq_done));

		/* check error */
		if (!dev_info->lv1_status) {
			dev_info->srb->result = DID_OK << 16;
		} else {
			decode_lv1_status(dev_info->lv1_status,
					  &(keys[0]), &(keys[1]), &(keys[2]));
			dev_info->srb->sense_buffer[0]  = 0x70;
			dev_info->srb->sense_buffer[2]  = keys[0];
			dev_info->srb->sense_buffer[7]  = 16 - 6;
			dev_info->srb->sense_buffer[12] = keys[1];
			dev_info->srb->sense_buffer[13] = keys[2];
			dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
		}
	}

	ps3_stor_srb_done(dev_info);
	return 0;
}

static int ps3_stor_hdd_handle_read_capacity(struct ps3_stor_dev_info * dev_info, struct scsi_cmnd * srb)
{
	struct ps3_stor_lv1_dev_info *lv1_dev_info = dev_info->lv1_dev_info;
	unsigned char data[PS3_STOR_READCAP_DATA_SIZE];
	u64 len;
	int ret;

	FUNC_START;
	memset(data, 0, sizeof(data));
	len = lv1_dev_info->region_info_array[srb->cmnd[1] >> 5].region_size - 1;
	data[0] = (len >> 24) & 0xff;
	data[1] = (len >> 16) & 0xff;
	data[2] = (len >> 8)  & 0xff;
	data[3] =  len        & 0xff;

	len = lv1_dev_info->sector_size;
	data[4] = (len >> 24) & 0xff;
	data[5] = (len >> 16) & 0xff;
	data[6] = (len >> 8)  & 0xff;
	data[7] =  len        & 0xff;

	ret = fill_from_dev_buffer(dev_info->srb, data, PS3_STOR_READCAP_DATA_SIZE);
	dev_info->srb->result = DID_OK << 16;
	ps3_stor_srb_done(dev_info);
	FUNC_END;
	return ret;
}


static int copy_page_data(unsigned char * buf, const unsigned char * data,
			   int length, int changable)
{
	if (changable) {
		/* reports no parameters are changable */
		memcpy(buf, data, 2);
		memset(buf + 2, 0, length - 2);
	} else {
		memcpy(buf, data, length);
	}
	return length;
}

static int fill_mode_page(struct ps3_stor_dev_info * dev_info,
			  unsigned char *buf, int page, int changable)
{
	int length;

	switch (page){
	case 8:
		/* TYPE_DISK; see sd_read_cache_type():sd.c */
		length = copy_page_data(buf, page_data_8, sizeof(page_data_8), changable);
		break;
	case 6:
		/* TYPE_RBC */
		length = copy_page_data(buf, page_data_6, sizeof(page_data_6), changable);
		break;
	case 0x3f: /* ALL PAGES, but sd.c checks only parameter header to see WriteProtect */
		length  = copy_page_data(buf, page_data_6, sizeof(page_data_6), changable);
		length += copy_page_data(buf + length, page_data_8, sizeof(page_data_8), changable);
		break;
	default:
		printk(KERN_ERR "%s: unknown page=%#x\n", __FUNCTION__, page);
		return 0;
	}

	return length;
}

/*
 * scsi disk driver asks only PAGE= 0x3f, 6(RBC), 8(SCSI disk)
 */
static int ps3_stor_hdd_handle_mode_sense(struct ps3_stor_dev_info * dev_info,
					  struct scsi_cmnd * srb)
{
	unsigned char sense_data[128];
	int offset = 0;

	/*
	 * NOTE: support MODE_SENSE_10 only
	 * see slave_cofigure()
	 */
	memset(sense_data, 0, sizeof(sense_data));
	/* parameter header */
	sense_data[2] = dev_info->lv1_dev_info->device_type;
	sense_data[3] = 0;      /* mid layer wants to see here     */
	/* bit 7=1 means WriteProtected    */
	offset = fill_mode_page(dev_info,
				&(sense_data[8]),
				dev_info->srb->cmnd[2] & 0x3f,
				dev_info->srb->cmnd[2] & 0xc0);
	sense_data[1] = offset + 8;        /* parameter length */
	sense_data[0] = (offset + 8) >> 8;

	fill_from_dev_buffer(dev_info->srb, sense_data, offset + 8);
	ps3_stor_srb_done(dev_info);
	return 0;
}


/*
 * convert kernel virtal address to lpar address for storage IO
 * NOTE: va should be within allocated special buffer
 *       if DEDICATED_SPECIAL bounce type
 */
static u64 ps3_stor_virtual_to_lpar(struct ps3_stor_dev_info *dev_info,
				    void *va)
{
	if (unlikely(dev_info->bounce_type == DEDICATED_SPECIAL)) {
		return dev_info->separate_bounce_lpar + (va - dev_info->bounce_buf);
	} else {
		return ps3_mm_phys_to_lpar(__pa(va));
	}
}


static int ps3_stor_common_handle_read(struct ps3_stor_dev_info * dev_info,
				       struct scsi_cmnd * srb)
{
	struct ps3_stor_lv1_dev_info *lv1_dev_info = dev_info->lv1_dev_info;
	int error;
	u64 lpar_addr;
	unsigned int region_id;
	u32 sectors = 0;
	u32 start_sector = 0;
	unsigned char *cmnd = dev_info->srb->cmnd;
	int ret = 0;

	/* check transfer length */
	switch (cmnd[0]) {
	case READ_10:
		start_sector = (cmnd[2] << 24) +
			(cmnd[3] << 16) +
			(cmnd[4] <<  8) +
			cmnd[5];
		sectors = (cmnd[7] << 8) +
			cmnd[8];
		break;
	case READ_6:
		start_sector = (cmnd[1] << 16) +
			(cmnd[2] <<  8) +
			cmnd[3];
		sectors = cmnd[4];
		break;

	}

	/* issue read */
	down_read(&dev_info->bounce_sem);
	lpar_addr = ps3_stor_virtual_to_lpar(dev_info, dev_info->bounce_buf);
	region_id = lv1_dev_info->region_info_array[(cmnd[1] >> 5)].region_id;
	init_completion(&(dev_info->irq_done));
	error = lv1_storage_read(lv1_dev_info->repo.did.dev_id,
				 region_id,
				 start_sector,
				 sectors,
				 0, /* flags */
				 lpar_addr,
				 &lv1_dev_info->current_tag);
	if (error) {
		/* error */
		printk(KERN_ERR "%s: error lv1dev =%u ret=%d\n", __FUNCTION__,
		       lv1_dev_info->repo.did.dev_id, error);
		dev_info->srb->result = DID_ERROR << 16; /* FIXME: other error code? */
		ret = -1;
	} else {
		/* wait irq */
		wait_for_completion(&(dev_info->irq_done));
		if (dev_info->lv1_status) {
			/* error */
			memset(dev_info->srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
			decode_lv1_status(dev_info->lv1_status,
					  &(dev_info->srb->sense_buffer[2]),
					  &(dev_info->srb->sense_buffer[12]),
					  &(dev_info->srb->sense_buffer[13]));
			dev_info->srb->sense_buffer[7] = 16 - 6;
			dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
			ret =  1;
		} else {
			/* OK */
			fill_from_dev_buffer(dev_info->srb,
					     dev_info->bounce_buf,
					     sectors * dev_info->sector_size);

			dev_info->srb->result = DID_OK << 16;
			ret =  0;
		}
	}

	ps3_stor_srb_done(dev_info);
	up_read(&dev_info->bounce_sem);
	return ret;
}

static int ps3_stor_common_handle_write(struct ps3_stor_dev_info * dev_info,
					struct scsi_cmnd * srb)
{
	struct ps3_stor_lv1_dev_info *lv1_dev_info = dev_info->lv1_dev_info;
	int ret;
	int error;
	u64 lpar_addr;
	unsigned int region_id;
	u32 start_sector = 0;
	u32 sectors = 0;
	unsigned char * cmnd = dev_info->srb->cmnd;

	/* check transfer length */
	switch (cmnd[0]) {
	case WRITE_10:
		start_sector = (cmnd[2] << 24) +
			(cmnd[3] << 16) +
			(cmnd[4] <<  8) +
			cmnd[5];
		sectors = (cmnd[7] << 8) +
			cmnd[8];
		break;
	case WRITE_6:
		start_sector = (cmnd[1] << 16) +
			(cmnd[2] <<  8) +
			cmnd[3];
		sectors = cmnd[4];
		break;
	}

	down_read(&dev_info->bounce_sem);
	ret = fetch_to_dev_buffer(dev_info->srb,
				  dev_info->bounce_buf,
				  sectors * dev_info->sector_size);

	lpar_addr = ps3_stor_virtual_to_lpar(dev_info, dev_info->bounce_buf);
	region_id = lv1_dev_info->region_info_array[(cmnd[1] >> 5)].region_id;
	init_completion(&(dev_info->irq_done));
	error = lv1_storage_write(lv1_dev_info->repo.did.dev_id,
				  region_id, /* region id */
				  start_sector,
				  sectors,
				  0, /* flags */
				  lpar_addr/*srb->request_buffer*/, /* assume non SG! */
				  &lv1_dev_info->current_tag);
	if (error) {
		/* error */
		printk(KERN_ERR "%s: error lv1dev=%u ret=%d\n", __FUNCTION__,
		       lv1_dev_info->repo.did.dev_id, error);
		dev_info->srb->result = DID_ERROR << 16; /* FIXME: other error code? */
		ret = -1;
	} else {

		/* wait irq */
		wait_for_completion(&(dev_info->irq_done));

		if (dev_info->lv1_status) {
			/* error */
			memset(dev_info->srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
			decode_lv1_status(dev_info->lv1_status,
					  &(dev_info->srb->sense_buffer[2]),
					  &(dev_info->srb->sense_buffer[12]),
					  &(dev_info->srb->sense_buffer[13]));
			dev_info->srb->sense_buffer[7] = 16 - 6;
			dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
			ret = 1;
		} else {
			/* OK */
			dev_info->srb->result = DID_OK << 16;
			ret = 0;
		}

	}
	ps3_stor_srb_done(dev_info);
	up_read(&dev_info->bounce_sem);
	return ret;
}

static int is_aligned_flash(u32 sector, int sector_size)
{
	u32 flash_align_sector = FLASH_ALIGN / sector_size;

	return (sector % flash_align_sector)? 0 : 1;
}

static u32 floor_align_flash(u32 sector, int sector_size)
{
	u32 flash_align_sector = FLASH_ALIGN / sector_size;

	return sector & ~(flash_align_sector - 1);
}

static u32 ceil_align_flash(u32 sector, int sector_size)
{
	u32 flash_align_sector = FLASH_ALIGN / sector_size;

	return (sector + (flash_align_sector - 1)) & ~(flash_align_sector - 1);
}

/*
 * special handling for flash drive; do safer way to write in order to reduce
 * the risk of flash corruption by sudden power off.
 */
static int ps3_stor_handle_write_flash(struct ps3_stor_dev_info * dev_info,
				       struct scsi_cmnd * srb)
{
	struct ps3_stor_lv1_dev_info *lv1_dev_info = dev_info->lv1_dev_info;
	int ret = 0;
	int error;
	u64 sector_size;
	u64 lpar_addr;
	unsigned int region_id;
	u64 start_sector = 0;
	u64 start_sector_aligned = 0;
	u64 sectors = 0;
	u64 sectors_aligned = 0;
	u64 current_sector;
	u64 aligned_sector_count;
	unsigned char * cmnd = dev_info->srb->cmnd;
	void * current_buffer;
	struct ps3_stor_lv1_region_info * region_info;

	static int align_warned;

	DPRINTK(KERN_ERR "%s: start\n", __FUNCTION__);

	/* check transfer length */
	switch (cmnd[0]) {
	case WRITE_10:
		start_sector = (cmnd[2] << 24) +
			(cmnd[3] << 16) +
			(cmnd[4] <<  8) +
			cmnd[5];
		sectors = (cmnd[7] << 8) +
			cmnd[8];
		break;
	case WRITE_6:
		start_sector = (cmnd[1] << 16) +
			(cmnd[2] <<  8) +
			cmnd[3];
		sectors = cmnd[4];
		break;
	}


        /*
         *    start_sector_aligned
         *   /          start_sector
         *  /          /
         * +----------+--------------------+---+
         *            |<-    sectors     ->|   |
         *            |<-   sectors_aligned  ->|
         *
         * ^-----------------------------------^ 256K align
         */
	sector_size = dev_info->sector_size;
	aligned_sector_count = FLASH_ALIGN / sector_size;

	start_sector_aligned = floor_align_flash(start_sector, sector_size);
	sectors_aligned = ceil_align_flash(start_sector + sectors, sector_size) - start_sector;

	/* check aligned border exceed region */
	region_info = &lv1_dev_info->region_info_array[cmnd[1] >> 5];
	if (!is_aligned_flash(region_info->region_start, sector_size) ||
	    (region_info->region_size < (start_sector_aligned + sectors_aligned))) {
		if (!align_warned) {
			printk(KERN_ERR "%s: region alignment is not 256k, continue to work with norman method\n",
			       __FUNCTION__);
			align_warned = 1;
		}
		return ps3_stor_common_handle_write(dev_info, srb);
	};

	down_read(&dev_info->bounce_sem);
	region_id = region_info->region_id;


	DPRINTK(KERN_ERR "%s: start=%#lx(%ld) start_a=%#lx(%ld) sec=%#lx(%ld) sec_a=%#lx(%ld)\n", __FUNCTION__,
		start_sector, start_sector,
		start_sector_aligned, start_sector_aligned,
		sectors, sectors,
		sectors_aligned, sectors_aligned);

	/*
	 * loop in the case that the requested write sectors across
	 * 245Kb alignment.  Since we have set max_sectors as 256kb,
	 * loop count is up to 2.
	 */
	for (current_sector = start_sector_aligned, ret = 0;
	     (current_sector < (start_sector + sectors_aligned)) && !ret;
	     current_sector += aligned_sector_count) {

		DPRINTK(KERN_ERR "%s: LOOP current=%#lx\n", __FUNCTION__, current_sector);

		current_buffer = dev_info->bounce_buf;

		/* read from (start_sector_aligned) to (start_sector) */
		if (current_sector < start_sector) {
			DPRINTK(KERN_ERR "%s: head read \n", __FUNCTION__);
			lpar_addr = ps3_stor_virtual_to_lpar(dev_info,
							     current_buffer);
			init_completion(&(dev_info->irq_done));
			error = lv1_storage_read(lv1_dev_info->repo.did.dev_id,
						 region_id,
						 current_sector,
						 start_sector - current_sector,
						 0,
						 lpar_addr,
						 &lv1_dev_info->current_tag);
			DPRINTK(KERN_ERR "HEAD start=%#lx, len=%#lx\n",
				start_sector_aligned, (start_sector - start_sector_aligned));
			if (error) {
				/* error */
				printk(KERN_ERR "%s: error lv1dev=%u ret=%d\n",
				       __FUNCTION__,
				       lv1_dev_info->repo.did.dev_id, error);
				dev_info->srb->result = DID_ERROR << 16; /* FIXME: other error code? */
				ret = -1;
				goto done;
			} else {
				/* wait irq */
				wait_for_completion(&(dev_info->irq_done));
			}
			if (dev_info->lv1_status) {
				/* error */
				memset(dev_info->srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
				decode_lv1_status(dev_info->lv1_status,
						  &(dev_info->srb->sense_buffer[2]),
						  &(dev_info->srb->sense_buffer[12]),
						  &(dev_info->srb->sense_buffer[13]));
				dev_info->srb->sense_buffer[7] = 16 - 6;
				dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
				ret = 1;
				goto done;
			} else {
				/* OK */
				ret = 0;
				current_buffer += (start_sector - start_sector_aligned) * sector_size;
			}
		} /* head remainder */


		if ((start_sector + sectors) < (current_sector + aligned_sector_count)) {
			void * buf = dev_info->bounce_buf;
			DPRINTK(KERN_ERR "%s: tail read\n", __FUNCTION__);
			buf += (start_sector + sectors - current_sector) * sector_size;
			lpar_addr = ps3_stor_virtual_to_lpar(dev_info, buf);
			init_completion(&(dev_info->irq_done));
			error = lv1_storage_read(lv1_dev_info->repo.did.dev_id,
						 region_id,
						 start_sector + sectors,
						 sectors_aligned - sectors,
						 0,
						 lpar_addr,
						 &lv1_dev_info->current_tag);
			DPRINTK(KERN_ERR "TAIL start=%#lx, len=%#lx\n",
				start_sector + sectors, sectors_aligned - sectors);
			if (error) {
				/* error */
				printk(KERN_ERR "%s: error lv1dev=%u ret=%d\n",
				       __FUNCTION__,
				       lv1_dev_info->repo.did.dev_id, error);
				dev_info->srb->result = DID_ERROR << 16; /* FIXME: other error code? */
				ret = -1;
				goto done;
			} else {
				/* wait irq */
				wait_for_completion(&(dev_info->irq_done));
			}
			if (dev_info->lv1_status) {
				/* error */
				memset(dev_info->srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
				decode_lv1_status(dev_info->lv1_status,
						  &(dev_info->srb->sense_buffer[2]),
						  &(dev_info->srb->sense_buffer[12]),
						  &(dev_info->srb->sense_buffer[13]));
				dev_info->srb->sense_buffer[7] = 16 - 6;
				dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
				ret = 1;
				goto done;
			} else {
				/* OK */
				ret = 0;
			}
		} /* tail remainder */

		{
			u64 copy_sectors_from, copy_sectors_to;

			/* start_sector is within this iteration */
			if ((current_sector < start_sector)  &&
			    (start_sector < (current_sector + aligned_sector_count))) {
				copy_sectors_from = start_sector;
			}
			else {
				copy_sectors_from = current_sector;
			}

			/* start_sector+sectors is within this iteration */
			if ((current_sector < (start_sector + sectors))  &&
			    ((start_sector + sectors) < (current_sector + aligned_sector_count))) {
				copy_sectors_to = start_sector + sectors;
			}
			else {
				copy_sectors_to = current_sector + aligned_sector_count;
			}

			DPRINTK(KERN_ERR "%s: copy to current=%p\n", __FUNCTION__, current_buffer);
			ret = fetch_to_dev_buffer_abs(dev_info->srb,
						      current_buffer,
						      (copy_sectors_from - start_sector) * sector_size,
						      (copy_sectors_to - start_sector) * sector_size);
		} /* write data */

		/* write 256K */
		DPRINTK(KERN_ERR "%s: WRITE sector=%#lx\n", __FUNCTION__, current_sector);
		lpar_addr = ps3_stor_virtual_to_lpar(dev_info,
						     dev_info->bounce_buf);
		init_completion(&(dev_info->irq_done));
		error = lv1_storage_write(lv1_dev_info->repo.did.dev_id,
					  region_id,
					  current_sector,
					  aligned_sector_count,
					  0,
					  lpar_addr,
					  &lv1_dev_info->current_tag);
		if (error) {
			/* error */
			printk(KERN_ERR "%s: error lv1dev=%u ret=%d\n",
			       __FUNCTION__, lv1_dev_info->repo.did.dev_id,
			       error);
			dev_info->srb->result = DID_ERROR << 16; /* FIXME: other error code? */
			ret = -1;
		} else {

			/* wait irq */
			wait_for_completion(&(dev_info->irq_done));

			if (dev_info->lv1_status) {
				/* error */
				memset(dev_info->srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
				decode_lv1_status(dev_info->lv1_status,
						  &(dev_info->srb->sense_buffer[2]),
						  &(dev_info->srb->sense_buffer[12]),
						  &(dev_info->srb->sense_buffer[13]));
				dev_info->srb->sense_buffer[7] = 16 - 6;
				dev_info->srb->result = SAM_STAT_CHECK_CONDITION;
				ret = 1;
			} else {
				/* OK */
				dev_info->srb->result = DID_OK << 16;
				ret = 0;
			}

		}
	} /* for */
 done:
	ps3_stor_srb_done(dev_info);
	up_read(&dev_info->bounce_sem);
	DPRINTK(KERN_ERR "%s: end\n", __FUNCTION__);
	return ret;
}

/*
 * NOTE: If return 1, all buffers communicate with the device
 *       should be in dedicated buffer area.
 *       Currently common_handle_read, common_handle_write know this
 *       restriction.
 *       And should implement remap function in ps3_stor_set_max_sectors()
 */
static int need_dedicated_dma_region(enum ps3_dev_type device_type)
{
	int ret = 0;
	switch (device_type) {
	case PS3_DEV_TYPE_STOR_FLASH:
		ret = 1; /* should be 1 */
		break;
	case PS3_DEV_TYPE_STOR_ROM:
		ret = 0;
		break;
	case PS3_DEV_TYPE_STOR_DISK:
		ret = 0;
		break;
	default:
		printk(KERN_ERR "%s: unknown type =%u\n", __FUNCTION__,
		       device_type);
		ret =  0;
		break;
	}
	return ret;
}
/*
 * allocate static(dedicated) bounce buffer
 */
static int get_dedicated_buffer_type(enum ps3_dev_type device_type)
{
	int ret = 0;
	switch (device_type) {
	case PS3_DEV_TYPE_STOR_FLASH:
		ret = DEDICATED_SPECIAL;
		break;
	case PS3_DEV_TYPE_STOR_ROM:
		ret = DEDICATED_KMALLOC;
		break;
	case PS3_DEV_TYPE_STOR_DISK:
		ret = DEDICATED_KMALLOC;
		break;
	default:
		printk(KERN_ERR "%s: unknown type =%u\n", __FUNCTION__,
		       device_type);
		ret =  0;
		break;
	}
	return ret;
}


extern unsigned long ps3_mem_total;
extern unsigned long ps3_rm_limit;
extern unsigned long ps3_2nd_mem_base;
extern unsigned long ps3_2nd_mem_size;

#define PS3_PCI_DMA_SIZE(mem) ((((mem -1) >> 27) + 1 ) << 27) /* 2^27=128M */
#define PS3_PCI_IO_PAGESIZE      24

static u64 ps3_allocate_dma_region(const struct ps3_device_id *did)
{
	u64 size, io_size, io_pagesize;
	u64 dma, flg = 0;
	int error;

	io_size = PS3_PCI_DMA_SIZE(ps3_mem_total);
	io_pagesize = PS3_PCI_IO_PAGESIZE;
	error = lv1_allocate_device_dma_region(did->bus_id, did->dev_id,
					       io_size, io_pagesize, flg,
					       &dma);
	if (error) {
		printk("lv1_allocate_device_dma_region faild, error=%d\n",
			error);
		return 0;
	}

	size = ps3_rm_limit;
	error = lv1_map_device_dma_region(did->bus_id, did->dev_id,
					  0, /* lpar addr */
					  dma, /* I/O addr */
					  size,
					  0xf800000000000000UL  /* flags */);
	if (error) {
		printk("lv1_map_device_dma_region faild, error=%d\n", error);
		return 0;
	}

	size = ps3_2nd_mem_size;
	error = lv1_map_device_dma_region(did->bus_id, did->dev_id,
					  ps3_2nd_mem_base,   /* lpar addr */
					  ps3_rm_limit + dma, /* I/O addr */
					  size,
					  0xf800000000000000UL  /* flags */);

	if (error) {
		printk("lv1_map_device_dma_region faild, error=%d\n", error);
		return 0;
	}
	return dma;
}

static u64 ps3_free_dma_region(const struct ps3_device_id *did, u64 dma)
{
	u64 size, io_size, io_pagesize;
	int error;

	io_size = PS3_PCI_DMA_SIZE(ps3_mem_total);
	io_pagesize = PS3_PCI_IO_PAGESIZE;

	if (dma == 0)
		return 0;

	/* unmap dma_region */
	size = ps3_rm_limit;
	error = lv1_unmap_device_dma_region(did->bus_id, did->dev_id,
					    dma, /* I/O addr */
					    size);
	if (error)
		printk("lv1_unmap_device_dma_region faild, error=%d\n", error);
	size = ps3_2nd_mem_size;
	error = lv1_unmap_device_dma_region(did->bus_id, did->dev_id,
					    ps3_rm_limit +  dma, /* I/O addr */
					    size);

	if (error)
		printk("lv1_unmap_device_dma_region faild, error=%d\n", error);

	/* free dma region */
	error = lv1_free_device_dma_region(did->bus_id, did->dev_id, dma);
	if (error)
		printk("lv1_free_device_dma_region faild, error=%d\n", error);
	return 0;
}


static void *ps3_stor_alloc_separate_memory(int alloc_size, u64 *lpar_addr)
{
	void * va;
	BUG_ON(alloc_size != ps3_stor_bounce_buffer.size);
	va = ps3_stor_bounce_buffer.address;
	*lpar_addr = ps3_mm_phys_to_lpar(__pa(va));
	return va;
}

static int ps3_stor_release_separate_memory(void *va, u64 lpar)
{
	/* Nothing to release anymore */
	return 0;
}


static int get_default_max_sector(struct ps3_stor_lv1_dev_info * lv1_dev_info)
{
	int ret = 0;
	switch (lv1_dev_info->device_type) {
	case PS3_DEV_TYPE_STOR_FLASH:
		ret = FLASH_ALIGN / lv1_dev_info->sector_size;
		break;
	case PS3_DEV_TYPE_STOR_ROM:
		ret = 32;
		break;
	case PS3_DEV_TYPE_STOR_DISK:
		ret =  128;
		break;
	default:
		printk(KERN_ERR "%s: unknown type =%u\n", __FUNCTION__,
		       lv1_dev_info->device_type);
		ret =  0;
		break;
	}
	return ret;
}


static irqreturn_t ps3_stor_hdd_irq_handler(int irq, void * context)
{
	struct ps3_stor_lv1_dev_info * lv1_dev_info = context;
	struct ps3_stor_dev_info * dev_info = lv1_dev_info->dev_info;
	int ret_val = IRQ_HANDLED;
	u64 tag;

	if (dev_info) {
		dev_info->lv1_retval = lv1_storage_get_async_status(lv1_dev_info->repo.did.dev_id,
								    &tag,
								    (u64 *)&dev_info->lv1_status);
		/*
		 * lv1_status = -1 may mean that ATAPI transport completed OK, but ATAPI command
		 * itself resulted CHECK CONDITION
		 * so, upper layer should issue REQUEST_SENSE to check the sense data
		 */
		if (tag != lv1_dev_info->current_tag)
			printk("%s: tag=%#lx ctag=%#lx\n", __FUNCTION__,
			       tag, lv1_dev_info->current_tag);
		if (dev_info->lv1_retval) {
			printk("%s: ret=%d status=%#lx\n", __FUNCTION__,
			       dev_info->lv1_retval, dev_info->lv1_status);
			//if (dev_info->lv1_retval == LV1_NO_ENTRY)
			//ret_val = IRQ_NONE;
		} else {
			complete(&(dev_info->irq_done));
		}
	}
	return ret_val;
}


/*
 * return 1 specified region is accessible from linux
 */
static irqreturn_t ps3_stor_temporary_irq_handler(int irq, void * context)
{
	struct ps3_stor_quirk_probe_info * info = context;

	info->lv1_retval = lv1_storage_get_async_status(info->device_id,
							&info->lv1_ret_tag,
							&info->lv1_status);
	complete(&(info->irq_done));

	return IRQ_HANDLED;
}

static int is_region_accessible(struct ps3_stor_lv1_dev_info * lv1_dev_info,
				unsigned int region_id)
{
	int accessible = 0;
	unsigned int irq_plug_id, dma_region;
	void * buf;
	struct ps3_stor_quirk_probe_info info;
	int error;

	/*
	 * special case
	 * cd-rom is assumed always accessible
	 */
	if (lv1_dev_info->device_type == PS3_DEV_TYPE_STOR_ROM)
		return 1;

	/*
	 * 1. open the device
	 * 2. register irq for the device
	 * 3. connect irq
	 * 4. map dma region
	 * 5. do read
	 * 6. umap dma region
	 * 7. disconnect irq
	 * 8. unregister irq
	 * 9. close the device
	 */
	memset(&info, 0, sizeof(info));

	error = lv1_open_device(lv1_dev_info->repo.did.bus_id,
				lv1_dev_info->repo.did.dev_id, 0);
	if (error)
		return 0;

	error = ps3_sb_event_receive_port_setup(PS3_BINDING_CPU_ANY,
				      &lv1_dev_info->repo.did,
				      lv1_dev_info->interrupt_id,
				      &irq_plug_id);
	if (error) {
		printk("%s:%u: ps3_sb_event_receive_port_setup failed (%d)\n",
		       __func__, __LINE__, error);
		goto fail_close_device;
	}

	error = request_irq(irq_plug_id, ps3_stor_temporary_irq_handler,
			    IRQF_DISABLED, "PS3 quirk", &info);
	if (error) {
		printk("%s:%d: request_irq failed (%d)\n", __func__, __LINE__,
		       error);
		goto fail_event_receive_port_destroy;
	}

	dma_region = ps3_allocate_dma_region(&lv1_dev_info->repo.did);
	if (!dma_region)
		goto fail_free_irq;

	/* 4k buffer is for fail safe of large sector devices */
	buf = kmalloc(4096, GFP_KERNEL);
	if (!buf) {
		printk(KERN_ERR "%s: no memory while probing dev=%u",
		       __FUNCTION__, lv1_dev_info->repo.did.dev_id);
		goto fail_free_dma_region;
	};

	init_completion(&(info.irq_done));
	info.device_id = lv1_dev_info->repo.did.dev_id;
	error = lv1_storage_read(lv1_dev_info->repo.did.dev_id,
				 region_id,
				 0, /* start sector */
				 1, /* sector count */
				 0, /* flags */
				 ps3_mm_phys_to_lpar(__pa(buf)), /* no need special convert */
				 &info.lv1_tag);
	if (error)
		goto fail_free_buf;

	wait_for_completion(&(info.irq_done));

	if (!info.lv1_retval && !info.lv1_status) {
		if (info.lv1_tag != info.lv1_ret_tag) {
			printk(KERN_ERR "%s: tag mismached dev=%u\n",
			       __FUNCTION__, lv1_dev_info->repo.did.dev_id);
		} else
			accessible = 1;
	}

fail_free_buf:
	kfree(buf);
fail_free_dma_region:
	ps3_free_dma_region(&lv1_dev_info->repo.did, dma_region);
fail_free_irq:
	free_irq(irq_plug_id, &info);
fail_event_receive_port_destroy:
	ps3_sb_event_receive_port_destroy(&lv1_dev_info->repo.did,
					  lv1_dev_info->interrupt_id,
					  irq_plug_id);
fail_close_device:
	lv1_close_device(lv1_dev_info->repo.did.bus_id,
			 lv1_dev_info->repo.did.dev_id);

	return accessible;
}

static unsigned int ps3_stor_enum_storage_type(enum ps3_dev_type device_type)
{
	struct ps3_repository_device repo, *prev = NULL;
	unsigned int devices = 0;
	int error;
	unsigned int interrupt_id, regions, region_id;
	struct ps3_stor_lv1_dev_info * lv1_dev_info;
	u64 port, blksize, blocks, region_size, region_start;
	unsigned int i, j, accessible_regions;

	while (!(error = ps3_repository_find_device(PS3_BUS_TYPE_STORAGE,
						    device_type, prev,
						    &repo))) {
		prev = &repo;
		error = ps3_repository_find_interrupt(&repo,
				PS3_INTERRUPT_TYPE_EVENT_PORT, &interrupt_id);
		if (error) {
			printk(KERN_ERR "%s: find_interrupt failed (%d)\n",
			       __FUNCTION__, error);
			continue;
		}

		error = ps3_repository_read_stor_dev_info(repo.bus_index,
							  repo.dev_index,
							  &port, &blksize,
							  &blocks, &regions);
		if (error) {
			printk(KERN_ERR "%s: read_stor_dev_info failed\n",
			       __FUNCTION__);
			continue;
		}

		/* LUN limitation */
		if (regions > 8) {
			printk(KERN_ERR "%s: region count exceeded (%u).  the rest are ignored\n",
			       __FUNCTION__, regions);
			regions = 8;
		}

		lv1_dev_info = &(ps3_stor_lv1_dev_info_array[ps3_stor_lv1_devnum]);
		INIT_LIST_HEAD(&(lv1_dev_info->bus_dev_list));

		lv1_dev_info->repo = repo;
		lv1_dev_info->device_type = device_type;
		lv1_dev_info->interrupt_id = interrupt_id;
		lv1_dev_info->sector_size = blksize;
		lv1_dev_info->attached_port = port;
		lv1_dev_info->regions = regions;

		/* check how many regions are accessible */
		accessible_regions = 0;
		for (i = 0; i < regions; i++) {
			if (is_region_accessible(lv1_dev_info, i)) {
				set_bit(i, &(lv1_dev_info->accessible_region_flag));
				accessible_regions ++;
			}
		}
		if (!accessible_regions) {
		    printk(KERN_WARNING "No accessible regions found\n");
		    continue;
		}

		lv1_dev_info->region_info_array = kzalloc(sizeof(struct ps3_stor_lv1_region_info) * accessible_regions,
							  GFP_KERNEL);
		if (!lv1_dev_info->region_info_array) {
			printk(KERN_ERR "%s: kzalloc failed for info array\n",
			       __FUNCTION__);
			continue;
		}

		lv1_dev_info->accessible_regions = accessible_regions;
		for (i = j = 0; i < regions; i++) {
			if (!test_bit(i, &lv1_dev_info->accessible_region_flag))
				continue;

			if (ps3_repository_read_stor_dev_region(repo.bus_index,
							        repo.dev_index,
								i, &region_id,
							        &region_start,
							        &region_size)) {
				printk(KERN_ERR "%s: read_stor_dev_region failed\n",
				       __FUNCTION__);
				continue;
			}
			printk(KERN_INFO "Region %u: id %u start %lu size %lu\n",
			       i, region_id, region_start, region_size);
			lv1_dev_info->region_info_array[j].region_index = i;
			lv1_dev_info->region_info_array[j].region_id = region_id;
			lv1_dev_info->region_info_array[j].region_start = region_start;
			lv1_dev_info->region_info_array[j].region_size = region_size;
			j++;
		}
		printk(KERN_INFO "ps3_stor: dev=%u type=%u port=%lu regions=%u accessible=%u\n",
		       repo.did.dev_id, device_type, port, regions,
		       accessible_regions);
		ps3_stor_lv1_devnum++;

		devices++;
	}
	if (error != -ENODEV)
		printk(KERN_ERR "%s: find_device failed: %d\n", __FUNCTION__,
		       error);
	return devices;
}

/*
 * returns current number of found HDDs
 * and collect device info
 */
static unsigned int ps3_stor_enum_storage_drives(void)
{
	unsigned int devices = 0;

	printk("Looking for disk devices...\n");
	devices += ps3_stor_enum_storage_type(PS3_DEV_TYPE_STOR_DISK);
	printk("Looking for ROM devices...\n");
	devices += ps3_stor_enum_storage_type(PS3_DEV_TYPE_STOR_ROM);
	printk("Looking for FLASH devices...\n");
	devices += ps3_stor_enum_storage_type(PS3_DEV_TYPE_STOR_FLASH);

	return devices;
}


static void ps3_stor_device_release(struct device * device)
{
	FUNC_START;
	// place holder
	FUNC_END;
}

const static struct platform_device ps3_stor_platform_device = {
	.name           = "ps3_stor",
	.dev            = {
		.release        = ps3_stor_device_release
	}
};


/*
  construct a host structure
  and associated structures for
  its devices.
  register the host thru device_register()
*/
static int ps3_stor_add_adapter(struct ps3_stor_lv1_bus_info * lv1_bus_info)
{
	int k;
        int error = 0;
        struct ps3_stor_host_info *host_info;
        struct ps3_stor_dev_info *dev_info;
        struct list_head *lh, *lh_sf;

        host_info = kzalloc(sizeof(struct ps3_stor_host_info), GFP_KERNEL);

        if (NULL == host_info) {
                printk(KERN_ERR "%s: out of memory \n", __FUNCTION__);
                return -ENOMEM;
        }
        INIT_LIST_HEAD(&host_info->dev_info_list);
	host_info->lv1_bus_info = lv1_bus_info;

	/* create structures for child devices of this adapter */
        for (k = 0; k < lv1_bus_info->devices; k++) {
                dev_info = kzalloc(sizeof(struct ps3_stor_dev_info),
				   GFP_KERNEL);
                if (NULL == dev_info) {
                        printk(KERN_ERR "%s: out of memory \n", __FUNCTION__);
                        error = -ENOMEM;
			goto clean;
                }
                dev_info->host_info = host_info;
		INIT_LIST_HEAD(&dev_info->dev_list);
		spin_lock_init(&dev_info->srb_lock);
		init_rwsem(&dev_info->bounce_sem);
                list_add_tail(&dev_info->dev_list, &host_info->dev_info_list);
        }

        spin_lock(&ps3_stor_host_list_lock);
        list_add_tail(&host_info->host_list, &ps3_stor_host_list);
        spin_unlock(&ps3_stor_host_list_lock);

	/* copy struct platform_device */
        host_info->dev =  ps3_stor_platform_device;
        host_info->dev.id = ps3_stor_add_host;

        error = platform_device_register(&host_info->dev);

        if (error)
		goto clean;

	/* bump up registerd buses */
	++ps3_stor_add_host;

        return error;

clean:
	list_for_each_safe(lh, lh_sf, &host_info->dev_info_list) {
		dev_info = list_entry(lh, struct ps3_stor_dev_info, dev_list);
		list_del(&dev_info->dev_list);
		kfree(dev_info);
	}

	kfree(host_info);
        return error;
}

static void ps3_stor_remove_adapter(void)
{
        struct ps3_stor_host_info * host_info = NULL;

        spin_lock(&ps3_stor_host_list_lock);
        if (!list_empty(&ps3_stor_host_list)) {
                host_info = list_entry(ps3_stor_host_list.prev,
                                       struct ps3_stor_host_info, host_list);
		list_del(&host_info->host_list);
	}
        spin_unlock(&ps3_stor_host_list_lock);

	if (!host_info)
		return;

        platform_device_unregister(&host_info->dev);
	kfree(host_info);
        --ps3_stor_add_host;
}

static int ps3_stor_wait_device_ready(void)
{
	unsigned int bus_index, bus_id, num_of_dev;
	int error;
	u64 tag, status;
	int retries;
	int ret = 0;
	int i;
	u64 * buf;
	int region_ready = 0;
	int region_expected = 0;
	struct device_probe_info * info_array;

	/* find the storage bus */
	error = ps3_repository_find_bus(PS3_BUS_TYPE_STORAGE, 0, &bus_index);
	if (error) {
		printk(KERN_ERR "%s: Cannot find storage bus (%d)\n",
		       __FUNCTION__, error);
		return 0;
	}

	error = ps3_repository_read_bus_id(bus_index, &bus_id);
	if (error) {
		printk(KERN_ERR "%s: read_bus_id failed (%d)\n", __FUNCTION__,
		       error);
		return 0;
	}

	error = ps3_repository_read_bus_num_dev(bus_index, &num_of_dev);
	if (error) {
		printk(KERN_ERR "%s: read_bus_num_dev failed (%d)\n",
		       __FUNCTION__, error);
		return 0;
	}

	/* 1) wait for expected devices becomes in repositry */
	retries = 0;
	while (retries++ < ps3_stor_wait_time)
	{
		if (ps3_repository_read_bus_num_dev(bus_index, &num_of_dev)) {
			continue;
		}
		if (ps3_stor_wait_num_storages + 1 <= num_of_dev)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
		printk(".");
	}
	printk("\n");

	buf = kzalloc(512, GFP_KERNEL);
	if (!buf)
		return 0;

	info_array = kzalloc(sizeof(struct device_probe_info) * num_of_dev, GFP_KERNEL);
	if (!info_array) {
		ret = -1;
		goto cleanup_0;
	}

	/* 2) store the device info */
	for (i = 0; i < num_of_dev; i++) {
		if (ps3_repository_read_dev_id(bus_index, i,
					       &info_array[i].device_id)) {
			BUG();
		}
		ps3_repository_read_dev_type(bus_index, i,
					     &info_array[i].device_type);
		info_array[i].found = 1;

		switch (info_array[i].device_type) {
		case PS3_DEV_TYPE_STOR_DISK:
		case PS3_DEV_TYPE_STOR_FLASH:
			info_array[i].region_expected = 1;
			region_expected ++;
			ret ++;
			break;
		case PS3_DEV_TYPE_STOR_ROM:
			ret ++;
		default:
			break;
		}
	} /* for */


	/* 2-1) open special event device */
	error = lv1_open_device(bus_id, NOTIFICATION_DEVID, 0);
	if (error) {
		printk(KERN_ERR "%s: open failed notification dev %d\n",
		       __FUNCTION__, error);
		ret = 0;
		goto cleanup_1;
	}

	/* 2-2) write info to request notify */
	buf[0] = 0;
	buf[1] = (1 << 1); /* region update info only */
	error = lv1_storage_write(NOTIFICATION_DEVID,
				  0, /* region */
				  0, /* lba */
				  1, /* sectors to write */
				  0, /* flags */
				  ps3_mm_phys_to_lpar(__pa(buf)), /* no need special convert */
				  &tag);
	if (error) {
		printk(KERN_ERR "%s: notify request write failed %d\n",
		       __FUNCTION__, error);
		ret = 0;
		goto cleanup_2;
	}

	/* wait for completion in one sec */
	retries = 0;
	while ((error = lv1_storage_check_async_status(NOTIFICATION_DEVID, tag,
						       &status)) &&
	       (retries++ < 1000)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}
	if (error) {
		/* write not completed */
		printk(KERN_ERR "%s: write not completed %d\n", __FUNCTION__,
		       error);
		ret = 0;
		goto cleanup_2;
	}

	/* 2-3) read to wait region notification for each device */
	while (region_ready < region_expected) {
		memset(buf, 0, 512);
		error = lv1_storage_read(NOTIFICATION_DEVID,
					 0, /* region */
					 0, /* lba */
					 1, /* sectors to read */
					 0, /* flags */
					 ps3_mm_phys_to_lpar(__pa(buf)), /* no need special convert */
					 &tag);
		retries = 0;
		while ((error = lv1_storage_check_async_status(NOTIFICATION_DEVID, tag, &status)) &&
		       (retries++ < 1000)) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(1);
		}
		if (error) {
			/* read not completed */
			printk(KERN_ERR "%s: read not complated %d\n",
			       __FUNCTION__, error);
			break;
		}

		/* 2-4) verify the notification */
		if (buf[0] != 1) {
			/* other info notified */
			printk(KERN_ERR "%s: notification info %ld dev=%lx type=%lx\n", __FUNCTION__,
			       buf[0], buf[2], buf[3]);
		}

		for (i = 0; i < num_of_dev; i++) {
			if (info_array[i].found && info_array[i].device_id == buf[2]) {
				info_array[i].region_ready = 1;
				region_ready ++;
				break;
			}
		} /* for */
	} /* while */

 cleanup_2:
	lv1_close_device(bus_id, NOTIFICATION_DEVID);

 cleanup_1:
	kfree(info_array);
 cleanup_0:
	kfree(buf);
	return ret;
}


static const struct scsi_command_handler_info scsi_cmnd_info_table_hdd[256] =
{
	[INQUIRY]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_inquiry},
	[REQUEST_SENSE]           = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_request_sense},
	[TEST_UNIT_READY]         = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_just_ok},
	[READ_CAPACITY]           = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_read_capacity},
	[MODE_SENSE_10]           = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_mode_sense},
	[SYNCHRONIZE_CACHE]       = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_sync_cache},
	[READ_10]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_read},
	[READ_6]                  = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_read},
	[WRITE_10]                = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_write},
	[WRITE_6]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_write}
};

static const struct scsi_command_handler_info scsi_cmnd_info_table_flash[256] =
{
	[INQUIRY]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_inquiry},
	[REQUEST_SENSE]           = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_request_sense},
	[TEST_UNIT_READY]         = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_just_ok},
	[READ_CAPACITY]           = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_read_capacity},
	[MODE_SENSE_10]           = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_mode_sense},
	[SYNCHRONIZE_CACHE]       = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_hdd_handle_sync_cache},
	[READ_10]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_read},
	[READ_6]                  = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_read},
	[WRITE_10]                = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_handle_write_flash},
	[WRITE_6]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_handle_write_flash}
};

static const struct scsi_command_handler_info scsi_cmnd_info_table_atapi[256] =
{
	[INQUIRY]                 = {USE_SRB_6, PIO_DATA_IN_PROTO, DIR_READ,
				     ps3_stor_atapi_handle_simple},
	[REQUEST_SENSE]           = {USE_SRB_6, PIO_DATA_IN_PROTO, DIR_READ,
				     ps3_stor_atapi_handle_request_sense},
	[START_STOP]              = {0, NON_DATA_PROTO, DIR_NA,
				     ps3_stor_atapi_handle_simple},
	[ALLOW_MEDIUM_REMOVAL]    = {0, NON_DATA_PROTO, DIR_NA,
				     ps3_stor_atapi_handle_simple},
	[TEST_UNIT_READY]         = {0, NON_DATA_PROTO, DIR_NA,
				     ps3_stor_atapi_handle_simple},
	[READ_CAPACITY]           = {8, PIO_DATA_IN_PROTO,  DIR_READ,
				     ps3_stor_atapi_handle_simple},
	[MODE_SENSE_10]           = {USE_SRB_10, PIO_DATA_IN_PROTO, DIR_READ,
				     ps3_stor_atapi_handle_simple},
	[READ_TOC]                = {USE_SRB_10, PIO_DATA_IN_PROTO, DIR_READ,
				     ps3_stor_atapi_handle_simple},
	[GPCMD_GET_CONFIGURATION] = {USE_SRB_10, PIO_DATA_IN_PROTO, DIR_READ,
				     ps3_stor_atapi_handle_simple},
	[GPCMD_READ_DISC_INFO]    = {USE_SRB_10, PIO_DATA_IN_PROTO, DIR_READ,
				     ps3_stor_atapi_handle_simple},
	[READ_10]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_read},
	[READ_6]                  = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_read},
	[WRITE_10]                = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_write},
	[WRITE_6]                 = {NOT_AVAIL, NA_PROTO, DIR_NA,
				     ps3_stor_common_handle_write},
	[GPCMD_READ_CD]           = {USE_CDDA_FRAME_RAW, DMA_PROTO, DIR_READ,
				     ps3_stor_atapi_handle_simple}
};


/*
 * called from scsi mid layer when it want to probe a
 * device.
 * Prepare so that mid can issue SCSI commands later (slave_configure)
 */
static int ps3_stor_slave_alloc(struct scsi_device * scsi_dev)
{
        int error = 0;
        struct ps3_stor_host_info * host_info = NULL;
	struct ps3_stor_dev_info * dev_info = NULL;
        struct Scsi_Host *scsi_host;
	struct ps3_stor_lv1_bus_info * lv1_bus_info;
	struct ps3_stor_lv1_dev_info * lv1_dev_info = NULL;
	struct list_head * pos;
	int found;
	char thread_name[64];

	FUNC_START;

	scsi_host = scsi_dev->host;
	host_info = *(struct ps3_stor_host_info **)(scsi_host->hostdata);
	lv1_bus_info = host_info->lv1_bus_info;
	/*
	 * connect lv1_dev_info with scsi_device
	 * assume SCSI mid layer started scsi id with ZERO '0'
	 */
	found = 0;
	list_for_each(pos, &(lv1_bus_info->dev_list)) {
		lv1_dev_info = list_entry(pos, struct ps3_stor_lv1_dev_info,
					  bus_dev_list);

		if ((lv1_dev_info->bus_device_index == scsi_dev->id) &&
		    (scsi_dev->lun < lv1_dev_info->accessible_regions)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		error = -ENXIO;
		goto out;
	}

	/*
	 * connect scsi_dev with dev_info
	 */
	found = 0;
	list_for_each(pos, &(host_info->dev_info_list)) {
		dev_info = list_entry(pos, struct ps3_stor_dev_info, dev_list);
		if (!dev_info->used) {
			dev_info->used = 1;
			dev_info->target = scsi_dev->id;
			dev_info->lv1_dev_info = lv1_dev_info;
			switch (lv1_dev_info->device_type)
			{
			case PS3_DEV_TYPE_STOR_DISK:
				dev_info->handler_info = scsi_cmnd_info_table_hdd;
				break;
			case PS3_DEV_TYPE_STOR_FLASH:
				dev_info->handler_info = scsi_cmnd_info_table_flash;
				break;
			case PS3_DEV_TYPE_STOR_ROM:
				dev_info->handler_info = scsi_cmnd_info_table_atapi;
				break;
			default:
				break;
			}
			/* reverse link */
			lv1_dev_info->dev_info = dev_info;
			scsi_dev->hostdata = dev_info;
			/* copy sector length and capacity */
			dev_info->sector_size = lv1_dev_info->sector_size;
			found = 1;
			break;
		} else {
			if (dev_info->target == scsi_dev->id) {
				/* another lun ? */
				if (scsi_dev->lun < lv1_dev_info->accessible_regions) {
					/* ok, support this lun */
					scsi_dev->hostdata = dev_info;
					goto skip_per_device_configure;
				}
			}
		}
	}

	if (!found) {
		printk(KERN_ERR "%s: no empty dev_info for device id=%d lun=%d \n", __FUNCTION__,
		       scsi_dev->id, scsi_dev->lun);
		error = -ENODEV;
		goto out;
	}
	FUNC_STEP_C("1");

	/* open lv1 device */
	error = lv1_open_device(lv1_dev_info->repo.did.bus_id,
			        lv1_dev_info->repo.did.dev_id, 0);
	if (error) {
		printk(KERN_ERR "%s:open failed %d\n", __FUNCTION__, error);
		error = -ENODEV;
		goto out;
	}

	error = ps3_sb_event_receive_port_setup(PS3_BINDING_CPU_ANY,
						&lv1_dev_info->repo.did /* host_info->dev.did */,
						lv1_dev_info->interrupt_id,
						&lv1_dev_info->irq_plug_id);
	if (error) {
		printk("%s:%u: ps3_sb_event_receive_port_setup failed (%d)\n",
		       __func__, __LINE__, error);
		error = -EPERM;
		goto fail_close_device;
	}

	FUNC_STEP_C("2");

	error = request_irq(lv1_dev_info->irq_plug_id,
			    ps3_stor_hdd_irq_handler, IRQF_DISABLED,
			    "PS3 stor", lv1_dev_info);
	if (error) {
		printk("%s:%d: request_irq failed (%d)\n", __func__, __LINE__,
		       error);
		goto fail_event_receive_port_destroy;
	}

	FUNC_STEP_C("3");

	/* prepare dma regions for the device */
	down_write(&dev_info->bounce_sem);
	switch (get_dedicated_buffer_type(lv1_dev_info->device_type)) {
	case DEDICATED_KMALLOC:
		/*
		 * adjust max_sector count.
		 * mid layer already set default value from host template
		 */
		blk_queue_max_sectors(scsi_dev->request_queue, get_default_max_sector(lv1_dev_info));
		/* create its own static bouce buffer */
		dev_info->dedicated_bounce_size = get_default_max_sector(lv1_dev_info) * lv1_dev_info->sector_size;
		dev_info->bounce_buf = kmalloc(dev_info->dedicated_bounce_size, GFP_KERNEL | __GFP_DMA);
		up_write(&dev_info->bounce_sem);
		if (!dev_info->bounce_buf) {
			printk(KERN_ERR "%s:kmalloc for static bounce buffer failed %#x\n", __FUNCTION__,
			       dev_info->dedicated_bounce_size);
			error = -ENOMEM;
			goto fail_free_irq;
		}
		dev_info->bounce_type = DEDICATED_KMALLOC;
		break;
	case DEDICATED_SPECIAL:
		blk_queue_max_sectors(scsi_dev->request_queue, get_default_max_sector(lv1_dev_info));
		/* use static buffer, kmalloc can not allocate 256K */
		dev_info->dedicated_bounce_size = FLASH_ALIGN;
		dev_info->bounce_buf = ps3_stor_alloc_separate_memory(FLASH_ALIGN,
								      &dev_info->separate_bounce_lpar);
		if (!dev_info->bounce_buf) {
			error = -ENOMEM;
			goto fail_free_irq;
		}
		up_write(&dev_info->bounce_sem);
		dev_info->bounce_type = DEDICATED_SPECIAL;
		break;
	}
	/* allocate dma region */
	if (need_dedicated_dma_region(lv1_dev_info->device_type)) {
		error = lv1_allocate_device_dma_region(lv1_dev_info->repo.did.bus_id,
						       lv1_dev_info->repo.did.dev_id,
						       CEIL_ALIGN_4K(dev_info->dedicated_bounce_size),
						       12 /* 4K */,
						       0,
						       &lv1_dev_info->dma_region);
		if (error || !lv1_dev_info->dma_region) {
			printk(KERN_ERR "%s:allocate dma region failed %d\n",
			       __FUNCTION__, error);
			error = -ENOMEM;
			goto fail_free_irq;
		}
		error = lv1_map_device_dma_region(lv1_dev_info->repo.did.bus_id,
						  lv1_dev_info->repo.did.dev_id,
						  ps3_stor_virtual_to_lpar(dev_info, dev_info->bounce_buf),
						  lv1_dev_info->dma_region,
						  CEIL_ALIGN_4K(dev_info->dedicated_bounce_size),
						  0xf800000000000000UL);
		DPRINTK(KERN_ERR "%s:map bounce buffer %d va=%p lp=%#lx pa=%#lx size=%#x dma=%#lx\n",
			__FUNCTION__, error, dev_info->bounce_buf,
			ps3_stor_virtual_to_lpar(dev_info, dev_info->bounce_buf),
		       __pa(dev_info->bounce_buf),
		       dev_info->dedicated_bounce_size,
		       lv1_dev_info->dma_region);
		if (error) {
			lv1_free_device_dma_region(lv1_dev_info->repo.did.bus_id,
						   lv1_dev_info->repo.did.dev_id,
						   lv1_dev_info->dma_region);

			error = -ENODEV;
			goto fail_free_irq;
		}
		dev_info->dedicated_dma_region = 1;

	} else {
		lv1_dev_info->dma_region =
			ps3_allocate_dma_region(&lv1_dev_info->repo.did);
		if (!lv1_dev_info->dma_region) {
			printk(KERN_ERR "%s:create dma region failed\n",
			       __FUNCTION__);
			error = -ENODEV;
			goto fail_free_irq;
		}
	}
	FUNC_STEP_C("4");

	/* create receive thread */
	sprintf(thread_name, "ps3stor-%d-%d",
		scsi_host->host_no, scsi_dev->id);
	dev_info->thread_struct = kthread_create(ps3_stor_main_thread,
						 dev_info, thread_name);
	if (IS_ERR(dev_info->thread_struct)) {
		error = -ENOMEM;
		dev_info->thread_struct = NULL;
		goto fail_free_irq;
	}
	init_MUTEX_LOCKED(&(dev_info->thread_sema));
	wake_up_process(dev_info->thread_struct);

skip_per_device_configure:
	FUNC_END;
        return 0;

fail_free_irq:
	FUNC_STEP_C("5");
	free_irq(lv1_dev_info->irq_plug_id, lv1_dev_info);
fail_event_receive_port_destroy:
	FUNC_STEP_C("6");
	ps3_sb_event_receive_port_destroy(&lv1_dev_info->repo.did,
					  lv1_dev_info->interrupt_id,
					  lv1_dev_info->irq_plug_id);
fail_close_device:
	FUNC_STEP_C("7");
	lv1_close_device(lv1_dev_info->repo.did.bus_id, lv1_dev_info->repo.did.dev_id);
out:
	FUNC_END_C("error");
	return error;/* say failed to alloc */
}

static int ps3_stor_slave_configure(struct scsi_device * scsi_dev)
{

	if (scsi_dev->host->max_cmd_len != PS3_STOR_MAX_CMD_LEN)
		scsi_dev->host->max_cmd_len = PS3_STOR_MAX_CMD_LEN;

	if (scsi_dev->host->cmd_per_lun)
		scsi_adjust_queue_depth(scsi_dev, 0, scsi_dev->host->cmd_per_lun);
	/*
	 * ATAPI SFF8020 devices use MODE_SENSE_10,
	 * so we can prohibit MODE_SENSE_6
	 */
	scsi_dev->use_10_for_ms = 1;

	return 0;
}

static void ps3_stor_slave_destroy(struct scsi_device * scsi_dev)
{
	int error;
	struct ps3_stor_dev_info * dev_info =
				(struct ps3_stor_dev_info *)scsi_dev->hostdata;
	struct ps3_stor_lv1_dev_info * lv1_dev_info = dev_info->lv1_dev_info;

	/* only LUN=0 should do */
	if (scsi_dev->lun != 0) {
		printk(KERN_ERR "%s: id=%d lun=%d skipped\n", __FUNCTION__,
		       scsi_dev->id, scsi_dev->lun);
		return;
	}

	/* terminate main thread */
	dev_info->thread_wakeup_reason = THREAD_TERMINATE;
	init_completion(&(dev_info->thread_terminated));
	up(&(dev_info)->thread_sema);
	wait_for_completion(&(dev_info->thread_terminated));


	/* free resources */
	switch (dev_info->bounce_type) {
	case DEDICATED_SPECIAL:
		ps3_stor_release_separate_memory(dev_info->bounce_buf,
						 dev_info->separate_bounce_lpar);
		dev_info->bounce_buf = NULL;
		break;
	case DEDICATED_KMALLOC:
		kfree(dev_info->bounce_buf);
		dev_info->bounce_buf = NULL;
		break;
	}

	if (dev_info->dedicated_dma_region) {
		error = lv1_unmap_device_dma_region(lv1_dev_info->repo.did.bus_id,
						    lv1_dev_info->repo.did.dev_id,
						    lv1_dev_info->dma_region,
						    CEIL_ALIGN_4K(dev_info->dedicated_bounce_size));
		if (error) {
			printk(KERN_ERR "%s: unmap dma region failed %d\n",
			       __FUNCTION__, error);
		}
		error = lv1_free_device_dma_region(lv1_dev_info->repo.did.bus_id,
						   lv1_dev_info->repo.did.dev_id,
						   lv1_dev_info->dma_region);
		if (error) {
			printk(KERN_ERR "%s: unmap dma region failed %d\n",
			       __FUNCTION__, error);
		}
		dev_info->dedicated_dma_region = 0;
	} else {
		error = ps3_free_dma_region(&lv1_dev_info->repo.did,
					    lv1_dev_info->dma_region);
		if (error) {
			printk(KERN_ERR "%s: free dma region failed %d\n",
			       __FUNCTION__, error);
		}
	}

	free_irq(lv1_dev_info->irq_plug_id, lv1_dev_info);

	ps3_sb_event_receive_port_destroy(&lv1_dev_info->repo.did,
					  lv1_dev_info->interrupt_id,
					  lv1_dev_info->irq_plug_id);
	if (error)
		printk(KERN_ERR "%s: disconnect event irq %d\n", __FUNCTION__,
		       error);

	error = lv1_close_device(lv1_dev_info->repo.did.bus_id,
				 lv1_dev_info->repo.did.dev_id);
	if (error)
		printk(KERN_ERR "%s: close device %d\n", __FUNCTION__, error);

	if (dev_info) {
		/* make this slot avaliable for re-use */
		dev_info->used = 0;
		scsi_dev->hostdata = NULL;
	}

}

static int ps3_stor_queuecommand(struct scsi_cmnd * srb,
				 void (*done)(struct scsi_cmnd *))
{
	struct ps3_stor_dev_info * dev_info;
	unsigned long flags;
	int ret = 0;
	dev_info = (struct ps3_stor_dev_info *)srb->device->hostdata;

	spin_lock_irqsave(&dev_info->srb_lock, flags);
	{
		if (dev_info->srb) {
			/* no more than one can be processed */
			printk(KERN_ERR "%s: more than 1 SRB queued %d %d\n", __FUNCTION__,
			       srb->device->host->host_no, srb->device->id);
			ret = SCSI_MLQUEUE_HOST_BUSY;
		} else {
			srb->scsi_done = done;
			dev_info->srb = srb;

			dev_info->thread_wakeup_reason = SRB_QUEUED;
			up(&(dev_info->thread_sema));
			ret = 0;
		};
	}
	spin_unlock_irqrestore(&(dev_info->srb_lock), flags);
	return ret;
}

static int ps3_stor_host_reset(struct scsi_cmnd * srb)
{
	return FAILED;
}


static ssize_t ps3_stor_get_max_sectors(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct scsi_device *scsi_dev = from_dev_to_scsi_device(dev);
	struct ps3_stor_dev_info * dev_info =
		(struct ps3_stor_dev_info *)scsi_dev->hostdata;
	ssize_t ret;

	down_read(&dev_info->bounce_sem);
	ret = sprintf(buf, "%u\n", scsi_dev->request_queue->max_sectors);
	up_read(&dev_info->bounce_sem);
	return ret;
}

static ssize_t ps3_stor_set_max_sectors(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct scsi_device *scsi_dev = from_dev_to_scsi_device(dev);
	struct ps3_stor_dev_info * dev_info;
	struct ps3_stor_lv1_dev_info * lv1_dev_info;
	unsigned short max_sectors;
	void * bounce_buf;

	if (sscanf(buf, "%hu", &max_sectors) > 0 && max_sectors <= SCSI_DEFAULT_MAX_SECTORS) {
		dev_info = (struct ps3_stor_dev_info *)scsi_dev->hostdata;
		lv1_dev_info = dev_info->lv1_dev_info;
		/* if dedicated dma region, refuse to reset buffer */
		if (need_dedicated_dma_region(lv1_dev_info->device_type)) {
			/* FIXME: need remap dma region !!! */
			return -EINVAL;
		}
		down_write(&dev_info->bounce_sem);
		if (dev_info->bounce_type == DEDICATED_KMALLOC) {
			/* try to allocate new bounce buffer */
			bounce_buf = kmalloc(max_sectors * lv1_dev_info->sector_size, GFP_NOIO | __GFP_DMA | __GFP_NOWARN);
			if (!bounce_buf) {
				up_write(&dev_info->bounce_sem);
				return -ENOMEM;
			}
			kfree(dev_info->bounce_buf);
			dev_info->bounce_buf = bounce_buf;
			dev_info->dedicated_bounce_size = max_sectors * lv1_dev_info->sector_size;
		}
		blk_queue_max_sectors(scsi_dev->request_queue, max_sectors);
		up_write(&dev_info->bounce_sem);
		return strlen(buf);
	}
	return -EINVAL;
}


static DEVICE_ATTR(max_sectors, S_IRUGO | S_IWUSR, ps3_stor_get_max_sectors,
		   ps3_stor_set_max_sectors);

static struct device_attribute *ps3_stor_sysfs_device_attr_list[] = {
	&dev_attr_max_sectors,
	NULL,
};

static struct scsi_host_template ps3_stor_driver_template = {
	.name =			"ps3_stor",
	.slave_alloc =		ps3_stor_slave_alloc,
	.slave_configure =	ps3_stor_slave_configure,
	.slave_destroy =	ps3_stor_slave_destroy,
	.queuecommand =		ps3_stor_queuecommand,
	.eh_host_reset_handler = ps3_stor_host_reset,
	.can_queue =		PS3_STOR_CANQUEUE,
	.this_id =		7,
	.sg_tablesize =		SG_ALL,
	.cmd_per_lun =		1,
	.emulated =             1,   /* only sg driver uses this       */
	.max_sectors =		128, /* multiple of pagesize, reset later */
	.unchecked_isa_dma =	0,
	.use_clustering =	ENABLE_CLUSTERING,
	.sdev_attrs =           ps3_stor_sysfs_device_attr_list,
	.module =		THIS_MODULE,
};


static int ps3_stor_driver_probe(struct platform_device * dev)
{
        int error = 0;
        struct ps3_stor_host_info *host_info;
        struct ps3_stor_lv1_bus_info *lv1_bus_info;
        struct Scsi_Host *scsi_host;

	host_info = from_dev_to_ps3_stor_host(dev);
	lv1_bus_info = host_info->lv1_bus_info;

        scsi_host = scsi_host_alloc(&ps3_stor_driver_template,
				    sizeof(struct ps3_stor_host_info*));
        if (NULL == scsi_host) {
                printk(KERN_ERR "%s: scsi_register failed\n", __FUNCTION__);
                error = -ENODEV;
		return error;
        }

        host_info->scsi_host = scsi_host;
	*((struct ps3_stor_host_info **)scsi_host->hostdata) = host_info;

	/*
	 * set maximum id as same as number of child devices
	 */
	scsi_host->max_id = lv1_bus_info->devices;
	scsi_host->max_lun = 8;

        error = scsi_add_host(scsi_host, &host_info->dev.dev);

        if (error) {
                printk(KERN_ERR "%s: scsi_add_host failed\n", __FUNCTION__);
                error = -ENODEV;
		scsi_host_put(scsi_host);
        } else {
		scsi_scan_host(scsi_host);
	}


        return error;
}

static int ps3_stor_driver_remove(struct platform_device * dev)
{
        struct list_head *lh, *lh_sf;
        struct ps3_stor_host_info *host_info;
        struct ps3_stor_dev_info *dev_info;

	host_info = from_dev_to_ps3_stor_host(dev);

	if (!host_info) {
		printk(KERN_ERR "%s: Unable to locate host info\n",
		       __FUNCTION__);
		return -ENODEV;
	}

        scsi_remove_host(host_info->scsi_host);

        list_for_each_safe(lh, lh_sf, &host_info->dev_info_list) {
                dev_info = list_entry(lh, struct ps3_stor_dev_info, dev_list);
                list_del(&dev_info->dev_list);
                kfree(dev_info);
        }

        scsi_host_put(host_info->scsi_host);

        return 0;
}

static void ps3_stor_driver_shutdown(struct platform_device * dev)
{
	ps3_stor_driver_remove(dev);
}


static struct platform_driver ps3_stor_platform_driver = {
	.driver = {
		.name = "ps3_stor"
	},
	.probe          = ps3_stor_driver_probe,
	.remove         = ps3_stor_driver_remove,
	.shutdown       = ps3_stor_driver_shutdown
};

static int __init ps3_stor_init(void)
{
	int host_to_add;
	unsigned int devices, index;

	FUNC_START;

	/* register this driver thru devfs */
	platform_driver_register(&ps3_stor_platform_driver);

	/* wait until expected number of devices becomes ready */
	devices = ps3_stor_wait_device_ready();
	if (devices <= 0)
		return -ENODEV;

	/* init lv1_bus_info */
	for (index = 0; index < PS3_STORAGE_NUM_OF_BUS_TYPES; index++) {
		ps3_stor_lv1_bus_info_array[index].bus_type = index;
		INIT_LIST_HEAD(&(ps3_stor_lv1_bus_info_array[index].dev_list));
	}

	/* alloc lv1_dev_info for devices */
	ps3_stor_lv1_dev_info_array =
		kzalloc(sizeof(struct ps3_stor_lv1_dev_info) * devices,
			GFP_KERNEL);

	if (!ps3_stor_lv1_dev_info_array) {
		printk("init failed\n");
		goto clean;
	}
	for (index = 0; index < devices; index++) {
		INIT_LIST_HEAD(&(ps3_stor_lv1_dev_info_array[index].bus_dev_list));
	}

	/* calc how many HBA to add */
	ps3_stor_lv1_devnum = 0;
	devices = ps3_stor_enum_storage_drives();

	for (index = 0; index < devices; index++) {
		struct ps3_stor_lv1_dev_info *dev_info =
			&ps3_stor_lv1_dev_info_array[index];
		if (dev_info->device_type == PS3_DEV_TYPE_STOR_DISK ||
		    dev_info->device_type == PS3_DEV_TYPE_STOR_ROM) {
			if (dev_info->attached_port & (1 << 1)) {
				dev_info->bus_device_index =
					ps3_stor_lv1_bus_info_array[PS3_STORAGE_PATA_1].devices ++;
				list_add_tail(&(dev_info->bus_dev_list),
					      &(ps3_stor_lv1_bus_info_array[PS3_STORAGE_PATA_1].dev_list));
			} else {
				dev_info->bus_device_index =
					ps3_stor_lv1_bus_info_array[PS3_STORAGE_PATA_0].devices ++;
				list_add_tail(&(dev_info->bus_dev_list),
					      &(ps3_stor_lv1_bus_info_array[PS3_STORAGE_PATA_0].dev_list));
			}

		}

		if (dev_info->device_type == PS3_DEV_TYPE_STOR_FLASH) {
			dev_info->bus_device_index =
				ps3_stor_lv1_bus_info_array[PS3_STORAGE_FLASH].devices ++;
			list_add_tail(&(dev_info->bus_dev_list),
				      &(ps3_stor_lv1_bus_info_array[PS3_STORAGE_FLASH].dev_list));
		}

	}

	host_to_add = 0;
	for (index = 0; index < PS3_STORAGE_NUM_OF_BUS_TYPES; index++) {
		if (ps3_stor_lv1_bus_info_array[index].devices)
			host_to_add ++;
	}


        /* add HBAs */
	ps3_stor_add_host = 0;
	for (index = 0; index < PS3_STORAGE_NUM_OF_BUS_TYPES; index++) {
		if (ps3_stor_lv1_bus_info_array[index].devices) {
			if (ps3_stor_add_adapter(&(ps3_stor_lv1_bus_info_array[index]))) {
				printk(KERN_ERR "ps3_stor_init: ps3_stor_add_adapter failed\n");
				break;
			} else
				host_to_add --;
		}
	}

	FUNC_END;
	return 0;

 clean:
	platform_driver_unregister(&ps3_stor_platform_driver);
	return -ENOMEM;
}

static void __exit ps3_stor_exit(void)
{
	int k;

	for (k = ps3_stor_add_host; k; k--)
		ps3_stor_remove_adapter();
	platform_driver_unregister(&ps3_stor_platform_driver);

	for (k = 0; k < ps3_stor_lv1_devnum; k++) {
		if (ps3_stor_lv1_dev_info_array[k].region_info_array) {
			kfree(ps3_stor_lv1_dev_info_array[k].region_info_array);
		}
	}
	kfree(ps3_stor_lv1_dev_info_array);
}


device_initcall(ps3_stor_init);
module_exit(ps3_stor_exit);

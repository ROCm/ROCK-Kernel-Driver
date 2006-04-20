/*
 * Serial Attached SCSI (SAS) Task interface
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * $Id: //depot/sas-class/sas_task.h#27 $
 */

#ifndef _SAS_TASK_H_
#define _SAS_TASK_H_

#include <linux/timer.h>
#include <linux/pci.h>
#include <scsi/sas/sas_discover.h>

/* SAM TMFs */
#define TMF_ABORT_TASK      0x01
#define TMF_ABORT_TASK_SET  0x02
#define TMF_CLEAR_TASK_SET  0x04
#define TMF_LU_RESET        0x08
#define TMF_CLEAR_ACA       0x40
#define TMF_QUERY_TASK      0x80

/* SAS TMF responses */
#define TMF_RESP_FUNC_COMPLETE   0x00
#define TMF_RESP_INVALID_FRAME   0x02
#define TMF_RESP_FUNC_ESUPP      0x04
#define TMF_RESP_FUNC_FAILED     0x05
#define TMF_RESP_FUNC_SUCC       0x08
#define TMF_RESP_NO_LUN          0x09
#define TMF_RESP_OVERLAPPED_TAG  0x0A

/*
      service_response |  SAS_TASK_COMPLETE  |  SAS_TASK_UNDELIVERED |
  exec_status          |                     |                       |
  ---------------------+---------------------+-----------------------+
       SAM_...         |         X           |                       |
       DEV_NO_RESPONSE |         X           |           X           |
       INTERRUPTED     |         X           |                       |
       QUEUE_FULL      |                     |           X           |
       DEVICE_UNKNOWN  |                     |           X           |
       SG_ERR          |                     |           X           |
  ---------------------+---------------------+-----------------------+
 */

enum service_response {
	SAS_TASK_COMPLETE,
	SAS_TASK_UNDELIVERED = -1,
};

enum exec_status {
	SAM_GOOD         = 0,
	SAM_CHECK_COND   = 2,
	SAM_COND_MET     = 4,
	SAM_BUSY         = 8,
	SAM_INTERMEDIATE = 0x10,
	SAM_IM_COND_MET  = 0x12,
	SAM_RESV_CONFLICT= 0x14,
	SAM_TASK_SET_FULL= 0x28,
	SAM_ACA_ACTIVE   = 0x30,
	SAM_TASK_ABORTED = 0x40,

	SAS_DEV_NO_RESPONSE = 0x80,
	SAS_DATA_UNDERRUN,
	SAS_DATA_OVERRUN,
	SAS_INTERRUPTED,
	SAS_QUEUE_FULL,
	SAS_DEVICE_UNKNOWN,
	SAS_SG_ERR,
	SAS_OPEN_REJECT,
	SAS_OPEN_TO,
	SAS_PROTO_RESPONSE,
	SAS_PHY_DOWN,
	SAS_NAK_R_ERR,
	SAS_PENDING,
	SAS_ABORTED_TASK,
};

/* When a task finishes with a response, the LLDD examines the
 * response:
 * 	- For an ATA task task_status_struct::stat is set to
 * SAS_PROTO_RESPONSE, and the task_status_struct::buf is set to the
 * contents of struct ata_task_resp.
 * 	- For SSP tasks, if no data is present or status/TMF response
 * is valid, task_status_struct::stat is set.  If data is present
 * (SENSE data), the LLDD copies up to SAS_STATUS_BUF_SIZE, sets
 * task_status_struct::buf_valid_size, and task_status_struct::stat is
 * set to SAM_CHECK_COND.
 *
 * "buf" has format SCSI Sense for SSP task, or struct ata_task_resp
 * for ATA task.
 *
 * "frame_len" is the total frame length, which could be more or less
 * than actually copied.
 *
 * Tasks ending with response, always set the residual field.
 */
struct ata_task_resp {
	u16  frame_len;
	u8   ending_fis[24];	  /* dev to host or data-in */
	u32  sstatus;
	u32  serror;
	u32  scontrol;
	u32  sactive;
};

#define SAS_STATUS_BUF_SIZE 96

struct task_status_struct {
	enum service_response resp;
	enum exec_status      stat;
	int  buf_valid_size;

	u8   buf[SAS_STATUS_BUF_SIZE];

	u32  residual;
	enum sas_open_rej_reason open_rej_reason;
};

/* ATA and ATAPI task queuable to a SAS LLDD.
 */
struct sas_ata_task {
	struct host_to_dev_fis fis;
	u8     atapi_packet[16];  /* 0 if not ATAPI task */

	u8     retry_count;	  /* hardware retry, should be > 0 */

	u8     dma_xfer:1;	  /* PIO:0 or DMA:1 */
	u8     use_ncq:1;
	u8     set_affil_pol:1;
	u8     stp_affil_pol:1;

	u8     device_control_reg_update:1;
};

struct sas_smp_task {
	struct scatterlist smp_req;
	struct scatterlist smp_resp;
};

enum task_attribute {
	TASK_ATTR_SIMPLE = 0,
	TASK_ATTR_HOQ    = 1,
	TASK_ATTR_ORDERED= 2,
	TASK_ATTR_ACA    = 4,
};

struct sas_ssp_task {
	u8     retry_count;	  /* hardware retry, should be > 0 */

	u8     LUN[8];
	u8     enable_first_burst:1;
	enum   task_attribute task_attr;
	u8     task_prio;
	u8     cdb[16];
};

#define SAS_TASK_STATE_PENDING  1
#define SAS_TASK_STATE_DONE     2
#define SAS_TASK_STATE_ABORTED  4

struct sas_task {
	struct domain_device *dev;
	struct list_head      list;

	spinlock_t   task_state_lock;
	unsigned     task_state_flags;

	enum   sas_proto      task_proto;

	/* Used by the discovery code. */
	struct timer_list     timer;
	struct completion     completion;

	union {
		struct sas_ata_task ata_task;
		struct sas_smp_task smp_task;
		struct sas_ssp_task ssp_task;
	};

	struct scatterlist *scatter;
	int    num_scatter;
	u32    total_xfer_len;
	u8     data_dir:2;	  /* Use PCI_DMA_... */

	struct task_status_struct task_status;
	void   (*task_done)(struct sas_task *task);

	void   *lldd_task;	  /* for use by LLDDs */
	void   *uldd_task;
};

static inline struct sas_task *sas_alloc_task(unsigned long flags)
{
	extern kmem_cache_t *sas_task_cache;
	struct sas_task *task = kmem_cache_alloc(sas_task_cache, flags);

	if (task) {
		memset(task, 0, sizeof(*task));
		INIT_LIST_HEAD(&task->list);
		spin_lock_init(&task->task_state_lock);
		task->task_state_flags = SAS_TASK_STATE_PENDING;
		init_timer(&task->timer);
		init_completion(&task->completion);
	}

	return task;
}

static inline void sas_free_task(struct sas_task *task)
{
	if (task) {
		extern kmem_cache_t *sas_task_cache;
		BUG_ON(!list_empty(&task->list));
		kmem_cache_free(sas_task_cache, task);
	}
}

#endif /* _SAS_TASK_H_ */

/*
 * Copyright (c) 2008, FUJITSU Limited
 *
 * Based on the blkback driver code.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __SCSIIF__BACKEND__COMMON_H__
#define __SCSIIF__BACKEND__COMMON_H__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/pgalloc.h>
#include <asm/delay.h>
#include <xen/evtchn.h>
#include <asm/hypervisor.h>
#include <xen/gnttab.h>
#include <xen/driver_util.h>
#include <xen/xenbus.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/io/vscsiif.h>


#define DPRINTK(_f, _a...)			\
	pr_debug("(file=%s, line=%d) " _f,	\
		 __FILE__ , __LINE__ , ## _a )

struct ids_tuple {
	unsigned int hst;		/* host    */
	unsigned int chn;		/* channel */
	unsigned int tgt;		/* target  */
	unsigned int lun;		/* LUN     */
};

struct v2p_entry {
	struct ids_tuple v;		/* translate from */
	struct scsi_device *sdev;	/* translate to   */
	struct list_head l;
};

struct vscsibk_info {
	struct xenbus_device *dev;

	domid_t domid;
	unsigned int evtchn;
	unsigned int irq;

	int feature;

	struct vscsiif_back_ring  ring;
	struct vm_struct *ring_area;
	grant_handle_t shmem_handle;
	grant_ref_t shmem_ref;

	spinlock_t ring_lock;
	atomic_t nr_unreplied_reqs;

	spinlock_t v2p_lock;
	struct list_head v2p_entry_lists;

	struct task_struct *kthread;
	wait_queue_head_t waiting_to_free;
	wait_queue_head_t wq;
	unsigned int waiting_reqs;
	struct page **mmap_pages;

};

typedef struct {
	unsigned char act;
	struct vscsibk_info *info;
	struct scsi_device *sdev;

	uint16_t rqid;
	
	uint16_t v_chn, v_tgt;

	uint8_t nr_segments;
	uint8_t cmnd[VSCSIIF_MAX_COMMAND_SIZE];
	uint8_t cmd_len;

	uint8_t sc_data_direction;
	uint16_t timeout_per_command;
	
	uint32_t request_bufflen;
	struct scatterlist *sgl;
	grant_ref_t gref[VSCSIIF_SG_TABLESIZE];

	int32_t rslt;
	uint32_t resid;
	uint8_t sense_buffer[VSCSIIF_SENSE_BUFFERSIZE];

	struct list_head free_list;
} pending_req_t;



#define scsiback_get(_b) (atomic_inc(&(_b)->nr_unreplied_reqs))
#define scsiback_put(_b)				\
	do {						\
		if (atomic_dec_and_test(&(_b)->nr_unreplied_reqs))	\
			wake_up(&(_b)->waiting_to_free);\
	} while (0)

#define VSCSIIF_TIMEOUT		(900*HZ)

#define VSCSI_TYPE_HOST		1

irqreturn_t scsiback_intr(int, void *);
int scsiback_init_sring(struct vscsibk_info *info,
		unsigned long ring_ref, unsigned int evtchn);
int scsiback_schedule(void *data);


struct vscsibk_info *vscsibk_info_alloc(domid_t domid);
void scsiback_free(struct vscsibk_info *info);
void scsiback_disconnect(struct vscsibk_info *info);
int __init scsiback_interface_init(void);
void scsiback_interface_exit(void);
int scsiback_xenbus_init(void);
void scsiback_xenbus_unregister(void);

void scsiback_init_translation_table(struct vscsibk_info *info);

int scsiback_add_translation_entry(struct vscsibk_info *info,
			struct scsi_device *sdev, struct ids_tuple *v);

int scsiback_del_translation_entry(struct vscsibk_info *info,
				struct ids_tuple *v);
struct scsi_device *scsiback_do_translation(struct vscsibk_info *info,
			struct ids_tuple *v);
void scsiback_release_translation_entry(struct vscsibk_info *info);


void scsiback_cmd_exec(pending_req_t *pending_req);
void scsiback_do_resp_with_sense(char *sense_buffer, int32_t result,
			uint32_t resid, pending_req_t *pending_req);
void scsiback_fast_flush_area(pending_req_t *req);

void scsiback_rsp_emulation(pending_req_t *pending_req);
void scsiback_req_emulation_or_cmdexec(pending_req_t *pending_req);
void scsiback_emulation_init(void);


#endif /* __SCSIIF__BACKEND__COMMON_H__ */

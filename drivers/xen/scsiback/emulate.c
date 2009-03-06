/*
 * Xen SCSI backend driver
 *
 * Copyright (c) 2008, FUJITSU Limited
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

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include "common.h"

/* Following SCSI commands are not defined in scsi/scsi.h */
#define EXTENDED_COPY		0x83	/* EXTENDED COPY command        */
#define REPORT_ALIASES		0xa3	/* REPORT ALIASES command       */
#define CHANGE_ALIASES		0xa4	/* CHANGE ALIASES command       */
#define SET_PRIORITY		0xa4	/* SET PRIORITY command         */


/*
  The bitmap in order to control emulation.
  (Bit 3 to 7 are reserved for future use.)
*/
#define VSCSIIF_NEED_CMD_EXEC		0x01	/* If this bit is set, cmd exec	*/
						/* is required.			*/
#define VSCSIIF_NEED_EMULATE_REQBUF	0x02	/* If this bit is set, need	*/
						/* emulation reqest buff before	*/
						/* cmd exec.			*/
#define VSCSIIF_NEED_EMULATE_RSPBUF	0x04	/* If this bit is set, need	*/
						/* emulation resp buff after	*/
						/* cmd exec.			*/

/* Additional Sense Code (ASC) used */
#define NO_ADDITIONAL_SENSE		0x0
#define LOGICAL_UNIT_NOT_READY		0x4
#define UNRECOVERED_READ_ERR		0x11
#define PARAMETER_LIST_LENGTH_ERR	0x1a
#define INVALID_OPCODE			0x20
#define ADDR_OUT_OF_RANGE		0x21
#define INVALID_FIELD_IN_CDB		0x24
#define INVALID_FIELD_IN_PARAM_LIST	0x26
#define POWERON_RESET			0x29
#define SAVING_PARAMS_UNSUP		0x39
#define THRESHOLD_EXCEEDED		0x5d
#define LOW_POWER_COND_ON		0x5e



/* Number os SCSI op_code	*/
#define VSCSI_MAX_SCSI_OP_CODE		256
static unsigned char bitmap[VSCSI_MAX_SCSI_OP_CODE];



/*
  Emulation routines for each SCSI op_code.
*/
static void (*pre_function[VSCSI_MAX_SCSI_OP_CODE])(pending_req_t *, void *);
static void (*post_function[VSCSI_MAX_SCSI_OP_CODE])(pending_req_t *, void *);


static const int check_condition_result =
		(DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

static void scsiback_mk_sense_buffer(uint8_t *data, uint8_t key,
			uint8_t asc, uint8_t asq)
{
	data[0] = 0x70;  /* fixed, current */
	data[2] = key;
	data[7] = 0xa;	  /* implies 18 byte sense buffer */
	data[12] = asc;
	data[13] = asq;
}

static void resp_not_supported_cmd(pending_req_t *pending_req, void *data)
{
	scsiback_mk_sense_buffer(pending_req->sense_buffer, ILLEGAL_REQUEST,
		INVALID_OPCODE, 0);
	pending_req->resid = 0;
	pending_req->rslt  = check_condition_result;
}


static int __copy_to_sg(struct scatterlist *sgl, unsigned int nr_sg,
	       void *buf, unsigned int buflen)
{
	struct scatterlist *sg;
	void *from = buf;
	void *to;
	unsigned int from_rest = buflen;
	unsigned int to_capa;
	unsigned int copy_size = 0;
	unsigned int i;
	unsigned long pfn;

	for_each_sg (sgl, sg, nr_sg, i) {
		if (sg_page(sg) == NULL) {
			printk(KERN_WARNING "%s: inconsistent length field in "
			       "scatterlist\n", __FUNCTION__);
			return -ENOMEM;
		}

		to_capa  = sg->length;
		copy_size = min_t(unsigned int, to_capa, from_rest);

		pfn = page_to_pfn(sg_page(sg));
		to = pfn_to_kaddr(pfn) + (sg->offset);
		memcpy(to, from, copy_size);

		from_rest  -= copy_size;
		if (from_rest == 0) {
			return 0;
		}
		
		from += copy_size;
	}

	printk(KERN_WARNING "%s: no space in scatterlist\n",
	       __FUNCTION__);
	return -ENOMEM;
}

static int __copy_from_sg(struct scatterlist *sgl, unsigned int nr_sg,
		 void *buf, unsigned int buflen)
{
	struct scatterlist *sg;
	void *from;
	void *to = buf;
	unsigned int from_rest;
	unsigned int to_capa = buflen;
	unsigned int copy_size;
	unsigned int i;
	unsigned long pfn;

	for_each_sg (sgl, sg, nr_sg, i) {
		if (sg_page(sg) == NULL) {
			printk(KERN_WARNING "%s: inconsistent length field in "
			       "scatterlist\n", __FUNCTION__);
			return -ENOMEM;
		}

		from_rest = sg->length;
		if ((from_rest > 0) && (to_capa < from_rest)) {
			printk(KERN_WARNING
			       "%s: no space in destination buffer\n",
			       __FUNCTION__);
			return -ENOMEM;
		}
		copy_size = from_rest;

		pfn = page_to_pfn(sg_page(sg));
		from = pfn_to_kaddr(pfn) + (sg->offset);
		memcpy(to, from, copy_size);

		to_capa  -= copy_size;
		to += copy_size;
	}

	return 0;
}

static int __nr_luns_under_host(struct vscsibk_info *info)
{
	struct v2p_entry *entry;
	struct list_head *head = &(info->v2p_entry_lists);
	unsigned long flags;
	int lun_cnt = 0;

	spin_lock_irqsave(&info->v2p_lock, flags);
	list_for_each_entry(entry, head, l) {
			lun_cnt++;
	}
	spin_unlock_irqrestore(&info->v2p_lock, flags);

	return (lun_cnt);
}


/* REPORT LUNS Define*/
#define VSCSI_REPORT_LUNS_HEADER	8
#define VSCSI_REPORT_LUNS_RETRY		3

/* quoted scsi_debug.c/resp_report_luns() */
static void __report_luns(pending_req_t *pending_req, void *data)
{
	struct vscsibk_info *info   = pending_req->info;
	unsigned int        channel = pending_req->sdev->channel;
	unsigned int        target  = pending_req->sdev->id;
	unsigned int        nr_seg  = pending_req->nr_segments;
	unsigned char *cmd = (unsigned char *)pending_req->cmnd;
	
	unsigned char *buff = NULL;
	unsigned char alloc_len;
	unsigned int alloc_luns = 0;
	unsigned int req_bufflen = 0;
	unsigned int actual_len = 0;
	unsigned int retry_cnt = 0;
	int select_report = (int)cmd[2];
	int i, lun_cnt = 0, lun, upper, err = 0;
	
	struct v2p_entry *entry;
	struct list_head *head = &(info->v2p_entry_lists);
	unsigned long flags;
	
	struct scsi_lun *one_lun;

	req_bufflen = cmd[9] + (cmd[8] << 8) + (cmd[7] << 16) + (cmd[6] << 24);
	if ((req_bufflen < 4) || (select_report != 0))
		goto fail;

	alloc_luns = __nr_luns_under_host(info);
	alloc_len  = sizeof(struct scsi_lun) * alloc_luns
				+ VSCSI_REPORT_LUNS_HEADER;
retry:
	if ((buff = kmalloc(alloc_len, GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "scsiback:%s kmalloc err\n", __FUNCTION__);
		goto fail;
	}

	memset(buff, 0, alloc_len);

	one_lun = (struct scsi_lun *) &buff[8];
	spin_lock_irqsave(&info->v2p_lock, flags);
	list_for_each_entry(entry, head, l) {
		if ((entry->v.chn == channel) &&
		    (entry->v.tgt == target)) {
			
			/* check overflow */
			if (lun_cnt >= alloc_luns) {
				spin_unlock_irqrestore(&info->v2p_lock,
							flags);

				if (retry_cnt < VSCSI_REPORT_LUNS_RETRY) {
					retry_cnt++;
					if (buff)
						kfree(buff);
					goto retry;
				}

				goto fail;
			}

			lun = entry->v.lun;
			upper = (lun >> 8) & 0x3f;
			if (upper)
				one_lun[lun_cnt].scsi_lun[0] = upper;
			one_lun[lun_cnt].scsi_lun[1] = lun & 0xff;
			lun_cnt++;
		}
	}

	spin_unlock_irqrestore(&info->v2p_lock, flags);

	buff[2] = ((sizeof(struct scsi_lun) * lun_cnt) >> 8) & 0xff;
	buff[3] = (sizeof(struct scsi_lun) * lun_cnt) & 0xff;

	actual_len = lun_cnt * sizeof(struct scsi_lun) 
				+ VSCSI_REPORT_LUNS_HEADER;
	req_bufflen = 0;
	for (i = 0; i < nr_seg; i++)
		req_bufflen += pending_req->sgl[i].length;

	err = __copy_to_sg(pending_req->sgl, nr_seg, buff, 
				min(req_bufflen, actual_len));
	if (err)
		goto fail;

	memset(pending_req->sense_buffer, 0, VSCSIIF_SENSE_BUFFERSIZE);
	pending_req->rslt = 0x00;
	pending_req->resid = req_bufflen - min(req_bufflen, actual_len);

	kfree(buff);
	return;

fail:
	scsiback_mk_sense_buffer(pending_req->sense_buffer, ILLEGAL_REQUEST,
		INVALID_FIELD_IN_CDB, 0);
	pending_req->rslt  = check_condition_result;
	pending_req->resid = 0;
	if (buff)
		kfree(buff);
	return;
}



int __pre_do_emulation(pending_req_t *pending_req, void *data)
{
	uint8_t op_code = pending_req->cmnd[0];

	if ((bitmap[op_code] & VSCSIIF_NEED_EMULATE_REQBUF) &&
	    pre_function[op_code] != NULL) {
		pre_function[op_code](pending_req, data);
	}

	/*
	    0: no need for native driver call, so should return immediately.
	    1: non emulation or should call native driver 
	       after modifing the request buffer.
	*/
	return !!(bitmap[op_code] & VSCSIIF_NEED_CMD_EXEC);
}

void scsiback_rsp_emulation(pending_req_t *pending_req)
{
	uint8_t op_code = pending_req->cmnd[0];

	if ((bitmap[op_code] & VSCSIIF_NEED_EMULATE_RSPBUF) &&
	    post_function[op_code] != NULL) {
		post_function[op_code](pending_req, NULL);
	}

	return;
}


void scsiback_req_emulation_or_cmdexec(pending_req_t *pending_req)
{
	if (__pre_do_emulation(pending_req, NULL)) {
		scsiback_cmd_exec(pending_req);
	}
	else {
		scsiback_fast_flush_area(pending_req);
		scsiback_do_resp_with_sense(pending_req->sense_buffer,
		  pending_req->rslt, pending_req->resid, pending_req);
	}
}


/*
  Following are not customizable functions.
*/
void scsiback_emulation_init(void)
{
	int i;

	/* Initialize to default state */
	for (i = 0; i < VSCSI_MAX_SCSI_OP_CODE; i++) {
		bitmap[i]        = (VSCSIIF_NEED_EMULATE_REQBUF | 
					VSCSIIF_NEED_EMULATE_RSPBUF);
		pre_function[i]  = resp_not_supported_cmd;
		post_function[i] = NULL;
		/* means,
		   - no need for pre-emulation
		   - no need for post-emulation
		   - call native driver
		*/
	}

	/*
	  Register appropriate functions below as you need.
	  (See scsi/scsi.h for definition of SCSI op_code.)
	*/

	/*
	  This command is Non emulation.
	*/
	bitmap[TEST_UNIT_READY] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[TEST_UNIT_READY] = NULL;
	post_function[TEST_UNIT_READY] = NULL;

	bitmap[REZERO_UNIT] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[REZERO_UNIT] = NULL;
	post_function[REZERO_UNIT] = NULL;

	bitmap[REQUEST_SENSE] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[REQUEST_SENSE] = NULL;
	post_function[REQUEST_SENSE] = NULL;

	bitmap[FORMAT_UNIT] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[FORMAT_UNIT] = NULL;
	post_function[FORMAT_UNIT] = NULL;

	bitmap[READ_BLOCK_LIMITS] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[READ_BLOCK_LIMITS] = NULL;
	post_function[READ_BLOCK_LIMITS] = NULL;

	bitmap[READ_6] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[READ_6] = NULL;
	post_function[READ_6] = NULL;

	bitmap[WRITE_6] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[WRITE_6] = NULL;
	post_function[WRITE_6] = NULL;

	bitmap[WRITE_FILEMARKS] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[WRITE_FILEMARKS] = NULL;
	post_function[WRITE_FILEMARKS] = NULL;

	bitmap[SPACE] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[SPACE] = NULL;
	post_function[SPACE] = NULL;

	bitmap[INQUIRY] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[INQUIRY] = NULL;
	post_function[INQUIRY] = NULL;

	bitmap[ERASE] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[ERASE] = NULL;
	post_function[ERASE] = NULL;

	bitmap[MODE_SENSE] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[MODE_SENSE] = NULL;
	post_function[MODE_SENSE] = NULL;

	bitmap[SEND_DIAGNOSTIC] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[SEND_DIAGNOSTIC] = NULL;
	post_function[SEND_DIAGNOSTIC] = NULL;

	bitmap[READ_CAPACITY] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[READ_CAPACITY] = NULL;
	post_function[READ_CAPACITY] = NULL;

	bitmap[READ_10] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[READ_10] = NULL;
	post_function[READ_10] = NULL;

	bitmap[WRITE_10] = VSCSIIF_NEED_CMD_EXEC;
	pre_function[WRITE_10] = NULL;
	post_function[WRITE_10] = NULL;

	/*
	  This command is Full emulation.
	*/
	pre_function[REPORT_LUNS] = __report_luns;
	bitmap[REPORT_LUNS] = (VSCSIIF_NEED_EMULATE_REQBUF | 
					VSCSIIF_NEED_EMULATE_RSPBUF);

	return;
}

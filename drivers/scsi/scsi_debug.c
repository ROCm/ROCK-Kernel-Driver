/* $Id: scsi_debug.c,v 1.1 1992/07/24 06:27:38 root Exp root $
 *  linux/kernel/scsi_debug.c
 *
 *  Copyright (C) 1992  Eric Youngdale
 *  Simulate a host adapter with 2 disks attached.  Do a lot of checking
 *  to make sure that we are not getting blocks mixed up, and PANIC if
 *  anything out of the ordinary is seen.
 *
 *  This version is more generic, simulating a variable number of disk 
 *  (or disk like devices) sharing a common amount of RAM (default 8 MB
 *  but can be set at driver/module load time).
 *
 *  For documentation see http://www.torque.net/sg/sdebug.html
 *
 *   D. Gilbert (dpg) work for MOD device test [20010421]
 *   dpg, work for devfs large number of disks [20010809]
 *   dpg, make more generic [20011123]
 *   dpg, forked for lk 2.5 series [20011216, 20020101]
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/io.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"

#include<linux/stat.h>

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

static char scsi_debug_version_str[] = "Version: 1.57 (20011216)";

#ifndef SCSI_CMD_READ_16
#define SCSI_CMD_READ_16 0x88
#endif
#ifndef SCSI_CMD_WRITE_16
#define SCSI_CMD_WRITE_16 0x8a
#endif

/* A few options that we want selected */
#define DEF_NR_FAKE_DEVS   1
#define DEF_DEV_SIZE_MB   8
#define DEF_FAKE_BLK0   0

static int scsi_debug_num_devs = DEF_NR_FAKE_DEVS;

#define NR_HOSTS_PRESENT (((scsi_debug_num_devs - 1) / 7) + 1)
#define N_HEAD          8
#define N_SECTOR        32
#define DISK_READONLY(TGT)      (0)
#define DISK_REMOVEABLE(TGT)    (0)
#define DEVICE_TYPE(TGT) (TYPE_DISK);

#define SCSI_DEBUG_MAILBOXES (scsi_debug_num_devs + 1)

static int scsi_debug_dev_size_mb = DEF_DEV_SIZE_MB;
#define STORE_SIZE (scsi_debug_dev_size_mb * 1024 * 1024)
#define STORE_ELEM_ORDER 1
#define STORE_ELEM_SIZE (PAGE_SIZE * (1 << STORE_ELEM_ORDER))
#define STORE_ELEMENTS ((STORE_SIZE / STORE_ELEM_SIZE) + 1)

/* default sector size is 512 bytes, 2**9 bytes */
#define POW2_SECT_SIZE 9
#define SECT_SIZE (1 << POW2_SECT_SIZE)

#define N_CYLINDER (STORE_SIZE / (SECT_SIZE * N_SECTOR * N_HEAD))

static int scsi_debug_fake_blk0 = DEF_FAKE_BLK0;

/* Do not attempt to use a timer to simulate a real disk with latency */
/* Only use this in the actual kernel, not in the simulator. */
#define IMMEDIATE

#define SDEBUG_SG_ADDRESS

#define START_PARTITION 4

/* Time to wait before completing a command */
#define DISK_SPEED     (HZ/10)	/* 100ms */
#define CAPACITY (N_HEAD * N_SECTOR * N_CYLINDER)
#define SECT_SIZE_PER(TGT) SECT_SIZE
#define SECT_PER_ELEM (STORE_ELEM_SIZE / SECT_SIZE)

static int starts[] =
{N_SECTOR,
 N_HEAD * N_SECTOR,		/* Single cylinder */
 N_HEAD * N_SECTOR * 4,
 0 /* CAPACITY */, 0};
static int npart = 0;

typedef struct scsi_debug_store_elem {
	unsigned char * p;
} Sd_store_elem;

static Sd_store_elem * store_arr = 0;

typedef struct sdebug_dev_info {
	Scsi_Device * sdp;
	unsigned short host_no;
	unsigned short id;
	char reset;
	char sb_index;
} Sdebug_dev_info;
static Sdebug_dev_info * devInfop;

static int num_aborts = 0;
static int num_dev_resets = 0;
static int num_bus_resets = 0;
static int num_host_resets = 0;

static spinlock_t mailbox_lock = SPIN_LOCK_UNLOCKED;
static rwlock_t sdebug_atomic_rw = RW_LOCK_UNLOCKED;

#include "scsi_debug.h"

typedef void (*done_fct_t) (Scsi_Cmnd *);

static volatile done_fct_t * do_done = 0;

struct Scsi_Host * SHpnt = NULL;

static int scsi_debug_read(Scsi_Cmnd * SCpnt, int upper_blk, int block, 
			   int num, int * errstsp, Sdebug_dev_info * devip);
static int scsi_debug_write(Scsi_Cmnd * SCpnt, int upper_blk, int block, 
			    int num, int * errstsp, Sdebug_dev_info * devip);
static void scsi_debug_send_self_command(struct Scsi_Host * shpnt);
static void scsi_debug_intr_handle(unsigned long);
static Sdebug_dev_info * devInfoReg(Scsi_Device * sdp);
static void mk_sense_buffer(Sdebug_dev_info * devip, int index, int key, 
			    int asc, int asq, int inbandLen);
static int check_reset(Scsi_Cmnd * SCpnt, Sdebug_dev_info * devip);

static struct timer_list * timeout = 0;
static Scsi_Cmnd ** SCint = 0;

/*
 * Semaphore used to simulate bus lockups.
 */
static int scsi_debug_lockup = 0;

#define NUM_SENSE_BUFFS 4
#define SENSE_BUFF_LEN 32
static char sense_buffers[NUM_SENSE_BUFFS][SENSE_BUFF_LEN];

static int made_block0 = 0;

static void scsi_debug_mkblock0(unsigned char * buff, int bufflen,
				Scsi_Cmnd * SCpnt)
{
	int i;
	struct partition *p;

	memset(buff, 0, bufflen);
	*((unsigned short *) (buff + 510)) = 0xAA55;
	p = (struct partition *) (buff + 0x1be);
	i = 0;
	while (starts[i + 1]) {
		int start_cyl, end_cyl;

		start_cyl = starts[i] / N_HEAD / N_SECTOR;
		end_cyl = (starts[i + 1] - 1) / N_HEAD / N_SECTOR;
		p->boot_ind = 0;

		p->head = (i == 0 ? 1 : 0);
		p->sector = 1 | ((start_cyl >> 8) << 6);
		p->cyl = (start_cyl & 0xff);

		p->end_head = N_HEAD - 1;
		p->end_sector = N_SECTOR | ((end_cyl >> 8) << 6);
		p->end_cyl = (end_cyl & 0xff);

		p->start_sect = starts[i];
		p->nr_sects = starts[i + 1] - starts[i];
		p->sys_ind = 0x83;	/* Linux ext2 partition */
		p++;
		i++;
	}
	if (!npart)
		npart = i;
	made_block0 = 1;
	i = (bufflen > STORE_ELEM_SIZE) ? STORE_ELEM_SIZE : bufflen;
	memcpy(store_arr[0].p, buff, i);
}

int scsi_debug_queuecommand(Scsi_Cmnd * SCpnt, void (*done) (Scsi_Cmnd *))
{
	unchar *cmd = (unchar *) SCpnt->cmnd;
	int block;
	int upper_blk;
	unsigned char *buff;
	int scsi_debug_errsts;
	int target = SCpnt->target;
	int bufflen = SCpnt->request_bufflen;
	unsigned long iflags;
	int i, num, capac;
	Sdebug_dev_info * devip = NULL;
	char * sbuff;

	/*
	 * If we are being notified of the mid-level reposessing a command
	 * due to timeout, just return.
	 */
	if (done == NULL) {
		return 0;
	}

	buff = (unsigned char *) SCpnt->request_buffer;

        /*
         * If a command comes for the ID of the host itself, just print
         * a silly message and return.
         */
        if(target == 7) {
                printk("How do you do!\n");
                SCpnt->result = 0;
                done(SCpnt);
                return 0;
        }

	if ((target > 7) || (SCpnt->lun != 0)) {
		SCpnt->result = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
	}
#if 0
	printk(KERN_INFO "sdebug:qc: host_no=%d, id=%d, sdp=%p, cmd=0x%x\n",
	       (int)SCpnt->device->host->host_no, (int)SCpnt->device->id,
	       SCpnt->device, (int)(unsigned char)*cmd);
#endif
	if (NULL == SCpnt->device->hostdata) {
		devip = devInfoReg(SCpnt->device);
		if (NULL == devip) {
			SCpnt->result = DID_NO_CONNECT << 16;
			done(SCpnt);
			return 0;
		}
		SCpnt->device->hostdata = devip;
	}
	devip = SCpnt->device->hostdata;

	switch (*cmd) {
	case REQUEST_SENSE:	/* mandatory */
		SCSI_LOG_LLQUEUE(3, printk("Request sense...\n"));
		if (devip) {
			sbuff = &sense_buffers[(int)devip->sb_index][0];
			devip->sb_index = 0; 
		}
		else
		    sbuff = &sense_buffers[0][0]; 
		memcpy(buff, sbuff, (bufflen < SENSE_BUFF_LEN) ? 
				     bufflen : SENSE_BUFF_LEN);
		memset(sbuff, 0, SENSE_BUFF_LEN);
		sbuff[0] = 0x70;
		SCpnt->result = 0;
		done(SCpnt);
		return 0;
	case START_STOP:
		if (check_reset(SCpnt, devip)) {
			done(SCpnt);
			return 0;
		}
		SCSI_LOG_LLQUEUE(3, printk("START_STOP\n"));
		scsi_debug_errsts = 0;
		break;
	case ALLOW_MEDIUM_REMOVAL:
		if (check_reset(SCpnt, devip)) {
			done(SCpnt);
			return 0;
		}
		if (cmd[4]) {
			SCSI_LOG_LLQUEUE(2, printk(
					"Medium removal inhibited..."));
		} else {
			SCSI_LOG_LLQUEUE(2, 
					printk("Medium removal enabled..."));
		}
		scsi_debug_errsts = 0;
		break;
	case INQUIRY:     /* mandatory */
		SCSI_LOG_LLQUEUE(3, printk("Inquiry...(%p %d)\n", buff, bufflen));
		memset(buff, 0, bufflen);
		buff[0] = DEVICE_TYPE(target);
		buff[1] = DISK_REMOVEABLE(target) ? 0x80 : 0;	
				/* Removable disk */
		buff[2] = 2;	/* claim SCSI 2 */
		buff[4] = 36 - 5;
		memcpy(&buff[8], "Linux   ", 8);
		memcpy(&buff[16], "scsi_debug      ", 16);
		memcpy(&buff[32], "0002", 4);
		scsi_debug_errsts = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,10)
		if (SCpnt->host->max_cmd_len != SCSI_DEBUG_MAX_CMD_LEN)
			SCpnt->host->max_cmd_len = SCSI_DEBUG_MAX_CMD_LEN;
#endif
		break;
	case SEND_DIAGNOSTIC:     /* mandatory */
		SCSI_LOG_LLQUEUE(3, printk("Send Diagnostic\n"));
		if (buff)
			memset(buff, 0, bufflen);
		scsi_debug_errsts = 0;
		break;
	case TEST_UNIT_READY:     /* mandatory */
		SCSI_LOG_LLQUEUE(3, printk("Test unit ready(%p %d)\n", buff, bufflen));
		if (buff)
			memset(buff, 0, bufflen);
		scsi_debug_errsts = 0;
		break;
	case READ_CAPACITY:
		if (check_reset(SCpnt, devip)) {
			done(SCpnt);
			return 0;
		}
		SCSI_LOG_LLQUEUE(3, printk("Read Capacity\n"));
                SHpnt = SCpnt->host;
		memset(buff, 0, bufflen);
		capac = CAPACITY - 1;
		buff[0] = (capac >> 24);
		buff[1] = (capac >> 16) & 0xff;
		buff[2] = (capac >> 8) & 0xff;
		buff[3] = capac & 0xff;
		buff[4] = 0;
		buff[5] = 0;
		buff[6] = (SECT_SIZE_PER(target) >> 8) & 0xff;
		buff[7] = SECT_SIZE_PER(target) & 0xff;

		scsi_debug_errsts = 0;
		break;
	case SCSI_CMD_READ_16:	/* SBC-2 */
	case READ_12:
	case READ_10:
	case READ_6:
		if (check_reset(SCpnt, devip)) {
			done(SCpnt);
			return 0;
		}
		upper_blk = 0;
		if ((*cmd) == SCSI_CMD_READ_16) {
			upper_blk = cmd[5] + (cmd[4] << 8) + 
				    (cmd[3] << 16) + (cmd[2] << 24);
			block = cmd[9] + (cmd[8] << 8) + 
				(cmd[7] << 16) + (cmd[6] << 24);
			num = cmd[13] + (cmd[12] << 8) + 
				(cmd[11] << 16) + (cmd[10] << 24);
		}
		else if ((*cmd) == READ_12) {
			block = cmd[5] + (cmd[4] << 8) + 
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[9] + (cmd[8] << 8) + 
				(cmd[7] << 16) + (cmd[6] << 24);
		}
		else if ((*cmd) == READ_10) {
			block = cmd[5] + (cmd[4] << 8) + 
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[8] + (cmd[7] << 8);
		}
		else {
			block = cmd[3] + (cmd[2] << 8) + 
				((cmd[1] & 0x1f) << 16);
			num = cmd[4];
		}
		if (scsi_debug_read(SCpnt, upper_blk, block, num, 
				    &scsi_debug_errsts, devip))
			break;
		SCpnt->result = 0;
/* calls bottom half in upper layers before return from scsi_do_...() */
		(done) (SCpnt);	
		return 0;
	case SCSI_CMD_WRITE_16:	/* SBC-2 */
	case WRITE_12:
	case WRITE_10:
	case WRITE_6:
		if (check_reset(SCpnt, devip)) {
			done(SCpnt);
			return 0;
		}
		upper_blk = 0;
		if ((*cmd) == SCSI_CMD_WRITE_16) {
			upper_blk = cmd[5] + (cmd[4] << 8) + 
				    (cmd[3] << 16) + (cmd[2] << 24);
			block = cmd[9] + (cmd[8] << 8) + 
				(cmd[7] << 16) + (cmd[6] << 24);
			num = cmd[13] + (cmd[12] << 8) + 
				(cmd[11] << 16) + (cmd[10] << 24);
		}
		else if ((*cmd) == WRITE_12) {
			block = cmd[5] + (cmd[4] << 8) + 
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[9] + (cmd[8] << 8) + 
				(cmd[7] << 16) + (cmd[6] << 24);
		}
		else if ((*cmd) == WRITE_10) {
			block = cmd[5] + (cmd[4] << 8) + 
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[8] + (cmd[7] << 8);
		}
		else {
			block = cmd[3] + (cmd[2] << 8) + 
				((cmd[1] & 0x1f) << 16);
			num = cmd[4];
		}
		if (scsi_debug_write(SCpnt, upper_blk, block, num, 
				     &scsi_debug_errsts, devip))
			break;
		SCpnt->result = 0;
/* calls bottom half in upper layers before return from scsi_do_...() */
		(done) (SCpnt);	
		return 0;
	case MODE_SENSE:
		/*
		 * Used to detect write protected status.
		 */
		scsi_debug_errsts = 0;
		memset(buff, 0, 6);
		break;
	default:
#if 0
		SCSI_LOG_LLQUEUE(3, printk("Unknown command %d\n", *cmd));
		SCpnt->result = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
#else
		if (check_reset(SCpnt, devip)) {
			done(SCpnt);
			return 0;
		}
		scsi_debug_errsts = (COMMAND_COMPLETE << 8) |
				    (CHECK_CONDITION << 1);
		mk_sense_buffer(devip, 2, ILLEGAL_REQUEST, 0x20, 0, 14);
		break;
#endif
	}

	spin_lock_irqsave(&mailbox_lock, iflags);
	for (i = 0; i < SCSI_DEBUG_MAILBOXES; i++) {
		if (timeout[i].function == NULL)
			break;
	}

	/*
	 * If all of the slots are full, just return 1.  The new error 
	 * handling scheme allows this, and the mid-level should queue things.
	 */
	if (i >= SCSI_DEBUG_MAILBOXES || timeout[i].function != 0) {
		SCSI_LOG_LLQUEUE(1, printk("Command rejected - host busy\n"));
		spin_unlock_irqrestore(&mailbox_lock, iflags);
		return 1;
	}
	SCSI_LOG_LLQUEUE(1, printk("Command accepted - slot %d\n", i));

#ifdef IMMEDIATE
	if (!scsi_debug_lockup) {
		SCpnt->result = scsi_debug_errsts;
		SCint[i] = SCpnt;
		do_done[i] = done;
		scsi_debug_intr_handle(i);	/* No timer - do this one right away */
	}
	spin_unlock_irqrestore(&mailbox_lock, iflags);
#else

	SCpnt->result = scsi_debug_errsts;
	timeout[i].function = scsi_debug_intr_handle;
	timeout[i].data = i;
	timeout[i].expires = jiffies + DISK_SPEED;
	SCint[i] = SCpnt;
	do_done[i] = done;

	spin_unlock_irqrestore(&mailbox_lock, iflags);
	add_timer(&timeout[i]);
	if (!done)
		printk("scsi_debug_queuecommand: done can't be NULL\n");

#if 0
	printk("Sending command (%d %x %d %d)...", i, done, 
	       timeout[i].expires, jiffies);
#endif
#endif

	return 0;
}

static int check_reset(Scsi_Cmnd * SCpnt, Sdebug_dev_info * devip)
{
	if (devip->reset) {
		devip->reset = 0;
		mk_sense_buffer(devip, 3, UNIT_ATTENTION, 0x29, 0, 14);
		SCpnt->result = (COMMAND_COMPLETE << 8) | 
				(CHECK_CONDITION << 1);
                return 1;
	}
	return 0;
}

static inline 
unsigned char * sdebug_scatg2virt(const struct scatterlist * sclp)
{
	if (NULL == sclp)
		return NULL;
	else if (sclp->page)
		return (unsigned char *)page_address(sclp->page) + 
		       sclp->offset;
	else {
#ifdef SDEBUG_SG_ADDRESS
		return sclp->address;
#else
		return NULL;
#endif
	}
}

static int scsi_debug_read(Scsi_Cmnd * SCpnt, int upper_blk, int block, 
			   int num, int * errstsp, Sdebug_dev_info * devip)
{
        unsigned char *buff = (unsigned char *) SCpnt->request_buffer;
        int nbytes, sgcount;
        struct scatterlist *sgpnt = NULL;
        int bufflen = SCpnt->request_bufflen;
	unsigned long iflags;

	if (upper_blk || (block + num > CAPACITY)) {
		*errstsp = (COMMAND_COMPLETE << 8) |
			   (CHECK_CONDITION << 1);
		mk_sense_buffer(devip, 1, ILLEGAL_REQUEST, 0x21, 0, 14);
		return 1;
	}
#if defined(SCSI_SETUP_LATENCY) || defined(SCSI_DATARATE)
	{
		int delay = SCSI_SETUP_LATENCY;

		delay += SCpnt->request.nr_sectors * SCSI_DATARATE;
		if (delay)
			usleep(delay);
	}
#endif

	read_lock_irqsave(&sdebug_atomic_rw, iflags);
        sgcount = 0;
	nbytes = bufflen;
	/* printk("scsi_debug_read: block=%d, tot_bufflen=%d\n", 
	       block, bufflen); */
	if (SCpnt->use_sg) {
		sgcount = 0;
		sgpnt = (struct scatterlist *) buff;
		buff = sdebug_scatg2virt(&sgpnt[sgcount]);
		bufflen = sgpnt[sgcount].length;
	}
	*errstsp = 0;
	do {
		int resid, k, off, len, rem, blk;
		unsigned char * bp;

		/* If this is block 0, then we want to read the partition
		 * table for this device.  Let's make one up */
		if (scsi_debug_fake_blk0 && (block == 0) && (! made_block0)) {
			scsi_debug_mkblock0(buff, bufflen, SCpnt);
			*errstsp = 0;
			break;
		}
		bp = buff;
		blk = block;
		for (resid = bufflen; resid > 0; resid -= len) {
			k = blk / SECT_PER_ELEM; 
			off = (blk % SECT_PER_ELEM) * SECT_SIZE;
			rem = STORE_ELEM_SIZE - off;
			len = (resid > rem) ? rem : resid;
/* printk("sdr: blk=%d k=%d off=%d rem=%d resid"
			       "=%d len=%d  sgcount=%d\n", blk, k, 
				off, rem, resid, len, sgcount); */
			memcpy(bp, store_arr[k].p + off, len);
			bp += len;
			blk += len / SECT_SIZE;
		}
#if 0
		/* Simulate a disk change */
		if (block == 0xfff0) {
			sense_buffer[0] = 0x70;
			sense_buffer[2] = UNIT_ATTENTION;
			starts[0] += 10;
			starts[1] += 10;
			starts[2] += 10;

			*errstsp = (COMMAND_COMPLETE << 8) | 
				   (CHECK_CONDITION << 1);
			read_unlock_irqrestore(&sdebug_atomic_rw, iflags);
			return 1;
		}	/* End phony disk change code */
#endif
		nbytes -= bufflen;
		if (SCpnt->use_sg) {
			block += bufflen >> POW2_SECT_SIZE;
			sgcount++;
			if (nbytes) {
				buff = sdebug_scatg2virt(&sgpnt[sgcount]);
				bufflen = sgpnt[sgcount].length;
			}
		}
		else if (nbytes > 0)
			printk("sdebug_read: unexpected nbytes=%d\n", nbytes);
	} while (nbytes);
	read_unlock_irqrestore(&sdebug_atomic_rw, iflags);
	return 0;
}

static int scsi_debug_write(Scsi_Cmnd * SCpnt, int upper_blk, int block, 
			    int num, int * errstsp, Sdebug_dev_info * devip)
{
        unsigned char *buff = (unsigned char *) SCpnt->request_buffer;
        int nbytes, sgcount;
        struct scatterlist *sgpnt = NULL;
        int bufflen = SCpnt->request_bufflen;
	unsigned long iflags;

	if (upper_blk || (block + num > CAPACITY)) {
		*errstsp = (COMMAND_COMPLETE << 8) |
			   (CHECK_CONDITION << 1);
		mk_sense_buffer(devip, 1, ILLEGAL_REQUEST, 0x21, 0, 14);
		return 1;
	}

	write_lock_irqsave(&sdebug_atomic_rw, iflags);
        sgcount = 0;
	nbytes = bufflen;
	if (SCpnt->use_sg) {
		sgcount = 0;
		sgpnt = (struct scatterlist *) buff;
		buff = sdebug_scatg2virt(&sgpnt[sgcount]);
		bufflen = sgpnt[sgcount].length;
	}
	*errstsp = 0;
	do {
		int resid, k, off, len, rem, blk;
		unsigned char * bp;

		bp = buff;
		blk = block;
		for (resid = bufflen; resid > 0; resid -= len) {
			k = blk / SECT_PER_ELEM; 
			off = (blk % SECT_PER_ELEM) * SECT_SIZE;
			rem = STORE_ELEM_SIZE - off;
			len = (resid > rem) ? rem : resid;
			memcpy(store_arr[k].p + off, bp, len);
			bp += len;
			blk += len / SECT_SIZE;
		}

		nbytes -= bufflen;
		if (SCpnt->use_sg) {
			block += bufflen >> POW2_SECT_SIZE;
			sgcount++;
			if (nbytes) {
				buff = sdebug_scatg2virt(&sgpnt[sgcount]);
				bufflen = sgpnt[sgcount].length;
			}
		}
		else if (nbytes > 0)
			printk("sdebug_write: unexpected nbytes=%d\n", nbytes);
	} while (nbytes);
	write_unlock_irqrestore(&sdebug_atomic_rw, iflags);
	return 0;
}

static void scsi_debug_send_self_command(struct Scsi_Host * shpnt)
{
	static unsigned char cmd[6] =
	{TEST_UNIT_READY, 0, 0, 0, 0, 0};

        Scsi_Request  * scp;
        Scsi_Device   * sdev;
        
        printk("Allocating host dev\n");
        sdev = scsi_get_host_dev(shpnt);
        if(sdev==NULL)
        {
        	printk("Out of memory.\n");
        	return;
        }
        
        printk("Got %p. Allocating command block\n", sdev);
        scp  = scsi_allocate_request(sdev);
        printk("Got %p\n", scp);
        
        if(scp==NULL)
        {
        	printk("Out of memory.\n");
        	goto bail;
        }

        scp->sr_cmd_len = 6;
        scp->sr_use_sg = 0;
        
        printk("Sending command\n");
        scsi_wait_req (scp, (void *) cmd, (void *) NULL,
                       0, 100, 3);
        
        printk("Releasing command\n");
        scsi_release_request(scp);
bail:
	printk("Freeing device\n");
        scsi_free_host_dev(sdev);
}

/* A "high" level interrupt handler.  This should be called once per jiffy
 * to simulate a regular scsi disk.  We use a timer to do this. */

static void scsi_debug_intr_handle(unsigned long indx)
{
	Scsi_Cmnd *SCtmp;
	void (*my_done) (Scsi_Cmnd *);
#if 0
	del_timer(&timeout[indx]);
#endif

	SCtmp = (Scsi_Cmnd *) SCint[indx];
	my_done = do_done[indx];
	do_done[indx] = NULL;
	timeout[indx].function = NULL;
	SCint[indx] = NULL;

	if (!my_done) {
		printk("scsi_debug_intr_handle: Unexpected interrupt\n");
		return;
	}
#if 0
	printk("In intr_handle...");
	printk("...done %d %x %d %d\n", i, my_done, to, jiffies);
	printk("In intr_handle: %d %x %x\n", i, SCtmp, my_done);
#endif

	my_done(SCtmp);
#if 0
	printk("Called done.\n");
#endif
}

static int initialized = 0;

static int do_init(void)
{
	int sz;

	starts[3] = CAPACITY;
	sz = sizeof(Sd_store_elem) * STORE_ELEMENTS;
	store_arr = kmalloc(sz, GFP_ATOMIC);
	if (NULL == store_arr)
		return 1;
	memset(store_arr, 0, sz);

	sz = sizeof(done_fct_t) * SCSI_DEBUG_MAILBOXES;
	do_done = kmalloc(sz, GFP_ATOMIC);
	if (NULL == do_done)
		goto out;
	memset((void *)do_done, 0, sz);

	sz = sizeof(struct timer_list) * SCSI_DEBUG_MAILBOXES;
	timeout = kmalloc(sz, GFP_ATOMIC);
	if (NULL == timeout)
		goto out;
	memset(timeout, 0, sz);

	sz = sizeof(Scsi_Cmnd *) * SCSI_DEBUG_MAILBOXES;
	SCint = kmalloc(sz, GFP_ATOMIC);
	if (NULL == SCint)
		goto out;
	memset(SCint, 0, sz);

	return 0;

out:
	if (store_arr)
		kfree(store_arr);
	if (do_done)
		kfree((void *)do_done);
	if (timeout)
		kfree(timeout);
	if (SCint)
		kfree(SCint);
	return 1;
}

static void do_end(void)
{
	kfree(SCint);
	kfree(timeout);
	kfree((void *)do_done);
	kfree(store_arr);
}


int scsi_debug_detect(Scsi_Host_Template * tpnt)
{
	int k, num, sz;

	if (0 == initialized) {
		++initialized;
		sz = sizeof(Sdebug_dev_info) * scsi_debug_num_devs;
		devInfop = kmalloc(sz, GFP_ATOMIC);
		if (NULL == devInfop) {
			printk("scsi_debug_detect: out of memory, 0.5\n");
			return 0;
		}
		memset(devInfop, 0, sz);
		if (do_init()) {
			printk("scsi_debug_detect: out of memory, 0\n");
			return 0;
		}
		for (k = 0; k < STORE_ELEMENTS; ++k) {
			store_arr[k].p = (unsigned char *)
				__get_free_pages(GFP_ATOMIC, STORE_ELEM_ORDER);
			if (0 == store_arr[k].p)
				goto detect_err;
			memset(store_arr[k].p, 0, STORE_ELEM_SIZE);
		}
		for (k = 0; k < NUM_SENSE_BUFFS; ++k)
			sense_buffers[k][0] = 0x70;
		for (k = 0; k < NR_HOSTS_PRESENT; k++) {
			tpnt->proc_name = "scsi_debug";	/* In the loop??? */
			scsi_register(tpnt, 0);
		}
		return NR_HOSTS_PRESENT;
	}
	else {
		printk("scsi_debug_detect: called again\n");
		return 0;
	}

detect_err:
	num = k;
	for (k = 0; k < STORE_ELEMENTS; ++k) {
		if (0 != store_arr[k].p) {
			free_pages((unsigned long)store_arr[k].p, 
				   STORE_ELEM_ORDER);
			store_arr[k].p = NULL;
		}
	}
	printk("scsi_debug_detect: out of memory: %d out of %d bytes\n",
	       (int)(num * STORE_ELEM_SIZE), 
	       (int)(scsi_debug_dev_size_mb * 1024 * 1024));
	return 0;
}


static int num_releases = 0;

int scsi_debug_release(struct Scsi_Host * hpnt)
{
	int k;
	
	scsi_unregister(hpnt);
	if (++num_releases != NR_HOSTS_PRESENT)
		return 0;

	for (k = 0; k < STORE_ELEMENTS; ++k) {
		if (0 != store_arr[k].p) {
			free_pages((unsigned long)store_arr[k].p, 
				   STORE_ELEM_ORDER);
			store_arr[k].p = NULL;
		}
	}
	do_end();
	kfree(devInfop);
	return 0;
}

static Sdebug_dev_info * devInfoReg(Scsi_Device * sdp)
{
	int k;
	unsigned short host_no, id;
	Sdebug_dev_info * devip;

	host_no = sdp->host->host_no;
	id = (unsigned short)sdp->id;
	for (k = 0; k < scsi_debug_num_devs; ++k) {
		devip = &devInfop[k];
		if (devip->sdp && (host_no == devip->host_no) &&
		    (id == devip->id)) {
			devip->sdp = sdp; /* overwrite previous sdp */
			return devip;
		}
		if (NULL == devip->sdp) {
			devip->sdp = sdp;
			devip->host_no = host_no;
			devip->id = id;
			devip->reset = 1;
			devip->sb_index = 0;
			return devip;
		}
	}
	return NULL;
}

static void mk_sense_buffer(Sdebug_dev_info * devip, int index, int key, 
			    int asc, int asq, int inbandLen)
{
	char * sbuff;
	if ((index < 0) || (index >= NUM_SENSE_BUFFS))
		return;
	if (devip)
		devip->sb_index = index;
	sbuff = &sense_buffers[index][0];
	memset(sbuff, 0, SENSE_BUFF_LEN);
	sbuff[0] = 0x70;
	sbuff[2] = key;
	sbuff[7] = (inbandLen > 7) ? (inbandLen - 8) : 0;
	sbuff[12] = asc;
	sbuff[13] = asq;
}

int scsi_debug_abort(Scsi_Cmnd * SCpnt)
{
#if 1
	++num_aborts;
	return SUCCESS;
#else
	int j;
	void (*my_done) (Scsi_Cmnd *);
	unsigned long iflags;
	SCpnt->result = SCpnt->abort_reason << 16;
	for (j = 0; j < SCSI_DEBUG_MAILBOXES; j++) {
		if (SCpnt == SCint[j]) {
			my_done = do_done[j];
			my_done(SCpnt);
			spin_lock_irqsave(&mailbox_lock, iflags);
			timeout[j] = 0;
			SCint[j] = NULL;
			do_done[j] = NULL;
			spin_unlock_irqrestore(&mailbox_lock, iflags);
		}
	}
	return SCSI_ABORT_SNOOZE;
#endif
}

int scsi_debug_biosparam(Disk * disk, kdev_t dev, int *info)
{
	/* int size = disk->capacity; */
	info[0] = N_HEAD;
	info[1] = N_SECTOR;
	info[2] = N_CYLINDER;
	if (info[2] >= 1024)
		info[2] = 1024;
	return 0;
}

#if 0
int scsi_debug_reset(Scsi_Cmnd * SCpnt, unsigned int why)
{
	int i;
	unsigned long iflags;

	void (*my_done) (Scsi_Cmnd *);
	printk("Bus unlocked by reset - %d\n", why);
	scsi_debug_lockup = 0;
	for (i = 0; i < SCSI_DEBUG_MAILBOXES; i++) {
		if (SCint[i] == NULL)
			continue;
		SCint[i]->result = DID_RESET << 16;
		my_done = do_done[i];
		my_done(SCint[i]);
		spin_lock_irqsave(&mailbox_lock, iflags);
		SCint[i] = NULL;
		do_done[i] = NULL;
		timeout[i].function = NULL;
		spin_unlock_irqrestore(&mailbox_lock, iflags);
	}
	return SCSI_RESET_SUCCESS;
}
#endif

int scsi_debug_device_reset(Scsi_Cmnd * SCpnt)
{
	Scsi_Device * sdp;
	int k;

	++num_dev_resets;
	if (SCpnt && ((sdp = SCpnt->device))) {
		for (k = 0; k < scsi_debug_num_devs; ++k) {
			if (sdp->hostdata == (devInfop + k))
				break;
		}
		if (k < scsi_debug_num_devs)
			devInfop[k].reset = 1;
	}
	return SUCCESS;
}

int scsi_debug_bus_reset(Scsi_Cmnd * SCpnt)
{
	Scsi_Device * sdp;
	struct Scsi_Host * hp;
	int k;

	++num_bus_resets;
	if (SCpnt && ((sdp = SCpnt->device)) && ((hp = sdp->host))) {
		for (k = 0; k < scsi_debug_num_devs; ++k) {
			if (hp == devInfop[k].sdp->host)
				devInfop[k].reset = 1;
		}
	}
	return SUCCESS;
}

int scsi_debug_host_reset(Scsi_Cmnd * SCpnt)
{
	int k;

	++num_host_resets;
	for (k = 0; k < scsi_debug_num_devs; ++k)
		devInfop[k].reset = 1;

	return SUCCESS;
}

#ifndef MODULE
static int __init scsi_debug_num_devs_setup(char *str)
{   
    int tmp; 
    
    if (get_option(&str, &tmp) == 1) {
        if (tmp > 0)
            scsi_debug_num_devs = tmp;
        return 1;
    } else {
        printk("scsi_debug_num_devs: usage scsi_debug_num_devs=<n> "
               "(<n> can be from 1 to around 2000)\n");
        return 0;
    }
}

__setup("scsi_debug_num_devs=", scsi_debug_num_devs_setup);

static int __init scsi_debug_dev_size_mb_setup(char *str)
{   
    int tmp; 
    
    if (get_option(&str, &tmp) == 1) {
        if (tmp > 0)
            scsi_debug_dev_size_mb = tmp;
        return 1;
    } else {
        printk("scsi_debug_dev_size_mb: usage scsi_debug_dev_size_mb=<n>\n"
               "    (<n> is number of MB ram shared by all devs\n");
        return 0;
    }
}

__setup("scsi_debug_dev_size_mb=", scsi_debug_dev_size_mb_setup);
#endif

MODULE_AUTHOR("Eric Youngdale + Douglas Gilbert");
MODULE_DESCRIPTION("SCSI debug adapter driver");
MODULE_PARM(scsi_debug_num_devs, "i");
MODULE_PARM_DESC(scsi_debug_num_devs, "number of SCSI devices to simulate");
MODULE_PARM(scsi_debug_dev_size_mb, "i");
MODULE_PARM_DESC(scsi_debug_dev_size_mb, "size in MB of ram shared by devs");

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

static char sdebug_info[256];

const char * scsi_debug_info(struct Scsi_Host * shp)
{
	sprintf(sdebug_info, "scsi_debug, %s, num_devs=%d, "
		"dev_size_mb=%d\n", scsi_debug_version_str,
		scsi_debug_num_devs, scsi_debug_dev_size_mb);
	return sdebug_info;
}

/* scsi_debug_proc_info
 * Used if the driver currently has no own support for /proc/scsi
 */
int scsi_debug_proc_info(char *buffer, char **start, off_t offset,
			 int length, int inode, int inout)
{
	int len, pos, begin;
	int orig_length;

	orig_length = length;

	if (inout == 1) {
		/* First check for the Signature */
		if (length >= 10 && strncmp(buffer, "scsi_debug", 10) == 0) {
			buffer += 11;
			length -= 11;

			if (buffer[length - 1] == '\n') {
				buffer[length - 1] = '\0';
				length--;
			}
			/*
			 * OK, we are getting some kind of command.  Figure out
			 * what we are supposed to do here.  Simulate bus lockups
			 * to test our reset capability.
			 */
			if (length == 4 && strncmp(buffer, "test", length) == 0) {
                                printk("Testing send self command %p\n", SHpnt);
                                scsi_debug_send_self_command(SHpnt);
                                return orig_length;
                        }
			if (length == 6 && strncmp(buffer, "lockup", length) == 0) {
				scsi_debug_lockup = 1;
				return orig_length;
			}
			if (length == 6 && strncmp(buffer, "unlock", length) == 0) {
				scsi_debug_lockup = 0;
				return orig_length;
			}
			printk("Unknown command:%s (%d)\n", buffer, length);
		} else
			printk("Wrong Signature:%10s\n", (char *) buffer);

		return -EINVAL;

	}
	begin = 0;
	pos = len = sprintf(buffer, "scsi_debug adapter driver, %s\n"
	    "num_devs=%d, shared (ram) size=%d MB, sector_size=%d bytes\n" 
	    "cylinders=%d, heads=%d, sectors=%d\n"
	    "number of aborts=%d, device_reset=%d, bus_resets=%d, " 
	    "host_resets=%d\n",
	    scsi_debug_version_str, scsi_debug_num_devs, 
	    scsi_debug_dev_size_mb, SECT_SIZE,
	    N_CYLINDER, N_HEAD, N_SECTOR,
	    num_aborts, num_dev_resets, num_bus_resets, num_host_resets);
#if 0
	 "This driver is not a real scsi driver, but it plays one on TV.\n"
	 "It is very handy for debugging specific problems because you\n"
			 "can simulate a variety of error conditions\n");
#endif
	if (pos < offset) {
		len = 0;
		begin = pos;
	}
	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);
	if (len > length)
		len = length;

	return (len);
}

/* Eventually this will go into an include file, but this will be later */
static Scsi_Host_Template driver_template = SCSI_DEBUG_TEMPLATE;

#include "scsi_module.c"


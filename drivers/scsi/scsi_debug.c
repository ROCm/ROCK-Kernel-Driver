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
 *   dpg, forked for lk 2.5 series [20011216, 20020101]
 *   dpg, use vmalloc() more inquiry+mode_sense [20020302]
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
#include <linux/vmalloc.h>

#include <asm/io.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"

#include <linux/stat.h>

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

static char scsi_debug_version_str[] = "Version: 1.59 (20020302)";

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

#define DEF_OPTS   0
#define SCSI_DEBUG_OPT_NOISE   1
#define SCSI_DEBUG_OPT_MEDIUM_ERR   2

#define OPT_MEDIUM_ERR_ADDR   0x1234

static int scsi_debug_num_devs = DEF_NR_FAKE_DEVS;
static int scsi_debug_opts = DEF_OPTS;

#define NR_HOSTS_PRESENT (((scsi_debug_num_devs - 1) / 7) + 1)
#define N_HEAD          8
#define N_SECTOR        32
#define DEV_READONLY(TGT)      (0)
#define DEV_REMOVEABLE(TGT)    (0)
#define DEVICE_TYPE(TGT) (TYPE_DISK);

#define SCSI_DEBUG_MAILBOXES (scsi_debug_num_devs + 1)

static int scsi_debug_dev_size_mb = DEF_DEV_SIZE_MB;
#define STORE_SIZE (scsi_debug_dev_size_mb * 1024 * 1024)

/* default sector size is 512 bytes, 2**9 bytes */
#define POW2_SECT_SIZE 9
#define SECT_SIZE (1 << POW2_SECT_SIZE)

#define N_CYLINDER (STORE_SIZE / (SECT_SIZE * N_SECTOR * N_HEAD))

/* Do not attempt to use a timer to simulate a real disk with latency */
/* Only use this in the actual kernel, not in the simulator. */
#define IMMEDIATE

#define START_PARTITION 4

/* Time to wait before completing a command */
#define DISK_SPEED     (HZ/10)	/* 100ms */
#define CAPACITY (N_HEAD * N_SECTOR * N_CYLINDER)
#define SECT_SIZE_PER(TGT) SECT_SIZE

static int starts[] =
{N_SECTOR,
 N_HEAD * N_SECTOR,		/* Single cylinder */
 N_HEAD * N_SECTOR * 4,
 0 /* CAPACITY */, 0};

static unsigned char * fake_storep;

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

static struct Scsi_Host * SHpnt = NULL;

static int scsi_debug_inquiry(unsigned char * cmd, int target,
			      unsigned char * buff, int bufflen,
			      Sdebug_dev_info * devip);
static int scsi_debug_mode_sense(unsigned char * cmd, int target,
			         unsigned char * buff, int bufflen,
			         Sdebug_dev_info * devip);
static int scsi_debug_read(Scsi_Cmnd * SCpnt, int upper_blk, int block, 
			   int num, int * errstsp, Sdebug_dev_info * devip);
static int scsi_debug_write(Scsi_Cmnd * SCpnt, int upper_blk, int block, 
			    int num, int * errstsp, Sdebug_dev_info * devip);
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


static inline 
unsigned char * sdebug_scatg2virt(const struct scatterlist * sclp)
{
	if (NULL == sclp)
		return NULL;
	else if (sclp->page)
		return (unsigned char *)page_address(sclp->page) + 
		       sclp->offset;
	else
		return NULL;
}

static
int scsi_debug_queuecommand(Scsi_Cmnd * SCpnt, void (*done) (Scsi_Cmnd *))
{
	unsigned char *cmd = (unsigned char *) SCpnt->cmnd;
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

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) {
		printk(KERN_INFO "scsi_debug: queue_command: cmd ");
		for (i = 0, num = SCpnt->cmd_len; i < num; ++i)
	            printk("%02x ", cmd[i]);
		printk("   use_sg=%d\n", SCpnt->use_sg);
	}
	/*
	 * If we are being notified of the mid-level reposessing a command
	 * due to timeout, just return.
	 */
	if (done == NULL) {
		return 0;
	}

	if (SCpnt->use_sg) { /* just use first element */
		struct scatterlist *sgpnt = (struct scatterlist *)
						SCpnt->request_buffer;

		buff = sdebug_scatg2virt(&sgpnt[0]);
		bufflen = sgpnt[0].length;
		/* READ and WRITE process scatterlist themselves */
	}
	else 
		buff = (unsigned char *) SCpnt->request_buffer;

        /*
         * If a command comes for the ID of the host itself, just print
         * a silly message and return.
         */
        if(target == 7) {
                printk(KERN_WARNING "How do you do!\n");
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
	       SCpnt->device, (int)*cmd);
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
	case INQUIRY:     /* mandatory */
		scsi_debug_errsts = scsi_debug_inquiry(cmd, target, buff, 
						       bufflen, devip);
		/* assume INQUIRY called first so setup max_cmd_len */
		if (SCpnt->host->max_cmd_len != SCSI_DEBUG_MAX_CMD_LEN)
			SCpnt->host->max_cmd_len = SCSI_DEBUG_MAX_CMD_LEN;
		break;
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
	case SEND_DIAGNOSTIC:     /* mandatory */
		SCSI_LOG_LLQUEUE(3, printk("Send Diagnostic\n"));
		if (buff)
			memset(buff, 0, bufflen);
		scsi_debug_errsts = 0;
		break;
	case TEST_UNIT_READY:     /* mandatory */
		SCSI_LOG_LLQUEUE(3, printk("Test unit ready(%p %d)\n", 
					   buff, bufflen));
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
	case MODE_SENSE_10:
		scsi_debug_errsts = 
		    scsi_debug_mode_sense(cmd, target, buff, bufflen, devip);
		break;
	default:
#if 0
		printk(KERN_INFO "scsi_debug: Unsupported command, "
		       "opcode=0x%x\n", (int)cmd[0]);
#endif
		if (check_reset(SCpnt, devip)) {
			done(SCpnt);
			return 0;
		}
		scsi_debug_errsts = (COMMAND_COMPLETE << 8) |
				    (CHECK_CONDITION << 1);
		mk_sense_buffer(devip, 2, ILLEGAL_REQUEST, 0x20, 0, 14);
		break;
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
		printk(KERN_ERR "scsi_debug_queuecommand: "
		       "done can't be NULL\n");

#if 0
	printk(KERN_INFO "Sending command (%d %x %d %d)...", i, done, 
	       timeout[i].expires, jiffies);
#endif
#endif

	return 0;
}

static int scsi_debug_ioctl(Scsi_Device *dev, int cmd, void *arg)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) {
		printk(KERN_INFO "scsi_debug: ioctl: cmd=0x%x\n", cmd);
	}
	return -ENOTTY;
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

#define SDEBUG_MAX_INQ_SZ 58

static int scsi_debug_inquiry(unsigned char * cmd, int target,
			      unsigned char * buff, int bufflen,
			      Sdebug_dev_info * devip)
{
	unsigned char pq_pdt;
	unsigned char arr[SDEBUG_MAX_INQ_SZ];
	int min_len = bufflen > SDEBUG_MAX_INQ_SZ ? 
			SDEBUG_MAX_INQ_SZ : bufflen;

	SCSI_LOG_LLQUEUE(3, printk("Inquiry...(%p %d)\n", buff, bufflen));
	if (bufflen < cmd[4])
		printk(KERN_INFO "scsi_debug: inquiry: bufflen=%d "
		       "< alloc_length=%d\n", bufflen, (int)cmd[4]);
	memset(buff, 0, bufflen);
	memset(arr, 0, SDEBUG_MAX_INQ_SZ);
	pq_pdt = DEVICE_TYPE(target);
	arr[0] = pq_pdt;
	if (0x2 & cmd[1]) {  /* CMDDT bit set */
		mk_sense_buffer(devip, 1, ILLEGAL_REQUEST, 0x24, 0, 14);
		return (COMMAND_COMPLETE << 8) | (CHECK_CONDITION << 1);
	}
	else if (0x1 & cmd[1]) {  /* EVPD bit set */
		if (0 == cmd[2]) { /* supported vital product data pages */
			arr[3] = 1;
			arr[4] = 0x80; /* ... only unit serial number */
		}
		else if (0x80 == cmd[2]) { /* unit serial number */
			arr[1] = 0x80;
			arr[3] = 4;
			arr[4] = '1'; arr[5] = '2'; arr[6] = '3';
			arr[7] = '4';
		}
		else {
			/* Illegal request, invalid field in cdb */
			mk_sense_buffer(devip, 1, ILLEGAL_REQUEST, 0x24, 0, 14);
			return (COMMAND_COMPLETE << 8) | (CHECK_CONDITION << 1);
		}
		memcpy(buff, arr, min_len); 
		return 0;
	}
	/* drops through here for a standard inquiry */
	arr[1] = DEV_REMOVEABLE(target) ? 0x80 : 0;	/* Removable disk */
	arr[2] = 3;	/* claim SCSI 3 */
	arr[4] = SDEBUG_MAX_INQ_SZ - 5;
	arr[7] = 0x3a; /* claim: WBUS16, SYNC, LINKED + CMDQUE */
	memcpy(&arr[8], "Linux   ", 8);
	memcpy(&arr[16], "scsi_debug      ", 16);
	memcpy(&arr[32], "0003", 4);
	memcpy(buff, arr, min_len);
	return 0;
}

/* <<Following mode page info copied from ST318451LW>> */ 

static int sdebug_err_recov_pg(unsigned char * p, int pcontrol, int target)
{	/* Read-Write Error Recovery page for mode_sense */
	unsigned char err_recov_pg[] = {0x1, 0xa, 0xc0, 11, 240, 0, 0, 0, 
					5, 0, 0xff, 0xff};

	memcpy(p, err_recov_pg, sizeof(err_recov_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(err_recov_pg) - 2);
	return sizeof(err_recov_pg);
}

static int sdebug_disconnect_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Disconnect-Reconnect page for mode_sense */
	unsigned char disconnect_pg[] = {0x2, 0xe, 128, 128, 0, 10, 0, 0, 
					 0, 0, 0, 0, 0, 0, 0, 0};

	memcpy(p, disconnect_pg, sizeof(disconnect_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(disconnect_pg) - 2);
	return sizeof(disconnect_pg);
}

static int sdebug_caching_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Caching page for mode_sense */
	unsigned char caching_pg[] = {0x8, 18, 0x14, 0, 0xff, 0xff, 0, 0, 
		0xff, 0xff, 0xff, 0xff, 0x80, 0x14, 0, 0,     0, 0, 0, 0};

	memcpy(p, caching_pg, sizeof(caching_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(caching_pg) - 2);
	return sizeof(caching_pg);
}

static int sdebug_ctrl_m_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Control mode page for mode_sense */
	unsigned char ctrl_m_pg[] = {0xa, 10, 2, 0, 0, 0, 0, 0,
				     0, 0, 0x2, 0x4b};

	memcpy(p, ctrl_m_pg, sizeof(ctrl_m_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(ctrl_m_pg) - 2);
	return sizeof(ctrl_m_pg);
}


#define SDEBUG_MAX_MSENSE_SZ 256

static int scsi_debug_mode_sense(unsigned char * cmd, int target,
			         unsigned char * buff, int bufflen,
			         Sdebug_dev_info * devip)
{
	unsigned char dbd;
	int pcontrol, pcode;
	unsigned char dev_spec;
	int alloc_len, msense_6, offset, len;
	unsigned char * ap;
	unsigned char arr[SDEBUG_MAX_MSENSE_SZ];
	int min_len = bufflen > SDEBUG_MAX_MSENSE_SZ ? 
			SDEBUG_MAX_MSENSE_SZ : bufflen;

	SCSI_LOG_LLQUEUE(3, printk("Mode sense ...(%p %d)\n", buff, bufflen));
	dbd = cmd[1] & 0x8;
	pcontrol = (cmd[2] & 0xc0) >> 6;
	pcode = cmd[2] & 0x3f;
	msense_6 = (MODE_SENSE == cmd[0]);
	alloc_len = msense_6 ? cmd[4] : ((cmd[7] << 8) | cmd[6]);
	/* printk(KERN_INFO "msense: dbd=%d pcontrol=%d pcode=%d "
		"msense_6=%d alloc_len=%d\n", dbd, pcontrol, pcode, "
		"msense_6, alloc_len); */
	if (bufflen < alloc_len)
		printk(KERN_INFO "scsi_debug: mode_sense: bufflen=%d "
		       "< alloc_length=%d\n", bufflen, alloc_len);
	memset(buff, 0, bufflen);
	memset(arr, 0, SDEBUG_MAX_MSENSE_SZ);
	if (0x3 == pcontrol) {  /* Saving values not supported */
		mk_sense_buffer(devip, 1, ILLEGAL_REQUEST, 0x39, 0, 14);
		return (COMMAND_COMPLETE << 8) | (CHECK_CONDITION << 1);
	}
	dev_spec = DEV_READONLY(target) ? 0x80 : 0x0;
	if (msense_6) {
		arr[2] = dev_spec;
		offset = 4;
	}
	else {
		arr[3] = dev_spec;
		offset = 8;
	}
	ap = arr + offset;

	switch (pcode) {
	case 0x1:	/* Read-Write error recovery page, direct access */
		len = sdebug_err_recov_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x2:	/* Disconnect-Reconnect page, all devices */
		len = sdebug_disconnect_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x8:	/* Caching page, direct access */
		len = sdebug_caching_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0xa:	/* Control Mode page, all devices */
		len = sdebug_ctrl_m_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x3f:	/* Read all Mode pages */
		len = sdebug_err_recov_pg(ap, pcontrol, target);
		len += sdebug_disconnect_pg(ap + len, pcontrol, target);
		len += sdebug_caching_pg(ap + len, pcontrol, target);
		len += sdebug_ctrl_m_pg(ap + len, pcontrol, target);
		offset += len;
		break;
	default:
		mk_sense_buffer(devip, 1, ILLEGAL_REQUEST, 0x24, 0, 14);
		return (COMMAND_COMPLETE << 8) | (CHECK_CONDITION << 1);
	}
	if (msense_6)
		arr[0] = offset - 1;
	else {
		offset -= 2;
		arr[0] = (offset >> 8) & 0xff; 
		arr[1] = offset & 0xff; 
	}
	memcpy(buff, arr, min_len);
	return 0;
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

		delay += SCpnt->request->nr_sectors * SCSI_DATARATE;
		if (delay)
			usleep(delay);
	}
#endif
	if ((SCSI_DEBUG_OPT_MEDIUM_ERR & scsi_debug_opts) &&
	    (block >= OPT_MEDIUM_ERR_ADDR) && 
	    (block < (OPT_MEDIUM_ERR_ADDR + num))) {
		*errstsp = (COMMAND_COMPLETE << 8) |
			   (CHECK_CONDITION << 1);
		mk_sense_buffer(devip, 1, MEDIUM_ERROR, 0x11, 0, 14);
		/* claim unrecoverable read error */
		return 1;
	}
	read_lock_irqsave(&sdebug_atomic_rw, iflags);
        sgcount = 0;
	nbytes = bufflen;
	/* printk(KERN_INFO "scsi_debug_read: block=%d, tot_bufflen=%d\n", 
	       block, bufflen); */
	if (SCpnt->use_sg) {
		sgcount = 0;
		sgpnt = (struct scatterlist *) buff;
		buff = sdebug_scatg2virt(&sgpnt[sgcount]);
		bufflen = sgpnt[sgcount].length;
	}
	*errstsp = 0;
	do {
		memcpy(buff, fake_storep + (block * SECT_SIZE), bufflen);
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
			printk(KERN_WARNING "sdebug_read: unexpected "
			       "nbytes=%d\n", nbytes);
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
		memcpy(fake_storep + (block * SECT_SIZE), buff, bufflen);

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
			printk(KERN_WARNING "sdebug_write: "
			       "unexpected nbytes=%d\n", nbytes);
	} while (nbytes);
	write_unlock_irqrestore(&sdebug_atomic_rw, iflags);
	return 0;
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
		printk(KERN_ERR "scsi_debug_intr_handle: Unexpected "
		       "interrupt\n");
		return;
	}
#if 0
	printk(KERN_INFO "In intr_handle...");
	printk(KERN_INFO "...done %d %x %d %d\n", i, my_done, to, jiffies);
	printk(KERN_INFO "In intr_handle: %d %x %x\n", i, SCtmp, my_done);
#endif

	my_done(SCtmp);
#if 0
	printk(KERN_INFO "Called done.\n");
#endif
}

static int initialized = 0;

static int do_init(void)
{
	int sz = STORE_SIZE;

	starts[3] = CAPACITY;
	fake_storep = vmalloc(sz);
	if (NULL == fake_storep)
		return 1;
	memset(fake_storep, 0, sz);

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
	if (fake_storep)
		vfree(fake_storep);
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
	vfree(fake_storep);
}


static int scsi_debug_detect(Scsi_Host_Template * tpnt)
{
	int k, sz;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: detect\n");
	if (0 == initialized) {
		++initialized;
		sz = sizeof(Sdebug_dev_info) * scsi_debug_num_devs;
		devInfop = kmalloc(sz, GFP_ATOMIC);
		if (NULL == devInfop) {
			printk(KERN_ERR "scsi_debug_detect: out of "
			       "memory, 0.5\n");
			return 0;
		}
		memset(devInfop, 0, sz);
		if (do_init()) {
			printk(KERN_ERR "scsi_debug_detect: out of memory"
			       ", 0\n");
			return 0;
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
		printk(KERN_WARNING "scsi_debug_detect: called again\n");
		return 0;
	}
}


static int num_releases = 0;

static int scsi_debug_release(struct Scsi_Host * hpnt)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: release\n");
	scsi_unregister(hpnt);
	if (++num_releases != NR_HOSTS_PRESENT)
		return 0;
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

static int scsi_debug_abort(Scsi_Cmnd * SCpnt)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: abort\n");
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

static int scsi_debug_biosparam(Disk * disk, struct block_device *dev, int *info)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: biosparam\n");
	/* int size = disk->capacity; */
	info[0] = N_HEAD;
	info[1] = N_SECTOR;
	info[2] = N_CYLINDER;
	if (info[2] >= 1024)
		info[2] = 1024;
	return 0;
}

static int scsi_debug_device_reset(Scsi_Cmnd * SCpnt)
{
	Scsi_Device * sdp;
	int k;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: device_reset\n");
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

static int scsi_debug_bus_reset(Scsi_Cmnd * SCpnt)
{
	Scsi_Device * sdp;
	struct Scsi_Host * hp;
	int k;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: bus_reset\n");
	++num_bus_resets;
	if (SCpnt && ((sdp = SCpnt->device)) && ((hp = sdp->host))) {
		for (k = 0; k < scsi_debug_num_devs; ++k) {
			if (hp == devInfop[k].sdp->host)
				devInfop[k].reset = 1;
		}
	}
	return SUCCESS;
}

static int scsi_debug_host_reset(Scsi_Cmnd * SCpnt)
{
	int k;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: host_reset\n");
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
        printk(KERN_INFO "scsi_debug_num_devs: usage scsi_debug_num_devs=<n> "
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
        printk(KERN_INFO "scsi_debug_dev_size_mb: usage "
	       "scsi_debug_dev_size_mb=<n>\n"
               "    (<n> is number of MB ram shared by all devs\n");
        return 0;
    }
}

__setup("scsi_debug_dev_size_mb=", scsi_debug_dev_size_mb_setup);

static int __init scsi_debug_opts_setup(char *str)
{   
    int tmp; 
    
    if (get_option(&str, &tmp) == 1) {
        if (tmp > 0)
            scsi_debug_opts = tmp;
        return 1;
    } else {
        printk(KERN_INFO "scsi_debug_opts: usage "
	       "scsi_debug_opts=<n>\n"
               "    (1->noise, 2->medium_error, 4->... (can be or-ed)\n");
        return 0;
    }
}

__setup("scsi_debug_opts=", scsi_debug_opts_setup);
#endif

MODULE_AUTHOR("Eric Youngdale + Douglas Gilbert");
MODULE_DESCRIPTION("SCSI debug adapter driver");
MODULE_PARM(scsi_debug_num_devs, "i");
MODULE_PARM_DESC(scsi_debug_num_devs, "number of SCSI devices to simulate");
MODULE_PARM(scsi_debug_dev_size_mb, "i");
MODULE_PARM_DESC(scsi_debug_dev_size_mb, "size in MB of ram shared by devs");
MODULE_PARM(scsi_debug_opts, "i");
MODULE_PARM_DESC(scsi_debug_opts, "1->noise, 2->medium_error, 4->...");

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

static char sdebug_info[256];

static const char * scsi_debug_info(struct Scsi_Host * shp)
{
	sprintf(sdebug_info, "scsi_debug, %s, num_devs=%d, "
		"dev_size_mb=%d, opts=0x%x", scsi_debug_version_str,
		scsi_debug_num_devs, scsi_debug_dev_size_mb,
		scsi_debug_opts);
	return sdebug_info;
}

/* scsi_debug_proc_info
 * Used if the driver currently has no own support for /proc/scsi
 */
static int scsi_debug_proc_info(char *buffer, char **start, off_t offset,
				int length, int inode, int inout)
{
	int len, pos, begin;
	int orig_length;

	orig_length = length;

	if (inout == 1) {
		char arr[16];
		int minLen = length > 15 ? 15 : length;

		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		memcpy(arr, buffer, minLen);
		arr[minLen] = '\0';
		if (1 != sscanf(arr, "%d", &pos))
			return -EINVAL;
		scsi_debug_opts = pos;
		return length;
	}
	begin = 0;
	pos = len = sprintf(buffer, "scsi_debug adapter driver, %s\n"
	    "num_devs=%d, shared (ram) size=%d MB, opts=0x%x\n"
	    "sector_size=%d bytes, cylinders=%d, heads=%d, sectors=%d\n"
	    "number of aborts=%d, device_reset=%d, bus_resets=%d, " 
	    "host_resets=%d\n",
	    scsi_debug_version_str, scsi_debug_num_devs, 
	    scsi_debug_dev_size_mb, scsi_debug_opts, SECT_SIZE,
	    N_CYLINDER, N_HEAD, N_SECTOR,
	    num_aborts, num_dev_resets, num_bus_resets, num_host_resets);
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


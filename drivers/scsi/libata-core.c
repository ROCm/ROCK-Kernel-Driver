/*
   libata-core.c - helper library for ATA

   Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
   Copyright 2003-2004 Jeff Garzik

   The contents of this file are subject to the Open
   Software License version 1.1 that can be found at
   http://www.opensource.org/licenses/osl-1.1.txt and is included herein
   by reference.

   Alternatively, the contents of this file may be used under the terms
   of the GNU General Public License version 2 (the "GPL") as distributed
   in the kernel source COPYING file, in which case the provisions of
   the GPL are applicable instead of the above.  If you wish to allow
   the use of your version of this file only under the terms of the
   GPL and not to allow others to use your version of this file under
   the OSL, indicate your decision by deleting the provisions above and
   replace them with the notice and other provisions required by the GPL.
   If you do not delete the provisions above, a recipient may use your
   version of this file under either the OSL or the GPL.

 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>
#include <scsi/scsi.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/libata.h>
#include <asm/io.h>
#include <asm/semaphore.h>

#include "libata.h"

static unsigned int ata_busy_sleep (struct ata_port *ap,
				    unsigned long tmout_pat,
			    	    unsigned long tmout);
static void __ata_dev_select (struct ata_port *ap, unsigned int device);
static void ata_dma_complete(struct ata_port *ap, u8 host_stat,
			     unsigned int done_late);
static void ata_host_set_pio(struct ata_port *ap);
static void ata_host_set_udma(struct ata_port *ap);
static void ata_dev_set_pio(struct ata_port *ap, unsigned int device);
static void ata_dev_set_udma(struct ata_port *ap, unsigned int device);
static void ata_set_mode(struct ata_port *ap);
static int ata_qc_issue_prot(struct ata_queued_cmd *qc);

static unsigned int ata_unique_id = 1;
static struct workqueue_struct *ata_wq;

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Library module for ATA devices");
MODULE_LICENSE("GPL");

static const char * thr_state_name[] = {
	"THR_UNKNOWN",
	"THR_PORT_RESET",
	"THR_AWAIT_DEATH",
	"THR_PROBE_FAILED",
	"THR_IDLE",
	"THR_PROBE_SUCCESS",
	"THR_PROBE_START",
};

/**
 *	ata_thr_state_name - convert thread state enum to string
 *	@thr_state: thread state to be converted to string
 *
 *	Converts the specified thread state id to a constant C string.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	The THR_xxx-prefixed string naming the specified thread
 *	state id, or the string "<invalid THR_xxx state>".
 */

static const char *ata_thr_state_name(unsigned int thr_state)
{
	if (thr_state < ARRAY_SIZE(thr_state_name))
		return thr_state_name[thr_state];
	return "<invalid THR_xxx state>";
}

/**
 *	msleep - sleep for a number of milliseconds
 *	@msecs: number of milliseconds to sleep
 *
 *	Issues schedule_timeout call for the specified number
 *	of milliseconds.
 *
 *	LOCKING:
 *	None.
 */

static void msleep(unsigned long msecs)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(msecs) + 1);
}

/**
 *	ata_tf_load_pio - send taskfile registers to host controller
 *	@ioaddr: set of IO ports to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller using PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_load_pio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		outb(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		outb(tf->hob_feature, ioaddr->feature_addr);
		outb(tf->hob_nsect, ioaddr->nsect_addr);
		outb(tf->hob_lbal, ioaddr->lbal_addr);
		outb(tf->hob_lbam, ioaddr->lbam_addr);
		outb(tf->hob_lbah, ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		outb(tf->feature, ioaddr->feature_addr);
		outb(tf->nsect, ioaddr->nsect_addr);
		outb(tf->lbal, ioaddr->lbal_addr);
		outb(tf->lbam, ioaddr->lbam_addr);
		outb(tf->lbah, ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		outb(tf->device, ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

/**
 *	ata_tf_load_mmio - send taskfile registers to host controller
 *	@ioaddr: set of IO ports to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller using MMIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_load_mmio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		writeb(tf->ctl, ap->ioaddr.ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		writeb(tf->hob_feature, (void *) ioaddr->feature_addr);
		writeb(tf->hob_nsect, (void *) ioaddr->nsect_addr);
		writeb(tf->hob_lbal, (void *) ioaddr->lbal_addr);
		writeb(tf->hob_lbam, (void *) ioaddr->lbam_addr);
		writeb(tf->hob_lbah, (void *) ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		writeb(tf->feature, (void *) ioaddr->feature_addr);
		writeb(tf->nsect, (void *) ioaddr->nsect_addr);
		writeb(tf->lbal, (void *) ioaddr->lbal_addr);
		writeb(tf->lbam, (void *) ioaddr->lbam_addr);
		writeb(tf->lbah, (void *) ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		writeb(tf->device, (void *) ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

/**
 *	ata_exec_command_pio - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues PIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_exec_command_pio(struct ata_port *ap, struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);

       	outb(tf->command, ap->ioaddr.command_addr);
	ata_pause(ap);
}


/**
 *	ata_exec_command_mmio - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues MMIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_exec_command_mmio(struct ata_port *ap, struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);

       	writeb(tf->command, (void *) ap->ioaddr.command_addr);
	ata_pause(ap);
}

/**
 *	ata_exec - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues PIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	Obtains host_set lock.
 */

static inline void ata_exec(struct ata_port *ap, struct ata_taskfile *tf)
{
	unsigned long flags;

	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);
	spin_lock_irqsave(&ap->host_set->lock, flags);
	ap->ops->exec_command(ap, tf);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);
}

/**
 *	ata_tf_to_host - issue ATA taskfile to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA taskfile register set to ATA host controller,
 *	via PIO, with proper synchronization with interrupt handler and
 *	other threads.
 *
 *	LOCKING:
 *	Obtains host_set lock.
 */

static void ata_tf_to_host(struct ata_port *ap, struct ata_taskfile *tf)
{
	ap->ops->tf_load(ap, tf);

	ata_exec(ap, tf);
}

/**
 *	ata_tf_to_host_nolock - issue ATA taskfile to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA taskfile register set to ATA host controller,
 *	via PIO, with proper synchronization with interrupt handler and
 *	other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_tf_to_host_nolock(struct ata_port *ap, struct ata_taskfile *tf)
{
	ap->ops->tf_load(ap, tf);
	ap->ops->exec_command(ap, tf);
}

/**
 *	ata_tf_read_pio - input device's ATA taskfile shadow registers
 *	@ioaddr: set of IO ports from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf via PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_read_pio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->nsect = inb(ioaddr->nsect_addr);
	tf->lbal = inb(ioaddr->lbal_addr);
	tf->lbam = inb(ioaddr->lbam_addr);
	tf->lbah = inb(ioaddr->lbah_addr);
	tf->device = inb(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		outb(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
		tf->hob_feature = inb(ioaddr->error_addr);
		tf->hob_nsect = inb(ioaddr->nsect_addr);
		tf->hob_lbal = inb(ioaddr->lbal_addr);
		tf->hob_lbam = inb(ioaddr->lbam_addr);
		tf->hob_lbah = inb(ioaddr->lbah_addr);
	}
}

/**
 *	ata_tf_read_mmio - input device's ATA taskfile shadow registers
 *	@ioaddr: set of IO ports from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf via MMIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_read_mmio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->nsect = readb((void *)ioaddr->nsect_addr);
	tf->lbal = readb((void *)ioaddr->lbal_addr);
	tf->lbam = readb((void *)ioaddr->lbam_addr);
	tf->lbah = readb((void *)ioaddr->lbah_addr);
	tf->device = readb((void *)ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		writeb(tf->ctl | ATA_HOB, ap->ioaddr.ctl_addr);
		tf->hob_feature = readb((void *)ioaddr->error_addr);
		tf->hob_nsect = readb((void *)ioaddr->nsect_addr);
		tf->hob_lbal = readb((void *)ioaddr->lbal_addr);
		tf->hob_lbam = readb((void *)ioaddr->lbam_addr);
		tf->hob_lbah = readb((void *)ioaddr->lbah_addr);
	}
}

/**
 *	ata_check_status_pio - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	via PIO and return it's value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_check_status_pio(struct ata_port *ap)
{
	return inb(ap->ioaddr.status_addr);
}

/**
 *	ata_check_status_mmio - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	via MMIO and return it's value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_check_status_mmio(struct ata_port *ap)
{
       	return readb((void *) ap->ioaddr.status_addr);
}

/**
 *	ata_tf_to_fis - Convert ATA taskfile to SATA FIS structure
 *	@tf: Taskfile to convert
 *	@fis: Buffer into which data will output
 *
 *	Converts a standard ATA taskfile to a Serial ATA
 *	FIS structure (Register - Host to Device).
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_to_fis(struct ata_taskfile *tf, u8 *fis, u8 pmp)
{
	fis[0] = 0x27;	/* Register - Host to Device FIS */
	fis[1] = (pmp & 0xf) | (1 << 7); /* Port multiplier number,
					    bit 7 indicates Command FIS */
	fis[2] = tf->command;
	fis[3] = tf->feature;

	fis[4] = tf->lbal;
	fis[5] = tf->lbam;
	fis[6] = tf->lbah;
	fis[7] = tf->device;

	fis[8] = tf->hob_lbal;
	fis[9] = tf->hob_lbam;
	fis[10] = tf->hob_lbah;
	fis[11] = tf->hob_feature;

	fis[12] = tf->nsect;
	fis[13] = tf->hob_nsect;
	fis[14] = 0;
	fis[15] = tf->ctl;

	fis[16] = 0;
	fis[17] = 0;
	fis[18] = 0;
	fis[19] = 0;
}

/**
 *	ata_tf_from_fis - Convert SATA FIS to ATA taskfile
 *	@fis: Buffer from which data will be input
 *	@tf: Taskfile to output
 *
 *	Converts a standard ATA taskfile to a Serial ATA
 *	FIS structure (Register - Host to Device).
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_from_fis(u8 *fis, struct ata_taskfile *tf)
{
	tf->command	= fis[2];	/* status */
	tf->feature	= fis[3];	/* error */

	tf->lbal	= fis[4];
	tf->lbam	= fis[5];
	tf->lbah	= fis[6];
	tf->device	= fis[7];

	tf->hob_lbal	= fis[8];
	tf->hob_lbam	= fis[9];
	tf->hob_lbah	= fis[10];

	tf->nsect	= fis[12];
	tf->hob_nsect	= fis[13];
}

/**
 *	ata_prot_to_cmd - determine which read/write opcodes to use
 *	@protocol: ATA_PROT_xxx taskfile protocol
 *	@lba48: true is lba48 is present
 *
 *	Given necessary input, determine which read/write commands
 *	to use to transfer data.
 *
 *	LOCKING:
 *	None.
 */
static int ata_prot_to_cmd(int protocol, int lba48)
{
	int rcmd = 0, wcmd = 0;

	switch (protocol) {
	case ATA_PROT_PIO:
		if (lba48) {
			rcmd = ATA_CMD_PIO_READ_EXT;
			wcmd = ATA_CMD_PIO_WRITE_EXT;
		} else {
			rcmd = ATA_CMD_PIO_READ;
			wcmd = ATA_CMD_PIO_WRITE;
		}
		break;

	case ATA_PROT_DMA:
		if (lba48) {
			rcmd = ATA_CMD_READ_EXT;
			wcmd = ATA_CMD_WRITE_EXT;
		} else {
			rcmd = ATA_CMD_READ;
			wcmd = ATA_CMD_WRITE;
		}
		break;

	default:
		return -1;
	}

	return rcmd | (wcmd << 8);
}

/**
 *	ata_dev_set_protocol - set taskfile protocol and r/w commands
 *	@dev: device to examine and configure
 *
 *	Examine the device configuration, after we have
 *	read the identify-device page and configured the
 *	data transfer mode.  Set internal state related to
 *	the ATA taskfile protocol (pio, pio mult, dma, etc.)
 *	and calculate the proper read/write commands to use.
 *
 *	LOCKING:
 *	caller.
 */
static void ata_dev_set_protocol(struct ata_device *dev)
{
	int pio = (dev->flags & ATA_DFLAG_PIO);
	int lba48 = (dev->flags & ATA_DFLAG_LBA48);
	int proto, cmd;

	if (pio)
		proto = dev->xfer_protocol = ATA_PROT_PIO;
	else
		proto = dev->xfer_protocol = ATA_PROT_DMA;

	cmd = ata_prot_to_cmd(proto, lba48);
	if (cmd < 0)
		BUG();

	dev->read_cmd = cmd & 0xff;
	dev->write_cmd = (cmd >> 8) & 0xff;
}

static const char * udma_str[] = {
	"UDMA/16",
	"UDMA/25",
	"UDMA/33",
	"UDMA/44",
	"UDMA/66",
	"UDMA/100",
	"UDMA/133",
	"UDMA7",
};

/**
 *	ata_udma_string - convert UDMA bit offset to string
 *	@udma_mask: mask of bits supported; only highest bit counts.
 *
 *	Determine string which represents the highest speed
 *	(highest bit in @udma_mask).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Constant C string representing highest speed listed in
 *	@udma_mask, or the constant C string "<n/a>".
 */

static const char *ata_udma_string(unsigned int udma_mask)
{
	int i;

	for (i = 7; i >= 0; i--) {
		if (udma_mask & (1 << i))
			return udma_str[i];
	}

	return "<n/a>";
}

/**
 *	ata_pio_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	This technique was originally described in
 *	Hale Landis's ATADRVR (www.ata-atapi.com), and
 *	later found its way into the ATA/ATAPI spec.
 *
 *	Write a pattern to the ATA shadow registers,
 *	and if a device is present, it will respond by
 *	correctly storing and echoing back the
 *	ATA shadow register contents.
 *
 *	LOCKING:
 *	caller.
 */

static unsigned int ata_pio_devchk(struct ata_port *ap,
				   unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	__ata_dev_select(ap, device);

	outb(0x55, ioaddr->nsect_addr);
	outb(0xaa, ioaddr->lbal_addr);

	outb(0xaa, ioaddr->nsect_addr);
	outb(0x55, ioaddr->lbal_addr);

	outb(0x55, ioaddr->nsect_addr);
	outb(0xaa, ioaddr->lbal_addr);

	nsect = inb(ioaddr->nsect_addr);
	lbal = inb(ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	ata_mmio_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	This technique was originally described in
 *	Hale Landis's ATADRVR (www.ata-atapi.com), and
 *	later found its way into the ATA/ATAPI spec.
 *
 *	Write a pattern to the ATA shadow registers,
 *	and if a device is present, it will respond by
 *	correctly storing and echoing back the
 *	ATA shadow register contents.
 *
 *	LOCKING:
 *	caller.
 */

static unsigned int ata_mmio_devchk(struct ata_port *ap,
				    unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	__ata_dev_select(ap, device);

	writeb(0x55, (void *) ioaddr->nsect_addr);
	writeb(0xaa, (void *) ioaddr->lbal_addr);

	writeb(0xaa, (void *) ioaddr->nsect_addr);
	writeb(0x55, (void *) ioaddr->lbal_addr);

	writeb(0x55, (void *) ioaddr->nsect_addr);
	writeb(0xaa, (void *) ioaddr->lbal_addr);

	nsect = readb((void *) ioaddr->nsect_addr);
	lbal = readb((void *) ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	ata_dev_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	Dispatch ATA device presence detection, depending
 *	on whether we are using PIO or MMIO to talk to the
 *	ATA shadow registers.
 *
 *	LOCKING:
 *	caller.
 */

static unsigned int ata_dev_devchk(struct ata_port *ap,
				    unsigned int device)
{
	if (ap->flags & ATA_FLAG_MMIO)
		return ata_mmio_devchk(ap, device);
	return ata_pio_devchk(ap, device);
}

/**
 *	ata_dev_classify - determine device type based on ATA-spec signature
 *	@tf: ATA taskfile register set for device to be identified
 *
 *	Determine from taskfile register contents whether a device is
 *	ATA or ATAPI, as per "Signature and persistence" section
 *	of ATA/PI spec (volume 1, sect 5.14).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Device type, %ATA_DEV_ATA, %ATA_DEV_ATAPI, or %ATA_DEV_UNKNOWN
 *	the event of failure.
 */

static unsigned int ata_dev_classify(struct ata_taskfile *tf)
{
	/* Apple's open source Darwin code hints that some devices only
	 * put a proper signature into the LBA mid/high registers,
	 * So, we only check those.  It's sufficient for uniqueness.
	 */

	if (((tf->lbam == 0) && (tf->lbah == 0)) ||
	    ((tf->lbam == 0x3c) && (tf->lbah == 0xc3))) {
		DPRINTK("found ATA device by sig\n");
		return ATA_DEV_ATA;
	}

	if (((tf->lbam == 0x14) && (tf->lbah == 0xeb)) ||
	    ((tf->lbam == 0x69) && (tf->lbah == 0x96))) {
		DPRINTK("found ATAPI device by sig\n");
		return ATA_DEV_ATAPI;
	}

	DPRINTK("unknown device\n");
	return ATA_DEV_UNKNOWN;
}

/**
 *	ata_dev_try_classify - Parse returned ATA device signature
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	After an event -- SRST, E.D.D., or SATA COMRESET -- occurs,
 *	an ATA/ATAPI-defined set of values is placed in the ATA
 *	shadow registers, indicating the results of device detection
 *	and diagnostics.
 *
 *	Select the ATA device, and read the values from the ATA shadow
 *	registers.  Then parse according to the Error register value,
 *	and the spec-defined values examined by ata_dev_classify().
 *
 *	LOCKING:
 *	caller.
 */

static u8 ata_dev_try_classify(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];
	struct ata_taskfile tf;
	unsigned int class;
	u8 err;

	__ata_dev_select(ap, device);

	memset(&tf, 0, sizeof(tf));

	err = ata_chk_err(ap);
	ap->ops->tf_read(ap, &tf);

	dev->class = ATA_DEV_NONE;

	/* see if device passed diags */
	if (err == 1)
		/* do nothing */ ;
	else if ((device == 0) && (err == 0x81))
		/* do nothing */ ;
	else
		return err;

	/* determine if device if ATA or ATAPI */
	class = ata_dev_classify(&tf);
	if (class == ATA_DEV_UNKNOWN)
		return err;
	if ((class == ATA_DEV_ATA) && (ata_chk_status(ap) == 0))
		return err;

	dev->class = class;

	return err;
}

/**
 *	ata_dev_id_string - Convert IDENTIFY DEVICE page into string
 *	@dev: Device whose IDENTIFY DEVICE results we will examine
 *	@s: string into which data is output
 *	@ofs: offset into identify device page
 *	@len: length of string to return
 *
 *	The strings in the IDENTIFY DEVICE page are broken up into
 *	16-bit chunks.  Run through the string, and output each
 *	8-bit chunk linearly, regardless of platform.
 *
 *	LOCKING:
 *	caller.
 */

void ata_dev_id_string(struct ata_device *dev, unsigned char *s,
		       unsigned int ofs, unsigned int len)
{
	unsigned int c;

	while (len > 0) {
		c = dev->id[ofs] >> 8;
		*s = c;
		s++;

		c = dev->id[ofs] & 0xff;
		*s = c;
		s++;

		ofs++;
		len -= 2;
	}
}

/**
 *	ata_dev_parse_strings - Store useful IDENTIFY DEVICE page strings
 *	@dev: Device whose IDENTIFY DEVICE page info we use
 *
 *	We store 'vendor' and 'product' strings read from the device,
 *	for later use in the SCSI simulator's INQUIRY data.
 *
 *	Set these strings here, in the case of 'product', using
 *	data read from the ATA IDENTIFY DEVICE page.
 *
 *	LOCKING:
 *	caller.
 */

static void ata_dev_parse_strings(struct ata_device *dev)
{
	assert (dev->class == ATA_DEV_ATA);
	memcpy(dev->vendor, "ATA     ", 8);

	ata_dev_id_string(dev, dev->product, ATA_ID_PROD_OFS,
			  sizeof(dev->product));
}

/**
 *	__ata_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *
 *	Use the method defined in the ATA specification to
 *	make either device 0, or device 1, active on the
 *	ATA channel.
 *
 *	LOCKING:
 *	caller.
 */

static void __ata_dev_select (struct ata_port *ap, unsigned int device)
{
	u8 tmp;

	if (device == 0)
		tmp = ATA_DEVICE_OBS;
	else
		tmp = ATA_DEVICE_OBS | ATA_DEV1;

	if (ap->flags & ATA_FLAG_MMIO) {
		writeb(tmp, (void *) ap->ioaddr.device_addr);
	} else {
		outb(tmp, ap->ioaddr.device_addr);
	}
	ata_pause(ap);		/* needed; also flushes, for mmio */
}

/**
 *	ata_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *	@wait: non-zero to wait for Status register BSY bit to clear
 *	@can_sleep: non-zero if context allows sleeping
 *
 *	Use the method defined in the ATA specification to
 *	make either device 0, or device 1, active on the
 *	ATA channel.
 *
 *	This is a high-level version of __ata_dev_select(),
 *	which additionally provides the services of inserting
 *	the proper pauses and status polling, where needed.
 *
 *	LOCKING:
 *	caller.
 */

void ata_dev_select(struct ata_port *ap, unsigned int device,
			   unsigned int wait, unsigned int can_sleep)
{
	VPRINTK("ENTER, ata%u: device %u, wait %u\n",
		ap->id, device, wait);

	if (wait)
		ata_wait_idle(ap);

	__ata_dev_select(ap, device);

	if (wait) {
		if (can_sleep && ap->device[device].class == ATA_DEV_ATAPI)
			msleep(150);
		ata_wait_idle(ap);
	}
}

/**
 *	ata_dump_id - IDENTIFY DEVICE info debugging output
 *	@dev: Device whose IDENTIFY DEVICE page we will dump
 *
 *	Dump selected 16-bit words from a detected device's
 *	IDENTIFY PAGE page.
 *
 *	LOCKING:
 *	caller.
 */

static inline void ata_dump_id(struct ata_device *dev)
{
	DPRINTK("49==0x%04x  "
		"53==0x%04x  "
		"63==0x%04x  "
		"64==0x%04x  "
		"75==0x%04x  \n",
		dev->id[49],
		dev->id[53],
		dev->id[63],
		dev->id[64],
		dev->id[75]);
	DPRINTK("80==0x%04x  "
		"81==0x%04x  "
		"82==0x%04x  "
		"83==0x%04x  "
		"84==0x%04x  \n",
		dev->id[80],
		dev->id[81],
		dev->id[82],
		dev->id[83],
		dev->id[84]);
	DPRINTK("88==0x%04x  "
		"93==0x%04x\n",
		dev->id[88],
		dev->id[93]);
}

/**
 *	ata_dev_identify - obtain IDENTIFY x DEVICE page
 *	@ap: port on which device we wish to probe resides
 *	@device: device bus address, starting at zero
 *
 *	Following bus reset, we issue the IDENTIFY [PACKET] DEVICE
 *	command, and read back the 512-byte device information page.
 *	The device information page is fed to us via the standard
 *	PIO-IN protocol, but we hand-code it here. (TODO: investigate
 *	using standard PIO-IN paths)
 *
 *	After reading the device information page, we use several
 *	bits of information from it to initialize data structures
 *	that will be used during the lifetime of the ata_device.
 *	Other data from the info page is used to disqualify certain
 *	older ATA devices we do not wish to support.
 *
 *	LOCKING:
 *	Inherited from caller.  Some functions called by this function
 *	obtain the host_set lock.
 */

static void ata_dev_identify(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];
	unsigned int i;
	u16 tmp, udma_modes;
	u8 status;
	struct ata_taskfile tf;
	unsigned int using_edd;

	if (!ata_dev_present(dev)) {
		DPRINTK("ENTER/EXIT (host %u, dev %u) -- nodev\n",
			ap->id, device);
		return;
	}

	if (ap->flags & (ATA_FLAG_SRST | ATA_FLAG_SATA_RESET))
		using_edd = 0;
	else
		using_edd = 1;

	DPRINTK("ENTER, host %u, dev %u\n", ap->id, device);

	assert (dev->class == ATA_DEV_ATA || dev->class == ATA_DEV_ATAPI ||
		dev->class == ATA_DEV_NONE);

	ata_dev_select(ap, device, 1, 1); /* select device 0/1 */

retry:
	ata_tf_init(ap, &tf, device);
	tf.ctl |= ATA_NIEN;
	tf.protocol = ATA_PROT_PIO;

	if (dev->class == ATA_DEV_ATA) {
		tf.command = ATA_CMD_ID_ATA;
		DPRINTK("do ATA identify\n");
	} else {
		tf.command = ATA_CMD_ID_ATAPI;
		DPRINTK("do ATAPI identify\n");
	}

	ata_tf_to_host(ap, &tf);

	/* crazy ATAPI devices... */
	if (dev->class == ATA_DEV_ATAPI)
		msleep(150);

	if (ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT))
		goto err_out;

	status = ata_chk_status(ap);
	if (status & ATA_ERR) {
		/*
		 * arg!  EDD works for all test cases, but seems to return
		 * the ATA signature for some ATAPI devices.  Until the
		 * reason for this is found and fixed, we fix up the mess
		 * here.  If IDENTIFY DEVICE returns command aborted
		 * (as ATAPI devices do), then we issue an
		 * IDENTIFY PACKET DEVICE.
		 *
		 * ATA software reset (SRST, the default) does not appear
		 * to have this problem.
		 */
		if ((using_edd) && (tf.command == ATA_CMD_ID_ATA)) {
			u8 err = ata_chk_err(ap);
			if (err & ATA_ABORTED) {
				dev->class = ATA_DEV_ATAPI;
				goto retry;
			}
		}
		goto err_out;
	}

	/* make sure we have BSY=0, DRQ=1 */
	if ((status & ATA_DRQ) == 0) {
		printk(KERN_WARNING "ata%u: dev %u (ATA%s?) not returning id page (0x%x)\n",
		       ap->id, device,
		       dev->class == ATA_DEV_ATA ? "" : "PI",
		       status);
		goto err_out;
	}

	/* read IDENTIFY [X] DEVICE page */
	if (ap->flags & ATA_FLAG_MMIO) {
		for (i = 0; i < ATA_ID_WORDS; i++)
			dev->id[i] = readw((void *)ap->ioaddr.data_addr);
	} else
		for (i = 0; i < ATA_ID_WORDS; i++)
			dev->id[i] = inw(ap->ioaddr.data_addr);

	/* wait for host_idle */
	status = ata_wait_idle(ap);
	if (status & (ATA_BUSY | ATA_DRQ)) {
		printk(KERN_WARNING "ata%u: dev %u (ATA%s?) error after id page (0x%x)\n",
		       ap->id, device,
		       dev->class == ATA_DEV_ATA ? "" : "PI",
		       status);
		goto err_out;
	}

	ata_irq_on(ap);	/* re-enable interrupts */

	/* print device capabilities */
	printk(KERN_DEBUG "ata%u: dev %u cfg "
	       "49:%04x 82:%04x 83:%04x 84:%04x 85:%04x 86:%04x 87:%04x 88:%04x\n",
	       ap->id, device, dev->id[49],
	       dev->id[82], dev->id[83], dev->id[84],
	       dev->id[85], dev->id[86], dev->id[87],
	       dev->id[88]);

	/*
	 * common ATA, ATAPI feature tests
	 */

	/* we require LBA and DMA support (bits 8 & 9 of word 49) */
	if (!ata_id_has_dma(dev) || !ata_id_has_lba(dev)) {
		printk(KERN_DEBUG "ata%u: no dma/lba\n", ap->id);
		goto err_out_nosup;
	}

	/* we require UDMA support */
	udma_modes =
	tmp = dev->id[ATA_ID_UDMA_MODES];
	if ((tmp & 0xff) == 0) {
		printk(KERN_DEBUG "ata%u: no udma\n", ap->id);
		goto err_out_nosup;
	}

	ata_dump_id(dev);

	ata_dev_parse_strings(dev);

	/* ATA-specific feature tests */
	if (dev->class == ATA_DEV_ATA) {
		if (!ata_id_is_ata(dev))	/* sanity check */
			goto err_out_nosup;

		tmp = dev->id[ATA_ID_MAJOR_VER];
		for (i = 14; i >= 1; i--)
			if (tmp & (1 << i))
				break;

		/* we require at least ATA-3 */
		if (i < 3) {
			printk(KERN_DEBUG "ata%u: no ATA-3\n", ap->id);
			goto err_out_nosup;
		}

		if (ata_id_has_lba48(dev)) {
			dev->flags |= ATA_DFLAG_LBA48;
			dev->n_sectors = ata_id_u64(dev, 100);
		} else {
			dev->n_sectors = ata_id_u32(dev, 60);
		}

		ap->host->max_cmd_len = 16;

		/* print device info to dmesg */
		printk(KERN_INFO "ata%u: dev %u ATA, max %s, %Lu sectors: %s\n",
		       ap->id, device,
		       ata_udma_string(udma_modes),
		       (unsigned long long)dev->n_sectors,
		       dev->flags & ATA_DFLAG_LBA48 ? " lba48" : "");
	}

	/* ATAPI-specific feature tests */
	else {
		if (ata_id_is_ata(dev))		/* sanity check */
			goto err_out_nosup;

		/* see if 16-byte commands supported */
		tmp = dev->id[0] & 0x3;
		if (tmp == 1)
			ap->host->max_cmd_len = 16;

		/* print device info to dmesg */
		printk(KERN_INFO "ata%u: dev %u ATAPI, max %s\n",
		       ap->id, device,
		       ata_udma_string(udma_modes));
	}

	DPRINTK("EXIT, drv_stat = 0x%x\n", ata_chk_status(ap));
	return;

err_out_nosup:
	printk(KERN_WARNING "ata%u: dev %u not supported, ignoring\n",
	       ap->id, device);
err_out:
	ata_irq_on(ap);	/* re-enable interrupts */
	dev->class++;	/* converts ATA_DEV_xxx into ATA_DEV_xxx_UNSUP */
	DPRINTK("EXIT, err\n");
}

/**
 *	ata_port_reset -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_port_reset(struct ata_port *ap)
{
	unsigned int i, found = 0;

	ap->ops->phy_reset(ap);
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		goto err_out;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		ata_dev_identify(ap, i);
		if (ata_dev_present(&ap->device[i])) {
			found = 1;
			if (ap->ops->dev_config)
				ap->ops->dev_config(ap, &ap->device[i]);
		}
	}

	if ((!found) || (ap->flags & ATA_FLAG_PORT_DISABLED))
		goto err_out_disable;

	ata_set_mode(ap);
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		goto err_out_disable;

	ap->thr_state = THR_PROBE_SUCCESS;

	return;

err_out_disable:
	ap->ops->port_disable(ap);
err_out:
	ap->thr_state = THR_PROBE_FAILED;
}

/**
 *	ata_port_probe -
 *	@ap:
 *
 *	LOCKING:
 */

void ata_port_probe(struct ata_port *ap)
{
	ap->flags &= ~ATA_FLAG_PORT_DISABLED;
}

/**
 *	sata_phy_reset -
 *	@ap:
 *
 *	LOCKING:
 *
 */
void sata_phy_reset(struct ata_port *ap)
{
	u32 sstatus;
	unsigned long timeout = jiffies + (HZ * 5);

	if (ap->flags & ATA_FLAG_SATA_RESET) {
		scr_write(ap, SCR_CONTROL, 0x301); /* issue phy wake/reset */
		scr_read(ap, SCR_STATUS);	/* dummy read; flush */
		udelay(400);			/* FIXME: a guess */
	}
	scr_write(ap, SCR_CONTROL, 0x300);	/* issue phy wake/clear reset */

	/* wait for phy to become ready, if necessary */
	do {
		msleep(200);
		sstatus = scr_read(ap, SCR_STATUS);
		if ((sstatus & 0xf) != 1)
			break;
	} while (time_before(jiffies, timeout));

	/* TODO: phy layer with polling, timeouts, etc. */
	if (sata_dev_present(ap))
		ata_port_probe(ap);
	else {
		sstatus = scr_read(ap, SCR_STATUS);
		printk(KERN_INFO "ata%u: no device found (phy stat %08x)\n",
		       ap->id, sstatus);
		ata_port_disable(ap);
	}

	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

	if (ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT)) {
		ata_port_disable(ap);
		return;
	}

	ata_bus_reset(ap);
}

/**
 *	ata_port_disable -
 *	@ap:
 *
 *	LOCKING:
 */

void ata_port_disable(struct ata_port *ap)
{
	ap->device[0].class = ATA_DEV_NONE;
	ap->device[1].class = ATA_DEV_NONE;
	ap->flags |= ATA_FLAG_PORT_DISABLED;
}

/**
 *	ata_set_mode - Program timings and issue SET FEATURES - XFER
 *	@ap: port on which timings will be programmed
 *
 *	LOCKING:
 *
 */
static void ata_set_mode(struct ata_port *ap)
{
	unsigned int force_pio, i;

	ata_host_set_pio(ap);
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

	ata_host_set_udma(ap);
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

#ifdef ATA_FORCE_PIO
	force_pio = 1;
#else
	force_pio = 0;
#endif

	if (force_pio) {
		ata_dev_set_pio(ap, 0);
		ata_dev_set_pio(ap, 1);
	} else {
		ata_dev_set_udma(ap, 0);
		ata_dev_set_udma(ap, 1);
	}

	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

	if (ap->ops->post_set_mode)
		ap->ops->post_set_mode(ap);

	for (i = 0; i < 2; i++) {
		struct ata_device *dev = &ap->device[i];
		ata_dev_set_protocol(dev);
	}
}

/**
 *	ata_busy_sleep - sleep until BSY clears, or timeout
 *	@ap: port containing status register to be polled
 *	@tmout_pat: impatience timeout
 *	@tmout: overall timeout
 *
 *	LOCKING:
 *
 */

static unsigned int ata_busy_sleep (struct ata_port *ap,
				    unsigned long tmout_pat,
			    	    unsigned long tmout)
{
	unsigned long timer_start, timeout;
	u8 status;

	status = ata_busy_wait(ap, ATA_BUSY, 300);
	timer_start = jiffies;
	timeout = timer_start + tmout_pat;
	while ((status & ATA_BUSY) && (time_before(jiffies, timeout))) {
		msleep(50);
		status = ata_busy_wait(ap, ATA_BUSY, 3);
	}

	if (status & ATA_BUSY)
		printk(KERN_WARNING "ata%u is slow to respond, "
		       "please be patient\n", ap->id);

	timeout = timer_start + tmout;
	while ((status & ATA_BUSY) && (time_before(jiffies, timeout))) {
		msleep(50);
		status = ata_chk_status(ap);
	}

	if (status & ATA_BUSY) {
		printk(KERN_ERR "ata%u failed to respond (%lu secs)\n",
		       ap->id, tmout / HZ);
		return 1;
	}

	return 0;
}

static void ata_bus_post_reset(struct ata_port *ap, unsigned int devmask)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int dev0 = devmask & (1 << 0);
	unsigned int dev1 = devmask & (1 << 1);
	unsigned long timeout;

	/* if device 0 was found in ata_dev_devchk, wait for its
	 * BSY bit to clear
	 */
	if (dev0)
		ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* if device 1 was found in ata_dev_devchk, wait for
	 * register access, then wait for BSY to clear
	 */
	timeout = jiffies + ATA_TMOUT_BOOT;
	while (dev1) {
		u8 nsect, lbal;

		__ata_dev_select(ap, 1);
		if (ap->flags & ATA_FLAG_MMIO) {
			nsect = readb((void *) ioaddr->nsect_addr);
			lbal = readb((void *) ioaddr->lbal_addr);
		} else {
			nsect = inb(ioaddr->nsect_addr);
			lbal = inb(ioaddr->lbal_addr);
		}
		if ((nsect == 1) && (lbal == 1))
			break;
		if (time_after(jiffies, timeout)) {
			dev1 = 0;
			break;
		}
		msleep(50);	/* give drive a breather */
	}
	if (dev1)
		ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* is all this really necessary? */
	__ata_dev_select(ap, 0);
	if (dev1)
		__ata_dev_select(ap, 1);
	if (dev0)
		__ata_dev_select(ap, 0);
}

/**
 *	ata_bus_edd -
 *	@ap:
 *
 *	LOCKING:
 *
 */

static unsigned int ata_bus_edd(struct ata_port *ap)
{
	struct ata_taskfile tf;

	/* set up execute-device-diag (bus reset) taskfile */
	/* also, take interrupts to a known state (disabled) */
	DPRINTK("execute-device-diag\n");
	ata_tf_init(ap, &tf, 0);
	tf.ctl |= ATA_NIEN;
	tf.command = ATA_CMD_EDD;
	tf.protocol = ATA_PROT_NODATA;

	/* do bus reset */
	ata_tf_to_host(ap, &tf);

	/* spec says at least 2ms.  but who knows with those
	 * crazy ATAPI devices...
	 */
	msleep(150);

	return ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);
}

static unsigned int ata_bus_softreset(struct ata_port *ap,
				      unsigned int devmask)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->id);

	/* software reset.  causes dev0 to be selected */
	if (ap->flags & ATA_FLAG_MMIO) {
		writeb(ap->ctl, ioaddr->ctl_addr);
		udelay(20);	/* FIXME: flush */
		writeb(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
		udelay(20);	/* FIXME: flush */
		writeb(ap->ctl, ioaddr->ctl_addr);
	} else {
		outb(ap->ctl, ioaddr->ctl_addr);
		udelay(10);
		outb(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
		udelay(10);
		outb(ap->ctl, ioaddr->ctl_addr);
	}

	/* spec mandates ">= 2ms" before checking status.
	 * We wait 150ms, because that was the magic delay used for
	 * ATAPI devices in Hale Landis's ATADRVR, for the period of time
	 * between when the ATA command register is written, and then
	 * status is checked.  Because waiting for "a while" before
	 * checking status is fine, post SRST, we perform this magic
	 * delay here as well.
	 */
	msleep(150);

	ata_bus_post_reset(ap, devmask);

	return 0;
}

/**
 *	ata_bus_reset - reset host port and associated ATA channel
 *	@ap: port to reset
 *
 *	This is typically the first time we actually start issuing
 *	commands to the ATA channel.  We wait for BSY to clear, then
 *	issue EXECUTE DEVICE DIAGNOSTIC command, polling for its
 *	result.  Determine what devices, if any, are on the channel
 *	by looking at the device 0/1 error register.  Look at the signature
 *	stored in each device's taskfile registers, to determine if
 *	the device is ATA or ATAPI.
 *
 *	LOCKING:
 *	Inherited from caller.  Some functions called by this function
 *	obtain the host_set lock.
 *
 *	SIDE EFFECTS:
 *	Sets ATA_FLAG_PORT_DISABLED if bus reset fails.
 */

void ata_bus_reset(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	u8 err;
	unsigned int dev0, dev1 = 0, rc = 0, devmask = 0;

	DPRINTK("ENTER, host %u, port %u\n", ap->id, ap->port_no);

	/* determine if device 0/1 are present */
	if (ap->flags & ATA_FLAG_SATA_RESET)
		dev0 = 1;
	else {
		dev0 = ata_dev_devchk(ap, 0);
		if (slave_possible)
			dev1 = ata_dev_devchk(ap, 1);
	}

	if (dev0)
		devmask |= (1 << 0);
	if (dev1)
		devmask |= (1 << 1);

	/* select device 0 again */
	__ata_dev_select(ap, 0);

	/* issue bus reset */
	if (ap->flags & ATA_FLAG_SRST)
		rc = ata_bus_softreset(ap, devmask);
	else if ((ap->flags & ATA_FLAG_SATA_RESET) == 0) {
		/* set up device control */
		if (ap->flags & ATA_FLAG_MMIO)
			writeb(ap->ctl, ioaddr->ctl_addr);
		else
			outb(ap->ctl, ioaddr->ctl_addr);
		rc = ata_bus_edd(ap);
	}

	if (rc)
		goto err_out;

	/*
	 * determine by signature whether we have ATA or ATAPI devices
	 */
	err = ata_dev_try_classify(ap, 0);
	if ((slave_possible) && (err != 0x81))
		ata_dev_try_classify(ap, 1);

	/* re-enable interrupts */
	ata_irq_on(ap);

	/* is double-select really necessary? */
	if (ap->device[1].class != ATA_DEV_NONE)
		__ata_dev_select(ap, 1);
	if (ap->device[0].class != ATA_DEV_NONE)
		__ata_dev_select(ap, 0);

	/* if no devices were detected, disable this port */
	if ((ap->device[0].class == ATA_DEV_NONE) &&
	    (ap->device[1].class == ATA_DEV_NONE))
		goto err_out;

	if (ap->flags & (ATA_FLAG_SATA_RESET | ATA_FLAG_SRST)) {
		/* set up device control for ATA_FLAG_SATA_RESET */
		if (ap->flags & ATA_FLAG_MMIO)
			writeb(ap->ctl, ioaddr->ctl_addr);
		else
			outb(ap->ctl, ioaddr->ctl_addr);
	}

	DPRINTK("EXIT\n");
	return;

err_out:
	printk(KERN_ERR "ata%u: disabling port\n", ap->id);
	ap->ops->port_disable(ap);

	DPRINTK("EXIT\n");
}

/**
 *	ata_host_set_pio -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_host_set_pio(struct ata_port *ap)
{
	struct ata_device *master, *slave;
	unsigned int pio, i;
	u16 mask;

	master = &ap->device[0];
	slave = &ap->device[1];

	assert (ata_dev_present(master) || ata_dev_present(slave));

	mask = ap->pio_mask;
	if (ata_dev_present(master))
		mask &= (master->id[ATA_ID_PIO_MODES] & 0x03);
	if (ata_dev_present(slave))
		mask &= (slave->id[ATA_ID_PIO_MODES] & 0x03);

	/* require pio mode 3 or 4 support for host and all devices */
	if (mask == 0) {
		printk(KERN_WARNING "ata%u: no PIO3/4 support, ignoring\n",
		       ap->id);
		goto err_out;
	}

	pio = (mask & ATA_ID_PIO4) ? 4 : 3;
	for (i = 0; i < ATA_MAX_DEVICES; i++)
		if (ata_dev_present(&ap->device[i])) {
			ap->device[i].pio_mode = (pio == 3) ?
				XFER_PIO_3 : XFER_PIO_4;
			if (ap->ops->set_piomode)
				ap->ops->set_piomode(ap, &ap->device[i], pio);
		}

	return;

err_out:
	ap->ops->port_disable(ap);
}

/**
 *	ata_host_set_udma -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_host_set_udma(struct ata_port *ap)
{
	struct ata_device *master, *slave;
	u16 mask;
	unsigned int i, j;
	int udma_mode = -1;

	master = &ap->device[0];
	slave = &ap->device[1];

	assert (ata_dev_present(master) || ata_dev_present(slave));
	assert ((ap->flags & ATA_FLAG_PORT_DISABLED) == 0);

	DPRINTK("udma masks: host 0x%X, master 0x%X, slave 0x%X\n",
		ap->udma_mask,
		(!ata_dev_present(master)) ? 0xff :
			(master->id[ATA_ID_UDMA_MODES] & 0xff),
		(!ata_dev_present(slave)) ? 0xff :
			(slave->id[ATA_ID_UDMA_MODES] & 0xff));

	mask = ap->udma_mask;
	if (ata_dev_present(master))
		mask &= (master->id[ATA_ID_UDMA_MODES] & 0xff);
	if (ata_dev_present(slave))
		mask &= (slave->id[ATA_ID_UDMA_MODES] & 0xff);

	i = XFER_UDMA_7;
	while (i >= XFER_UDMA_0) {
		j = i - XFER_UDMA_0;
		DPRINTK("mask 0x%X i 0x%X j %u\n", mask, i, j);
		if (mask & (1 << j)) {
			udma_mode = i;
			break;
		}

		i--;
	}

	/* require udma for host and all attached devices */
	if (udma_mode < 0) {
		printk(KERN_WARNING "ata%u: no UltraDMA support, ignoring\n",
		       ap->id);
		goto err_out;
	}

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		if (ata_dev_present(&ap->device[i])) {
			ap->device[i].udma_mode = udma_mode;
			if (ap->ops->set_udmamode)
				ap->ops->set_udmamode(ap, &ap->device[i],
						      udma_mode);
		}

	return;

err_out:
	ap->ops->port_disable(ap);
}

/**
 *	ata_dev_set_xfermode -
 *	@ap:
 *	@dev:
 *
 *	LOCKING:
 */

static void ata_dev_set_xfermode(struct ata_port *ap, struct ata_device *dev)
{
	struct ata_taskfile tf;

	/* set up set-features taskfile */
	DPRINTK("set features - xfer mode\n");
	ata_tf_init(ap, &tf, dev->devno);
	tf.ctl |= ATA_NIEN;
	tf.command = ATA_CMD_SET_FEATURES;
	tf.feature = SETFEATURES_XFER;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.protocol = ATA_PROT_NODATA;
	if (dev->flags & ATA_DFLAG_PIO)
		tf.nsect = dev->pio_mode;
	else
		tf.nsect = dev->udma_mode;

	/* do bus reset */
	ata_tf_to_host(ap, &tf);

	/* crazy ATAPI devices... */
	if (dev->class == ATA_DEV_ATAPI)
		msleep(150);

	ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	ata_irq_on(ap);	/* re-enable interrupts */

	ata_wait_idle(ap);

	DPRINTK("EXIT\n");
}

/**
 *	ata_dev_set_udma -
 *	@ap:
 *	@device:
 *
 *	LOCKING:
 */

static void ata_dev_set_udma(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];

	if (!ata_dev_present(dev) || (ap->flags & ATA_FLAG_PORT_DISABLED))
		return;

	ata_dev_set_xfermode(ap, dev);

	assert((dev->udma_mode >= XFER_UDMA_0) &&
	       (dev->udma_mode <= XFER_UDMA_7));
	printk(KERN_INFO "ata%u: dev %u configured for %s\n",
	       ap->id, device,
	       udma_str[dev->udma_mode - XFER_UDMA_0]);
}

/**
 *	ata_dev_set_pio -
 *	@ap:
 *	@device:
 *
 *	LOCKING:
 */

static void ata_dev_set_pio(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];

	if (!ata_dev_present(dev) || (ap->flags & ATA_FLAG_PORT_DISABLED))
		return;

	/* force PIO mode */
	dev->flags |= ATA_DFLAG_PIO;

	ata_dev_set_xfermode(ap, dev);

	assert((dev->pio_mode >= XFER_PIO_3) &&
	       (dev->pio_mode <= XFER_PIO_4));
	printk(KERN_INFO "ata%u: dev %u configured for PIO%c\n",
	       ap->id, device,
	       dev->pio_mode == 3 ? '3' : '4');
}

/**
 *	ata_sg_clean -
 *	@qc:
 *
 *	LOCKING:
 */

static void ata_sg_clean(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct scatterlist *sg = qc->sg;
	int dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);

	assert(dir == SCSI_DATA_READ || dir == SCSI_DATA_WRITE);
	assert(qc->flags & ATA_QCFLAG_SG);
	assert(sg != NULL);

	if (!cmd->use_sg)
		assert(qc->n_elem == 1);

	DPRINTK("unmapping %u sg elements\n", qc->n_elem);

	if (cmd->use_sg)
		pci_unmap_sg(ap->host_set->pdev, sg, qc->n_elem, dir);
	else
		pci_unmap_single(ap->host_set->pdev, sg_dma_address(&sg[0]),
				 sg_dma_len(&sg[0]), dir);

	qc->flags &= ~ATA_QCFLAG_SG;
	qc->sg = NULL;
}

/**
 *	ata_fill_sg -
 *	@qc:
 *
 *	LOCKING:
 *
 */
void ata_fill_sg(struct ata_queued_cmd *qc)
{
	struct scatterlist *sg = qc->sg;
	struct ata_port *ap = qc->ap;
	unsigned int idx, nelem;

	assert(sg != NULL);
	assert(qc->n_elem > 0);

	idx = 0;
	for (nelem = qc->n_elem; nelem; nelem--,sg++) {
		u32 addr, boundary;
		u32 sg_len, len;

		/* determine if physical DMA addr spans 64K boundary.
		 * Note h/w doesn't support 64-bit, so we unconditionally
		 * truncate dma_addr_t to u32.
		 */
		addr = (u32) sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		while (sg_len) {
			boundary = (addr & ~0xffff) + (0xffff + 1);
			len = sg_len;
			if ((addr + sg_len) > boundary)
				len = boundary - addr;

			ap->prd[idx].addr = cpu_to_le32(addr);
			ap->prd[idx].flags_len = cpu_to_le32(len & 0xffff);
			VPRINTK("PRD[%u] = (0x%X, 0x%X)\n", idx, addr, len);

			idx++;
			sg_len -= len;
			addr += len;
		}
	}

	if (idx)
		ap->prd[idx - 1].flags_len |= cpu_to_le32(ATA_PRD_EOT);
}

/**
 *	ata_sg_setup_one -
 *	@qc:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *
 */

static int ata_sg_setup_one(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;
	int dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
	struct scatterlist *sg = qc->sg;
	unsigned int have_sg = (qc->flags & ATA_QCFLAG_SG);
	dma_addr_t dma_address;

	assert(sg == &qc->sgent);
	assert(qc->n_elem == 1);

	sg->page = virt_to_page(cmd->request_buffer);
	sg->offset = (unsigned long) cmd->request_buffer & ~PAGE_MASK;
	sg_dma_len(sg) = cmd->request_bufflen;

	if (!have_sg)
		return 0;

	dma_address = pci_map_single(ap->host_set->pdev, cmd->request_buffer,
				     cmd->request_bufflen, dir);
	if (pci_dma_mapping_error(dma_address))
		return -1;

	sg_dma_address(sg) = dma_address;

	DPRINTK("mapped buffer of %d bytes for %s\n", cmd->request_bufflen,
		qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read");

	return 0;
}

/**
 *	ata_sg_setup -
 *	@qc:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *
 */

static int ata_sg_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct scatterlist *sg;
	int n_elem;
	unsigned int have_sg = (qc->flags & ATA_QCFLAG_SG);

	VPRINTK("ENTER, ata%u, use_sg %d\n", ap->id, cmd->use_sg);
	assert(cmd->use_sg > 0);

	sg = (struct scatterlist *)cmd->request_buffer;
	if (have_sg) {
		int dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
		n_elem = pci_map_sg(ap->host_set->pdev, sg, cmd->use_sg, dir);
		if (n_elem < 1)
			return -1;
		DPRINTK("%d sg elements mapped\n", n_elem);
	} else {
		n_elem = cmd->use_sg;
	}
	qc->n_elem = n_elem;

	return 0;
}

/**
 *	ata_pio_poll -
 *	@ap:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static unsigned long ata_pio_poll(struct ata_port *ap)
{
	u8 status;
	unsigned int poll_state = PIO_ST_UNKNOWN;
	unsigned int reg_state = PIO_ST_UNKNOWN;
	const unsigned int tmout_state = PIO_ST_TMOUT;

	switch (ap->pio_task_state) {
	case PIO_ST:
	case PIO_ST_POLL:
		poll_state = PIO_ST_POLL;
		reg_state = PIO_ST;
		break;
	case PIO_ST_LAST:
	case PIO_ST_LAST_POLL:
		poll_state = PIO_ST_LAST_POLL;
		reg_state = PIO_ST_LAST;
		break;
	default:
		BUG();
		break;
	}

	status = ata_chk_status(ap);
	if (status & ATA_BUSY) {
		if (time_after(jiffies, ap->pio_task_timeout)) {
			ap->pio_task_state = tmout_state;
			return 0;
		}
		ap->pio_task_state = poll_state;
		return ATA_SHORT_PAUSE;
	}

	ap->pio_task_state = reg_state;
	return 0;
}

/**
 *	ata_pio_complete -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_pio_complete (struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	u8 drv_stat;

	/*
	 * This is purely hueristic.  This is a fast path.
	 * Sometimes when we enter, BSY will be cleared in
	 * a chk-status or two.  If not, the drive is probably seeking
	 * or something.  Snooze for a couple msecs, then
	 * chk-status again.  If still busy, fall back to
	 * PIO_ST_POLL state.
	 */
	drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 10);
	if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
		msleep(2);
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 10);
		if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
			ap->pio_task_state = PIO_ST_LAST_POLL;
			ap->pio_task_timeout = jiffies + ATA_TMOUT_PIO;
			return;
		}
	}

	drv_stat = ata_wait_idle(ap);
	if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
		ap->pio_task_state = PIO_ST_ERR;
		return;
	}

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc != NULL);

	ap->pio_task_state = PIO_ST_IDLE;

	ata_irq_on(ap);

	ata_qc_complete(qc, drv_stat, 0);
}

/**
 *	ata_pio_sector -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_pio_sector(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	struct scatterlist *sg;
	struct scsi_cmnd *cmd;
	unsigned char *buf;
	u8 status;

	/*
	 * This is purely hueristic.  This is a fast path.
	 * Sometimes when we enter, BSY will be cleared in
	 * a chk-status or two.  If not, the drive is probably seeking
	 * or something.  Snooze for a couple msecs, then
	 * chk-status again.  If still busy, fall back to
	 * PIO_ST_POLL state.
	 */
	status = ata_busy_wait(ap, ATA_BUSY, 5);
	if (status & ATA_BUSY) {
		msleep(2);
		status = ata_busy_wait(ap, ATA_BUSY, 10);
		if (status & ATA_BUSY) {
			ap->pio_task_state = PIO_ST_POLL;
			ap->pio_task_timeout = jiffies + ATA_TMOUT_PIO;
			return;
		}
	}

	/* handle BSY=0, DRQ=0 as error */
	if ((status & ATA_DRQ) == 0) {
		ap->pio_task_state = PIO_ST_ERR;
		return;
	}

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc != NULL);

	cmd = qc->scsicmd;
	sg = qc->sg;

	if (qc->cursect == (qc->nsect - 1))
		ap->pio_task_state = PIO_ST_LAST;

	buf = kmap(sg[qc->cursg].page) +
	      sg[qc->cursg].offset + (qc->cursg_ofs * ATA_SECT_SIZE);

	qc->cursect++;
	qc->cursg_ofs++;

	if (cmd->use_sg)
		if ((qc->cursg_ofs * ATA_SECT_SIZE) == sg_dma_len(&sg[qc->cursg])) {
			qc->cursg++;
			qc->cursg_ofs = 0;
		}

	DPRINTK("data %s, drv_stat 0x%X\n",
		qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read",
		status);

	/* do the actual data transfer */
	/* FIXME: mmio-ize */
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		outsl(ap->ioaddr.data_addr, buf, ATA_SECT_DWORDS);
	else
		insl(ap->ioaddr.data_addr, buf, ATA_SECT_DWORDS);

	kunmap(sg[qc->cursg].page);
}

static void ata_pio_task(void *_data)
{
	struct ata_port *ap = _data;
	unsigned long timeout = 0;

	switch (ap->pio_task_state) {
	case PIO_ST:
		ata_pio_sector(ap);
		break;

	case PIO_ST_LAST:
		ata_pio_complete(ap);
		break;

	case PIO_ST_POLL:
	case PIO_ST_LAST_POLL:
		timeout = ata_pio_poll(ap);
		break;

	case PIO_ST_TMOUT:
		printk(KERN_ERR "ata%d: FIXME: PIO_ST_TMOUT\n", /* FIXME */
		       ap->id);
		timeout = 11 * HZ;
		break;

	case PIO_ST_ERR:
		printk(KERN_ERR "ata%d: FIXME: PIO_ST_ERR\n", /* FIXME */
		       ap->id);
		timeout = 11 * HZ;
		break;
	}

	if ((ap->pio_task_state != PIO_ST_IDLE) &&
	    (ap->pio_task_state != PIO_ST_TMOUT) &&
	    (ap->pio_task_state != PIO_ST_ERR)) {
		if (timeout)
			queue_delayed_work(ata_wq, &ap->pio_task,
					   timeout);
		else
			queue_work(ata_wq, &ap->pio_task);
	}
}

/**
 *	ata_eng_timeout - Handle timeout of queued command
 *	@ap: Port on which timed-out command is active
 *
 *	Some part of the kernel (currently, only the SCSI layer)
 *	has noticed that the active command on port @ap has not
 *	completed after a specified length of time.  Handle this
 *	condition by disabling DMA (if necessary) and completing
 *	transactions, with error if necessary.
 *
 *	This also handles the case of the "lost interrupt", where
 *	for some reason (possibly hardware bug, possibly driver bug)
 *	an interrupt was not delivered to the driver, even though the
 *	transaction completed successfully.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 */

void ata_eng_timeout(struct ata_port *ap)
{
	u8 host_stat, drv_stat;
	struct ata_queued_cmd *qc;

	DPRINTK("ENTER\n");

	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (!qc) {
		printk(KERN_ERR "ata%u: BUG: timeout without command\n",
		       ap->id);
		goto out;
	}

	/* hack alert!  We cannot use the supplied completion
	 * function from inside the ->eh_strategy_handler() thread.
	 * libata is the only user of ->eh_strategy_handler() in
	 * any kernel, so the default scsi_done() assumes it is
	 * not being called from the SCSI EH.
	 */
	qc->scsidone = scsi_finish_command;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		if (ap->flags & ATA_FLAG_MMIO) {
			void *mmio = (void *) ap->ioaddr.bmdma_addr;
			host_stat = readb(mmio + ATA_DMA_STATUS);
		} else
			host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

		printk(KERN_ERR "ata%u: DMA timeout, stat 0x%x\n",
		       ap->id, host_stat);

		ata_dma_complete(ap, host_stat, 1);
		break;

	case ATA_PROT_NODATA:
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

		printk(KERN_ERR "ata%u: command 0x%x timeout, stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat);

		ata_qc_complete(qc, drv_stat, 1);
		break;

	default:
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

		printk(KERN_ERR "ata%u: unknown timeout, cmd 0x%x stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat);

		ata_qc_complete(qc, drv_stat, 1);
		break;
	}

out:
	DPRINTK("EXIT\n");
}

/**
 *	ata_qc_new -
 *	@ap:
 *	@dev:
 *
 *	LOCKING:
 */

static struct ata_queued_cmd *ata_qc_new(struct ata_port *ap)
{
	struct ata_queued_cmd *qc = NULL;
	unsigned int i;

	for (i = 0; i < ATA_MAX_QUEUE; i++)
		if (!test_and_set_bit(i, &ap->qactive)) {
			qc = ata_qc_from_tag(ap, i);
			break;
		}

	if (qc)
		qc->tag = i;

	return qc;
}

/**
 *	ata_qc_new_init -
 *	@ap:
 *	@dev:
 *
 *	LOCKING:
 */

struct ata_queued_cmd *ata_qc_new_init(struct ata_port *ap,
				      struct ata_device *dev)
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_new(ap);
	if (qc) {
		qc->sg = NULL;
		qc->flags = 0;
		qc->scsicmd = NULL;
		qc->ap = ap;
		qc->dev = dev;
		qc->cursect = qc->cursg = qc->cursg_ofs = 0;
		qc->nsect = 0;

		ata_tf_init(ap, &qc->tf, dev->devno);

		if (likely((dev->flags & ATA_DFLAG_PIO) == 0))
			qc->flags |= ATA_QCFLAG_DMA;
		if (dev->flags & ATA_DFLAG_LBA48)
			qc->tf.flags |= ATA_TFLAG_LBA48;
	}

	return qc;
}

/**
 *	ata_qc_complete -
 *	@qc:
 *	@drv_stat:
 *	@done_late:
 *
 *	LOCKING:
 *
 */

void ata_qc_complete(struct ata_queued_cmd *qc, u8 drv_stat, unsigned int done_late)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;
	unsigned int tag, do_clear = 0;

	assert(qc != NULL);	/* ata_qc_from_tag _might_ return NULL */
	assert(qc->flags & ATA_QCFLAG_ACTIVE);

	if (likely(qc->flags & ATA_QCFLAG_SG))
		ata_sg_clean(qc);

	if (cmd) {
		if (unlikely(drv_stat & (ATA_ERR | ATA_BUSY | ATA_DRQ))) {
			if (qc->flags & ATA_QCFLAG_ATAPI)
				cmd->result = SAM_STAT_CHECK_CONDITION;
			else
				ata_to_sense_error(qc);
		} else {
			cmd->result = SAM_STAT_GOOD;
		}

		qc->scsidone(cmd);
	}

	qc->flags &= ~ATA_QCFLAG_ACTIVE;
	tag = qc->tag;
	if (likely(ata_tag_valid(tag))) {
		if (tag == ap->active_tag)
			ap->active_tag = ATA_TAG_POISON;
		qc->tag = ATA_TAG_POISON;
		do_clear = 1;
	}

	if (qc->waiting)
		complete(qc->waiting);

	if (likely(do_clear))
		clear_bit(tag, &ap->qactive);
}

/**
 *	ata_qc_issue - issue taskfile to device
 *	@qc: command to issue to device
 *
 *	Prepare an ATA command to submission to device.
 *	This includes mapping the data into a DMA-able
 *	area, filling in the S/G table, and finally
 *	writing the taskfile to hardware, starting the command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, negative on error.
 */

int ata_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;

	/* set up SG table */
	if (cmd->use_sg) {
		if (ata_sg_setup(qc))
			goto err_out;
	} else {
		if (ata_sg_setup_one(qc))
			goto err_out;
	}

	ap->ops->fill_sg(qc);

	qc->ap->active_tag = qc->tag;
	qc->flags |= ATA_QCFLAG_ACTIVE;

	return ata_qc_issue_prot(qc);

err_out:
	return -1;
}

/**
 *	ata_qc_issue_prot - issue taskfile to device in proto-dependent manner
 *	@qc: command to issue to device
 *
 *	Using various libata functions and hooks, this function
 *	starts an ATA command.  ATA commands are grouped into
 *	classes called "protocols", and issuing each type of protocol
 *	is slightly different.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, negative on error.
 */

static int ata_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	ata_dev_select(ap, qc->dev->devno, 1, 0);

	switch (qc->tf.protocol) {
	case ATA_PROT_NODATA:
		ata_tf_to_host_nolock(ap, &qc->tf);
		break;

	case ATA_PROT_DMA:
		ap->ops->tf_load(ap, &qc->tf);	 /* load tf registers */
		ap->ops->bmdma_setup(qc);	    /* initiate bmdma */
		ap->ops->bmdma_start(qc);	    /* initiate bmdma */
		break;

	case ATA_PROT_PIO: /* load tf registers, initiate polling pio */
		qc->flags |= ATA_QCFLAG_POLL;
		qc->tf.ctl |= ATA_NIEN;	/* disable interrupts */
		ata_tf_to_host_nolock(ap, &qc->tf);
		ap->pio_task_state = PIO_ST;
		queue_work(ata_wq, &ap->pio_task);
		break;

	default:
		WARN_ON(1);
		return -1;
	}

	return 0;
}

/**
 *	ata_bmdma_setup_mmio - Set up PCI IDE BMDMA transaction (MMIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_setup_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 host_stat, dmactl;
	void *mmio = (void *) ap->ioaddr.bmdma_addr;

	/* load PRD table addr. */
	mb();	/* make sure PRD table writes are visible to controller */
	writel(ap->prd_dma, mmio + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = readb(mmio + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	writeb(dmactl, mmio + ATA_DMA_CMD);

	/* clear interrupt, error bits */
	host_stat = readb(mmio + ATA_DMA_STATUS);
	writeb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR, mmio + ATA_DMA_STATUS);

	/* issue r/w command */
	ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	ata_bmdma_start_mmio - Start a PCI IDE BMDMA transaction (MMIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_start_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void *mmio = (void *) ap->ioaddr.bmdma_addr;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = readb(mmio + ATA_DMA_CMD);
	writeb(dmactl | ATA_DMA_START, mmio + ATA_DMA_CMD);

	/* Strictly, one may wish to issue a readb() here, to
	 * flush the mmio write.  However, control also passes
	 * to the hardware at this point, and it will interrupt
	 * us when we are to resume control.  So, in effect,
	 * we don't care when the mmio write flushes.
	 * Further, a read of the DMA status register _immediately_
	 * following the write may not be what certain flaky hardware
	 * is expected, so I think it is best to not add a readb()
	 * without first all the MMIO ATA cards/mobos.
	 * Or maybe I'm just being paranoid.
	 */
}

/**
 *	ata_bmdma_setup_pio - Set up PCI IDE BMDMA transaction (PIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_setup_pio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 host_stat, dmactl;

	/* load PRD table addr. */
	outl(ap->prd_dma, ap->ioaddr.bmdma_addr + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	outb(dmactl, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	/* clear interrupt, error bits */
	host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	outb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
	     ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

	/* issue r/w command */
	ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	ata_bmdma_start_pio - Start a PCI IDE BMDMA transaction (PIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_start_pio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	outb(dmactl | ATA_DMA_START,
	     ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
}

/**
 *	ata_dma_complete -
 *	@ap:
 *	@host_stat:
 *	@done_late:
 *
 *	LOCKING:
 */

static void ata_dma_complete(struct ata_port *ap, u8 host_stat,
			     unsigned int done_late)
{
	VPRINTK("ENTER\n");

	if (ap->flags & ATA_FLAG_MMIO) {
		void *mmio = (void *) ap->ioaddr.bmdma_addr;

		/* clear start/stop bit */
		writeb(readb(mmio + ATA_DMA_CMD) & ~ATA_DMA_START,
		       mmio + ATA_DMA_CMD);

		/* ack intr, err bits */
		writeb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
		       mmio + ATA_DMA_STATUS);
	} else {
		/* clear start/stop bit */
		outb(inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD) & ~ATA_DMA_START,
		     ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

		/* ack intr, err bits */
		outb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
		     ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	}


	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_altstatus(ap);		/* dummy read */

	DPRINTK("host %u, host_stat==0x%X, drv_stat==0x%X\n",
		ap->id, (u32) host_stat, (u32) ata_chk_status(ap));

	/* get drive status; clear intr; complete txn */
	ata_qc_complete(ata_qc_from_tag(ap, ap->active_tag),
			ata_wait_idle(ap), done_late);
}

/**
 *	ata_host_intr - Handle host interrupt for given (port, task)
 *	@ap: Port on which interrupt arrived (possibly...)
 *	@qc: Taskfile currently active in engine
 *
 *	Handle host interrupt for given queued command.  Currently,
 *	only DMA interrupts are handled.  All other commands are
 *	handled via polling with interrupts disabled (nIEN bit).
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	One if interrupt was handled, zero if not (shared irq).
 */

inline unsigned int ata_host_intr (struct ata_port *ap,
				   struct ata_queued_cmd *qc)
{
	u8 status, host_stat;
	unsigned int handled = 0;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		if (ap->flags & ATA_FLAG_MMIO) {
			void *mmio = (void *) ap->ioaddr.bmdma_addr;
			host_stat = readb(mmio + ATA_DMA_STATUS);
		} else
			host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
		VPRINTK("BUS_DMA (host_stat 0x%X)\n", host_stat);

		if (!(host_stat & ATA_DMA_INTR)) {
			ap->stats.idle_irq++;
			break;
		}

		ata_dma_complete(ap, host_stat, 0);
		handled = 1;
		break;

	case ATA_PROT_NODATA:	/* command completion, but no data xfer */
		status = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);
		DPRINTK("BUS_NODATA (drv_stat 0x%X)\n", status);
		ata_qc_complete(qc, status, 0);
		handled = 1;
		break;

	default:
		ap->stats.idle_irq++;

#ifdef ATA_IRQ_TRAP
		if ((ap->stats.idle_irq % 1000) == 0) {
			handled = 1;
			ata_irq_ack(ap, 0); /* debug trap */
			printk(KERN_WARNING "ata%d: irq trap\n", ap->id);
		}
#endif
		break;
	}

	return handled;
}

/**
 *	ata_interrupt -
 *	@irq:
 *	@dev_instance:
 *	@regs:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

irqreturn_t ata_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	/* TODO: make _irqsave conditional on x86 PCI IDE legacy mode */
	spin_lock_irqsave(&host_set->lock, flags);

	for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap;

		ap = host_set->ports[i];
		if (ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED))) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && ((qc->flags & ATA_QCFLAG_POLL) == 0))
				handled += ata_host_intr(ap, qc);
		}
	}

	spin_unlock_irqrestore(&host_set->lock, flags);

	return IRQ_RETVAL(handled);
}

/**
 *	ata_thread_iter -
 *	@ap:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static unsigned long ata_thread_iter(struct ata_port *ap)
{
	long timeout = 0;

	DPRINTK("ata%u: thr_state %s\n",
		ap->id, ata_thr_state_name(ap->thr_state));

	switch (ap->thr_state) {
	case THR_UNKNOWN:
		ap->thr_state = THR_PORT_RESET;
		break;

	case THR_PROBE_START:
		ap->thr_state = THR_PORT_RESET;
		break;

	case THR_PORT_RESET:
		ata_port_reset(ap);
		break;

	case THR_PROBE_SUCCESS:
		up(&ap->probe_sem);
		ap->thr_state = THR_IDLE;
		break;

	case THR_PROBE_FAILED:
		up(&ap->probe_sem);
		ap->thr_state = THR_AWAIT_DEATH;
		break;

	case THR_AWAIT_DEATH:
	case THR_IDLE:
		timeout = -1;
		break;

	default:
		printk(KERN_DEBUG "ata%u: unknown thr state %s\n",
		       ap->id, ata_thr_state_name(ap->thr_state));
		break;
	}

	DPRINTK("ata%u: new thr_state %s, returning %ld\n",
		ap->id, ata_thr_state_name(ap->thr_state), timeout);
	return timeout;
}

void atapi_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	qc->flags |= ATA_QCFLAG_ACTIVE;
	ap->active_tag = qc->tag;

	ata_dev_select(ap, qc->dev->devno, 1, 0);
	ata_tf_to_host_nolock(ap, &qc->tf);
	queue_work(ata_wq, &ap->packet_task);

	VPRINTK("EXIT\n");
}

/**
 *	atapi_packet_task - Write CDB bytes to hardware
 *	@_data: Port to which ATAPI device is attached.
 *
 *	When device has indicated its readiness to accept
 *	a CDB, this function is called.  Send the CDB.
 *	If DMA is to be performed, exit immediately.
 *	Otherwise, we are in polling mode, so poll
 *	status under operation succeeds or fails.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */

static void atapi_packet_task(void *_data)
{
	struct ata_port *ap = _data;
	struct ata_queued_cmd *qc;
	u8 status;

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc != NULL);
	assert(qc->flags & ATA_QCFLAG_ACTIVE);

	/* sleep-wait for BSY to clear */
	DPRINTK("busy wait\n");
	if (ata_busy_sleep(ap, ATA_TMOUT_CDB_QUICK, ATA_TMOUT_CDB))
		goto err_out;

	/* make sure DRQ is set */
	status = ata_chk_status(ap);
	if ((status & ATA_DRQ) == 0)
		goto err_out;

	/* send SCSI cdb */
	/* FIXME: mmio-ize */
	DPRINTK("send cdb\n");
	outsl(ap->ioaddr.data_addr,
	      qc->scsicmd->cmnd, ap->host->max_cmd_len / 4);

	/* if we are DMA'ing, irq handler takes over from here */
	if (qc->tf.protocol == ATA_PROT_ATAPI_DMA) {
		/* FIXME: start DMA here */
	} else {
		ap->pio_task_state = PIO_ST;
		queue_work(ata_wq, &ap->pio_task);
	}

	return;

err_out:
	ata_qc_complete(qc, ATA_ERR, 0);
}

int ata_port_start (struct ata_port *ap)
{
	struct pci_dev *pdev = ap->host_set->pdev;

	ap->prd = pci_alloc_consistent(pdev, ATA_PRD_TBL_SZ, &ap->prd_dma);
	if (!ap->prd)
		return -ENOMEM;

	DPRINTK("prd alloc, virt %p, dma %llx\n", ap->prd, (unsigned long long) ap->prd_dma);

	return 0;
}

void ata_port_stop (struct ata_port *ap)
{
	struct pci_dev *pdev = ap->host_set->pdev;

	pci_free_consistent(pdev, ATA_PRD_TBL_SZ, ap->prd, ap->prd_dma);
}

static void ata_probe_task(void *_data)
{
	struct ata_port *ap = _data;
	long timeout;

	timeout = ata_thread_iter(ap);
	if (timeout < 0)
		return;

	if (timeout > 0)
		queue_delayed_work(ata_wq, &ap->probe_task, timeout);
	else
		queue_work(ata_wq, &ap->probe_task);
}

/**
 *	ata_host_remove -
 *	@ap:
 *	@do_unregister:
 *
 *	LOCKING:
 */

static void ata_host_remove(struct ata_port *ap, unsigned int do_unregister)
{
	struct Scsi_Host *sh = ap->host;

	DPRINTK("ENTER\n");

	if (do_unregister)
		scsi_remove_host(sh);

	ap->ops->port_stop(ap);
}

/**
 *	ata_host_init -
 *	@host:
 *	@ent:
 *	@port_no:
 *
 *	LOCKING:
 *
 */

static void ata_host_init(struct ata_port *ap, struct Scsi_Host *host,
			  struct ata_host_set *host_set,
			  struct ata_probe_ent *ent, unsigned int port_no)
{
	unsigned int i;

	host->max_id = 16;
	host->max_lun = 1;
	host->max_channel = 1;
	host->unique_id = ata_unique_id++;
	host->max_cmd_len = 12;
	scsi_set_device(host, &ent->pdev->dev);
	scsi_assign_lock(host, &host_set->lock);

	ap->flags = ATA_FLAG_PORT_DISABLED;
	ap->id = host->unique_id;
	ap->host = host;
	ap->ctl = ATA_DEVCTL_OBS;
	ap->host_set = host_set;
	ap->port_no = port_no;
	ap->pio_mask = ent->pio_mask;
	ap->udma_mask = ent->udma_mask;
	ap->flags |= ent->host_flags;
	ap->ops = ent->port_ops;
	ap->thr_state = THR_PROBE_START;
	ap->cbl = ATA_CBL_NONE;
	ap->device[0].flags = ATA_DFLAG_MASTER;
	ap->active_tag = ATA_TAG_POISON;
	ap->last_ctl = 0xFF;

	INIT_WORK(&ap->packet_task, atapi_packet_task, ap);
	INIT_WORK(&ap->pio_task, ata_pio_task, ap);
	INIT_WORK(&ap->probe_task, ata_probe_task, ap);

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		ap->device[i].devno = i;

	init_MUTEX_LOCKED(&ap->probe_sem);

#ifdef ATA_IRQ_TRAP
	ap->stats.unhandled_irq = 1;
	ap->stats.idle_irq = 1;
#endif

	memcpy(&ap->ioaddr, &ent->port[port_no], sizeof(struct ata_ioports));
}

/**
 *	ata_host_add -
 *	@ent:
 *	@host_set:
 *	@port_no:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static struct ata_port * ata_host_add(struct ata_probe_ent *ent,
				      struct ata_host_set *host_set,
				      unsigned int port_no)
{
	struct Scsi_Host *host;
	struct ata_port *ap;
	int rc;

	DPRINTK("ENTER\n");
	host = scsi_host_alloc(ent->sht, sizeof(struct ata_port));
	if (!host)
		return NULL;

	ap = (struct ata_port *) &host->hostdata[0];

	ata_host_init(ap, host, host_set, ent, port_no);

	rc = ap->ops->port_start(ap);
	if (rc)
		goto err_out;

	return ap;

err_out:
	scsi_host_put(host);
	return NULL;
}

/**
 *	ata_device_add -
 *	@ent:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

int ata_device_add(struct ata_probe_ent *ent)
{
	unsigned int count = 0, i;
	struct pci_dev *pdev = ent->pdev;
	struct ata_host_set *host_set;

	DPRINTK("ENTER\n");
	/* alloc a container for our list of ATA ports (buses) */
	host_set = kmalloc(sizeof(struct ata_host_set) +
			   (ent->n_ports * sizeof(void *)), GFP_KERNEL);
	if (!host_set)
		return 0;
	memset(host_set, 0, sizeof(struct ata_host_set) + (ent->n_ports * sizeof(void *)));
	spin_lock_init(&host_set->lock);

	host_set->pdev = pdev;
	host_set->n_ports = ent->n_ports;
	host_set->irq = ent->irq;
	host_set->mmio_base = ent->mmio_base;
	host_set->private_data = ent->private_data;

	/* register each port bound to this device */
	for (i = 0; i < ent->n_ports; i++) {
		struct ata_port *ap;

		ap = ata_host_add(ent, host_set, i);
		if (!ap)
			goto err_out;

		host_set->ports[i] = ap;

		/* print per-port info to dmesg */
		printk(KERN_INFO "ata%u: %cATA max %s cmd 0x%lX ctl 0x%lX "
				 "bmdma 0x%lX irq %lu\n",
			ap->id,
			ap->flags & ATA_FLAG_SATA ? 'S' : 'P',
			ata_udma_string(ent->udma_mask),
	       		ap->ioaddr.cmd_addr,
	       		ap->ioaddr.ctl_addr,
	       		ap->ioaddr.bmdma_addr,
	       		ent->irq);

		count++;
	}

	if (!count) {
		kfree(host_set);
		return 0;
	}

	/* obtain irq, that is shared between channels */
	if (request_irq(ent->irq, ent->port_ops->irq_handler, ent->irq_flags,
			DRV_NAME, host_set))
		goto err_out;

	/* perform each probe synchronously */
	DPRINTK("probe begin\n");
	for (i = 0; i < count; i++) {
		struct ata_port *ap;
		int rc;

		ap = host_set->ports[i];

		DPRINTK("ata%u: probe begin\n", ap->id);
		queue_work(ata_wq, &ap->probe_task);	/* start probe */

		DPRINTK("ata%u: probe-wait begin\n", ap->id);
		down(&ap->probe_sem);	/* wait for end */

		DPRINTK("ata%u: probe-wait end\n", ap->id);

		rc = scsi_add_host(ap->host, &pdev->dev);
		if (rc) {
			printk(KERN_ERR "ata%u: scsi_add_host failed\n",
			       ap->id);
			/* FIXME: do something useful here */
			/* FIXME: handle unconditional calls to
			 * scsi_scan_host and ata_host_remove, below,
			 * at the very least
			 */
		}
	}

	/* probes are done, now scan each port's disk(s) */
	DPRINTK("probe begin\n");
	for (i = 0; i < count; i++) {
		struct ata_port *ap = host_set->ports[i];

		scsi_scan_host(ap->host);
	}

	pci_set_drvdata(pdev, host_set);

	VPRINTK("EXIT, returning %u\n", ent->n_ports);
	return ent->n_ports; /* success */

err_out:
	for (i = 0; i < count; i++) {
		ata_host_remove(host_set->ports[i], 1);
		scsi_host_put(host_set->ports[i]->host);
	}
	kfree(host_set);
	VPRINTK("EXIT, returning 0\n");
	return 0;
}

/**
 *	ata_scsi_release - SCSI layer callback hook for host unload
 *	@host: libata host to be unloaded
 *
 *	Performs all duties necessary to shut down a libata port:
 *	Kill port kthread, disable port, and release resources.
 *
 *	LOCKING:
 *	Inherited from SCSI layer.
 *
 *	RETURNS:
 *	One.
 */

int ata_scsi_release(struct Scsi_Host *host)
{
	struct ata_port *ap = (struct ata_port *) &host->hostdata[0];

	DPRINTK("ENTER\n");

	ap->ops->port_disable(ap);
	ata_host_remove(ap, 0);

	DPRINTK("EXIT\n");
	return 1;
}

/**
 *	ata_std_ports - initialize ioaddr with standard port offsets.
 *	@ioaddr:
 */
void ata_std_ports(struct ata_ioports *ioaddr)
{
	ioaddr->data_addr = ioaddr->cmd_addr + ATA_REG_DATA;
	ioaddr->error_addr = ioaddr->cmd_addr + ATA_REG_ERR;
	ioaddr->feature_addr = ioaddr->cmd_addr + ATA_REG_FEATURE;
	ioaddr->nsect_addr = ioaddr->cmd_addr + ATA_REG_NSECT;
	ioaddr->lbal_addr = ioaddr->cmd_addr + ATA_REG_LBAL;
	ioaddr->lbam_addr = ioaddr->cmd_addr + ATA_REG_LBAM;
	ioaddr->lbah_addr = ioaddr->cmd_addr + ATA_REG_LBAH;
	ioaddr->device_addr = ioaddr->cmd_addr + ATA_REG_DEVICE;
	ioaddr->status_addr = ioaddr->cmd_addr + ATA_REG_STATUS;
	ioaddr->command_addr = ioaddr->cmd_addr + ATA_REG_CMD;
}

/**
 *	ata_pci_init_one -
 *	@pdev:
 *	@port_info:
 *	@n_ports:
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *
 */

int ata_pci_init_one (struct pci_dev *pdev, struct ata_port_info **port_info,
		      unsigned int n_ports)
{
	struct ata_probe_ent *probe_ent, *probe_ent2 = NULL;
	struct ata_port_info *port0, *port1;
	u8 tmp8, mask;
	unsigned int legacy_mode = 0;
	int rc;

	DPRINTK("ENTER\n");

	port0 = port_info[0];
	if (n_ports > 1)
		port1 = port_info[1];
	else
		port1 = port0;

	if ((port0->host_flags & ATA_FLAG_NO_LEGACY) == 0) {
		/* TODO: support transitioning to native mode? */
		pci_read_config_byte(pdev, PCI_CLASS_PROG, &tmp8);
		mask = (1 << 2) | (1 << 0);
		if ((tmp8 & mask) != mask)
			legacy_mode = (1 << 3);
	}

	/* FIXME... */
	if ((!legacy_mode) && (n_ports > 1)) {
		printk(KERN_ERR "ata: BUG: native mode, n_ports > 1\n");
		return -EINVAL;
	}

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	if (legacy_mode) {
		if (!request_region(0x1f0, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x1f0;
			res.end = 0x1f0 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 0);
			else
				printk(KERN_WARNING "ata: 0x1f0 IDE port busy\n");
		} else
			legacy_mode |= (1 << 0);

		if (!request_region(0x170, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x170;
			res.end = 0x170 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 1);
			else
				printk(KERN_WARNING "ata: 0x170 IDE port busy\n");
		} else
			legacy_mode |= (1 << 1);
	}

	/* we have legacy mode, but all ports are unavailable */
	if (legacy_mode == (1 << 3)) {
		rc = -EBUSY;
		goto err_out_regions;
	}

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->pdev = pdev;
	INIT_LIST_HEAD(&probe_ent->node);

	if (legacy_mode) {
		probe_ent2 = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
		if (!probe_ent2) {
			rc = -ENOMEM;
			goto err_out_free_ent;
		}

		memset(probe_ent2, 0, sizeof(*probe_ent));
		probe_ent2->pdev = pdev;
		INIT_LIST_HEAD(&probe_ent2->node);
	}

	probe_ent->port[0].bmdma_addr = pci_resource_start(pdev, 4);
	probe_ent->sht = port0->sht;
	probe_ent->host_flags = port0->host_flags;
	probe_ent->pio_mask = port0->pio_mask;
	probe_ent->udma_mask = port0->udma_mask;
	probe_ent->port_ops = port0->port_ops;

	if (legacy_mode) {
		probe_ent->port[0].cmd_addr = 0x1f0;
		probe_ent->port[0].altstatus_addr =
		probe_ent->port[0].ctl_addr = 0x3f6;
		probe_ent->n_ports = 1;
		probe_ent->irq = 14;
		ata_std_ports(&probe_ent->port[0]);

		probe_ent2->port[0].cmd_addr = 0x170;
		probe_ent2->port[0].altstatus_addr =
		probe_ent2->port[0].ctl_addr = 0x376;
		probe_ent2->port[0].bmdma_addr = pci_resource_start(pdev, 4)+8;
		probe_ent2->n_ports = 1;
		probe_ent2->irq = 15;
		ata_std_ports(&probe_ent2->port[0]);

		probe_ent2->sht = port1->sht;
		probe_ent2->host_flags = port1->host_flags;
		probe_ent2->pio_mask = port1->pio_mask;
		probe_ent2->udma_mask = port1->udma_mask;
		probe_ent2->port_ops = port1->port_ops;
	} else {
		probe_ent->port[0].cmd_addr = pci_resource_start(pdev, 0);
		ata_std_ports(&probe_ent->port[0]);
		probe_ent->port[0].altstatus_addr =
		probe_ent->port[0].ctl_addr =
			pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS;

		probe_ent->port[1].cmd_addr = pci_resource_start(pdev, 2);
		ata_std_ports(&probe_ent->port[1]);
		probe_ent->port[1].altstatus_addr =
		probe_ent->port[1].ctl_addr =
			pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS;
		probe_ent->port[1].bmdma_addr = pci_resource_start(pdev, 4) + 8;

		probe_ent->n_ports = 2;
		probe_ent->irq = pdev->irq;
		probe_ent->irq_flags = SA_SHIRQ;
	}

	pci_set_master(pdev);

	/* FIXME: check ata_device_add return */
	if (legacy_mode) {
		if (legacy_mode & (1 << 0))
			ata_device_add(probe_ent);
		if (legacy_mode & (1 << 1))
			ata_device_add(probe_ent2);
		kfree(probe_ent2);
	} else {
		ata_device_add(probe_ent);
		assert(probe_ent2 == NULL);
	}
	kfree(probe_ent);

	return 0;

err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	if (legacy_mode & (1 << 0))
		release_region(0x1f0, 8);
	if (legacy_mode & (1 << 1))
		release_region(0x170, 8);
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}

/**
 *	ata_pci_remove_one - PCI layer callback for device removal
 *	@pdev: PCI device that was removed
 *
 *	PCI layer indicates to libata via this hook that
 *	hot-unplug or module unload event has occured.
 *	Handle this by unregistering all objects associated
 *	with this PCI device.  Free those objects.  Then finally
 *	release PCI resources and disable device.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 */

void ata_pci_remove_one (struct pci_dev *pdev)
{
	struct ata_host_set *host_set = pci_get_drvdata(pdev);
	struct ata_port *ap;
	unsigned int i;

	for (i = 0; i < host_set->n_ports; i++) {
		ap = host_set->ports[i];

		scsi_remove_host(ap->host);
	}

	free_irq(host_set->irq, host_set);
	if (host_set->mmio_base)
		iounmap(host_set->mmio_base);
	if (host_set->ports[0]->ops->host_stop)
		host_set->ports[0]->ops->host_stop(host_set);

	for (i = 0; i < host_set->n_ports; i++) {
		ap = host_set->ports[i];

		ata_scsi_release(ap->host);
		scsi_host_put(ap->host);
	}

	pci_release_regions(pdev);

	for (i = 0; i < host_set->n_ports; i++) {
		struct ata_ioports *ioaddr;

		ap = host_set->ports[i];
		ioaddr = &ap->ioaddr;

		if ((ap->flags & ATA_FLAG_NO_LEGACY) == 0) {
			if (ioaddr->cmd_addr == 0x1f0)
				release_region(0x1f0, 8);
			else if (ioaddr->cmd_addr == 0x170)
				release_region(0x170, 8);
		}
	}

	kfree(host_set);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

/* move to PCI subsystem */
int pci_test_config_bits(struct pci_dev *pdev, struct pci_bits *bits)
{
	unsigned long tmp = 0;

	switch (bits->width) {
	case 1: {
		u8 tmp8 = 0;
		pci_read_config_byte(pdev, bits->reg, &tmp8);
		tmp = tmp8;
		break;
	}
	case 2: {
		u16 tmp16 = 0;
		pci_read_config_word(pdev, bits->reg, &tmp16);
		tmp = tmp16;
		break;
	}
	case 4: {
		u32 tmp32 = 0;
		pci_read_config_dword(pdev, bits->reg, &tmp32);
		tmp = tmp32;
		break;
	}

	default:
		return -EINVAL;
	}

	tmp &= bits->mask;

	return (tmp == bits->val) ? 1 : 0;
}


/**
 *	ata_init -
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int __init ata_init(void)
{
	ata_wq = create_workqueue("ata");
	if (!ata_wq)
		return -ENOMEM;

	printk(KERN_DEBUG "libata version " DRV_VERSION " loaded.\n");
	return 0;
}

static void __exit ata_exit(void)
{
	destroy_workqueue(ata_wq);
}

module_init(ata_init);
module_exit(ata_exit);

/*
 * libata is essentially a library of internal helper functions for
 * low-level ATA host controller drivers.  As such, the API/ABI is
 * likely to change as new drivers are added and updated.
 * Do not depend on ABI/API stability.
 */

EXPORT_SYMBOL_GPL(pci_test_config_bits);
EXPORT_SYMBOL_GPL(ata_std_bios_param);
EXPORT_SYMBOL_GPL(ata_std_ports);
EXPORT_SYMBOL_GPL(ata_device_add);
EXPORT_SYMBOL_GPL(ata_qc_complete);
EXPORT_SYMBOL_GPL(ata_eng_timeout);
EXPORT_SYMBOL_GPL(ata_tf_load_pio);
EXPORT_SYMBOL_GPL(ata_tf_load_mmio);
EXPORT_SYMBOL_GPL(ata_tf_read_pio);
EXPORT_SYMBOL_GPL(ata_tf_read_mmio);
EXPORT_SYMBOL_GPL(ata_tf_to_fis);
EXPORT_SYMBOL_GPL(ata_tf_from_fis);
EXPORT_SYMBOL_GPL(ata_check_status_pio);
EXPORT_SYMBOL_GPL(ata_check_status_mmio);
EXPORT_SYMBOL_GPL(ata_exec_command_pio);
EXPORT_SYMBOL_GPL(ata_exec_command_mmio);
EXPORT_SYMBOL_GPL(ata_port_start);
EXPORT_SYMBOL_GPL(ata_port_stop);
EXPORT_SYMBOL_GPL(ata_interrupt);
EXPORT_SYMBOL_GPL(ata_fill_sg);
EXPORT_SYMBOL_GPL(ata_bmdma_setup_pio);
EXPORT_SYMBOL_GPL(ata_bmdma_start_pio);
EXPORT_SYMBOL_GPL(ata_bmdma_setup_mmio);
EXPORT_SYMBOL_GPL(ata_bmdma_start_mmio);
EXPORT_SYMBOL_GPL(ata_port_probe);
EXPORT_SYMBOL_GPL(sata_phy_reset);
EXPORT_SYMBOL_GPL(ata_bus_reset);
EXPORT_SYMBOL_GPL(ata_port_disable);
EXPORT_SYMBOL_GPL(ata_pci_init_one);
EXPORT_SYMBOL_GPL(ata_pci_remove_one);
EXPORT_SYMBOL_GPL(ata_scsi_queuecmd);
EXPORT_SYMBOL_GPL(ata_scsi_error);
EXPORT_SYMBOL_GPL(ata_scsi_slave_config);
EXPORT_SYMBOL_GPL(ata_scsi_release);
EXPORT_SYMBOL_GPL(ata_host_intr);

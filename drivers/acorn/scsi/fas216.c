/*
 *  linux/arch/arm/drivers/scsi/fas216.c
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on information in qlogicfas.c by Tom Zerucha, Michael Griffith, and
 * other sources, including:
 *   the AMD Am53CF94 data sheet
 *   the AMD Am53C94 data sheet 
 *
 * This is a generic driver.  To use it, have a look at cumana_2.c.  You
 * should define your own structure that overlays FAS216_Info, eg:
 * struct my_host_data {
 *    FAS216_Info info;
 *    ... my host specific data ...
 * };
 *
 * Changelog:
 *  30-08-1997	RMK	Created
 *  14-09-1997	RMK	Started disconnect support
 *  08-02-1998	RMK	Corrected real DMA support
 *  15-02-1998	RMK	Started sync xfer support
 *  06-04-1998	RMK	Tightened conditions for printing incomplete
 *			transfers
 *  02-05-1998	RMK	Added extra checks in fas216_reset
 *  24-05-1998	RMK	Fixed synchronous transfers with period >= 200ns
 *  27-06-1998	RMK	Changed asm/delay.h to linux/delay.h
 *  26-08-1998	RMK	Improved message support wrt MESSAGE_REJECT
 *  02-04-2000	RMK	Converted to use the new error handling, and
 *			automatically request sense data upon check
 *			condition status from targets.
 *
 * Todo:
 *  - allow individual devices to enable sync xfers.
 */
#include <linux/module.h>
#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/stat.h>
#include <linux/delay.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ecard.h>

#define FAS216_C

#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"
#include "fas216.h"

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Generic FAS216/NCR53C9x driver");

#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	5

/* NOTE: SCSI2 Synchronous transfers *require* DMA according to
 *  the data sheet.  This restriction is crazy, especially when
 *  you only want to send 16 bytes!  What were the guys who
 *  designed this chip on at that time?  Did they read the SCSI2
 *  spec at all?  The following sections are taken from the SCSI2
 *  standard (s2r10) concerning this:
 *
 * > IMPLEMENTORS NOTES:
 * >   (1)  Re-negotiation at every selection is not recommended, since a
 * >   significant performance impact is likely.
 *
 * >  The implied synchronous agreement shall remain in effect until a BUS DEVICE
 * >  RESET message is received, until a hard reset condition occurs, or until one
 * >  of the two SCSI devices elects to modify the agreement.  The default data
 * >  transfer mode is asynchronous data transfer mode.  The default data transfer
 * >  mode is entered at power on, after a BUS DEVICE RESET message, or after a hard
 * >  reset condition.
 *
 *  In total, this means that once you have elected to use synchronous
 *  transfers, you must always use DMA.
 *
 *  I was thinking that this was a good chip until I found this restriction ;(
 */
#define SCSI2_SYNC
#undef  SCSI2_WIDE
#undef  SCSI2_TAG

#undef DEBUG_CONNECT
#undef DEBUG_BUSSERVICE
#undef DEBUG_FUNCTIONDONE
#undef DEBUG_MESSAGES

#undef CHECK_STRUCTURE

static struct { int stat, ssr, isr, ph; } list[8];
static int ptr;

static void fas216_dumpstate(FAS216_Info *info)
{
	unsigned char is, stat, inst;

	is   = inb(REG_IS(info));
	stat = inb(REG_STAT(info));
	inst = inb(REG_INST(info));
	
	printk("FAS216: CTCL=%02X CTCM=%02X CMD=%02X STAT=%02X"
	       " INST=%02X IS=%02X CFIS=%02X",
		inb(REG_CTCL(info)), inb(REG_CTCM(info)),
		inb(REG_CMD(info)),  stat, inst, is,
		inb(REG_CFIS(info)));
	printk(" CNTL1=%02X CNTL2=%02X CNTL3=%02X CTCH=%02X\n",
		inb(REG_CNTL1(info)), inb(REG_CNTL2(info)),
		inb(REG_CNTL3(info)), inb(REG_CTCH(info)));
}

static void fas216_dumpinfo(FAS216_Info *info)
{
	static int used = 0;
	int i;

	if (used++)
		return;

	printk("FAS216_Info=\n");
	printk("  { magic_start=%lX host=%p SCpnt=%p origSCpnt=%p\n",
		info->magic_start, info->host, info->SCpnt,
		info->origSCpnt);
	printk("    scsi={ io_port=%X io_shift=%X irq=%X cfg={ %X %X %X %X }\n",
		info->scsi.io_port, info->scsi.io_shift, info->scsi.irq,
		info->scsi.cfg[0], info->scsi.cfg[1], info->scsi.cfg[2],
		info->scsi.cfg[3]);
	printk("           type=%p phase=%X reconnected={ target=%d lun=%d tag=%d }\n",
		info->scsi.type, info->scsi.phase,
		info->scsi.reconnected.target,
		info->scsi.reconnected.lun, info->scsi.reconnected.tag);
	printk("           SCp={ ptr=%p this_residual=%X buffer=%p buffers_residual=%X }\n",
		info->scsi.SCp.ptr, info->scsi.SCp.this_residual,
		info->scsi.SCp.buffer, info->scsi.SCp.buffers_residual);
	printk("      msgs async_stp=%X disconnectable=%d aborting=%d }\n",
		info->scsi.async_stp,
		info->scsi.disconnectable, info->scsi.aborting);
	printk("    stats={ queues=%X removes=%X fins=%X reads=%X writes=%X miscs=%X\n"
	       "            disconnects=%X aborts=%X bus_resets=%X host_resets=%X}\n",
		info->stats.queues, info->stats.removes, info->stats.fins,
		info->stats.reads, info->stats.writes, info->stats.miscs,
		info->stats.disconnects, info->stats.aborts, info->stats.bus_resets,
		info->stats.host_resets);
	printk("    ifcfg={ clockrate=%X select_timeout=%X asyncperiod=%X sync_max_depth=%X }\n",
		info->ifcfg.clockrate, info->ifcfg.select_timeout,
		info->ifcfg.asyncperiod, info->ifcfg.sync_max_depth);
	for (i = 0; i < 8; i++) {
		printk("    busyluns[%d]=%X dev[%d]={ disconnect_ok=%d stp=%X sof=%X sync_state=%X }\n",
			i, info->busyluns[i], i,
			info->device[i].disconnect_ok, info->device[i].stp,
			info->device[i].sof, info->device[i].sync_state);
	}
	printk("    dma={ transfer_type=%X setup=%p pseudo=%p stop=%p }\n",
		info->dma.transfer_type, info->dma.setup,
		info->dma.pseudo, info->dma.stop);
	printk("    internal_done=%X magic_end=%lX }\n",
		info->internal_done, info->magic_end);
}

#ifdef CHECK_STRUCTURE
static void __fas216_checkmagic(FAS216_Info *info, const char *func)
{
	int corruption = 0;
	if (info->magic_start != MAGIC) {
		printk(KERN_CRIT "FAS216 Error: magic at start corrupted\n");
		corruption++;
	}
	if (info->magic_end != MAGIC) {
		printk(KERN_CRIT "FAS216 Error: magic at end corrupted\n");
		corruption++;
	}
	if (corruption) {
		fas216_dumpinfo(info);
		panic("scsi memory space corrupted in %s", func);
	}
}
#define fas216_checkmagic(info) __fas216_checkmagic((info), __FUNCTION__)
#else
#define fas216_checkmagic(info)
#endif

static const char *fas216_bus_phase(int stat)
{
	static const char *phases[] = {
		"DATA OUT", "DATA IN",
		"COMMAND", "STATUS",
		"MISC OUT", "MISC IN",
		"MESG OUT", "MESG IN"
	};

	return phases[stat & STAT_BUSMASK];
}

static const char *fas216_drv_phase(FAS216_Info *info)
{
	switch (info->scsi.phase) {
	case PHASE_IDLE:		return "idle";
	case PHASE_SELECTION:		return "selection";
	case PHASE_COMMAND:		return "command";
	case PHASE_RECONNECTED:		return "reconnected";
	case PHASE_DATAOUT:		return "data out";
	case PHASE_DATAIN:		return "data in";
	case PHASE_MSGIN:		return "message in";
	case PHASE_MSGIN_DISCONNECT:	return "disconnect";
	case PHASE_MSGOUT_EXPECT:	return "expect message out";
	case PHASE_MSGOUT:		return "message out";
	case PHASE_STATUS:		return "status";
	case PHASE_DONE:		return "done";
	default:			return "???";
	}
}

static char fas216_target(FAS216_Info *info)
{
	if (info->SCpnt)
		return '0' + info->SCpnt->target;
	else
		return 'H';
}

static void add_debug_list(int stat, int ssr, int isr, int ph)
{
	list[ptr].stat = stat;
	list[ptr].ssr = ssr;
	list[ptr].isr = isr;
	list[ptr].ph = ph;

	ptr = (ptr + 1) & 7;
}

static void print_debug_list(void)
{
	int i;

	i = ptr;

	printk(KERN_ERR "SCSI IRQ trail: ");
	do {
		printk("%02X:%02X:%02X:%1X ",
			list[i].stat, list[i].ssr,
			list[i].isr, list[i].ph);
		i = (i + 1) & 7;
	} while (i != ptr);
	printk("\n");
}

static void fas216_done(FAS216_Info *info, unsigned int result);

/* Function: int fas216_clockrate(unsigned int clock)
 * Purpose : calculate correct value to be written into clock conversion
 *	     factor register.
 * Params  : clock - clock speed in MHz
 * Returns : CLKF_ value
 */
static int fas216_clockrate(int clock)
{
	if (clock <= 10 || clock > 40) {
		printk(KERN_CRIT
		       "fas216: invalid clock rate: check your driver!\n");
		clock = -1;
	} else
		clock = ((clock - 1) / 5 + 1) & 7;

	return clock;
}

/* Function: unsigned short fas216_get_last_msg(FAS216_Info *info, int pos)
 * Purpose : retrieve a last message from the list, using position in fifo
 * Params  : info - interface to search
 *         : pos  - current fifo position
 */
static inline unsigned short
fas216_get_last_msg(FAS216_Info *info, int pos)
{
	unsigned short packed_msg = NOP;
	struct message *msg;
	int msgnr = 0;

	while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
		if (pos >= msg->fifo)
			break;
	}

	if (msg) {
		if (msg->msg[0] == EXTENDED_MESSAGE)
			packed_msg = EXTENDED_MESSAGE | msg->msg[2] << 8;
		else
			packed_msg = msg->msg[0];
	}

#ifdef DEBUG_MESSAGES
	printk("Message: %04X found at position %02X\n",
		packed_msg, pos);
#endif
	return packed_msg;
}

/* Function: int fas216_syncperiod(FAS216_Info *info, int ns)
 * Purpose : Calculate value to be loaded into the STP register
 *           for a given period in ns
 * Params  : info - state structure for interface connected to device
 *         : ns   - period in ns (between subsequent bytes)
 * Returns : Value suitable for REG_STP
 */
static int
fas216_syncperiod(FAS216_Info *info, int ns)
{
	int value = (info->ifcfg.clockrate * ns) / 1000;

	fas216_checkmagic(info);

	if (value < 4)
		value = 4;
	else if (value > 35)
		value = 35;

	return value & 31;
}

/* Function: void fas216_set_sync(FAS216_Info *info, int target)
 * Purpose : Correctly setup FAS216 chip for specified transfer period.
 * Params  : info   - state structure for interface
 *         : target - target
 * Notes   : we need to switch the chip out of FASTSCSI mode if we have
 *           a transfer period >= 200ns - otherwise the chip will violate
 *           the SCSI timings.
 */
static void
fas216_set_sync(FAS216_Info *info, int target)
{
	outb(info->device[target].sof, REG_SOF(info));
	outb(info->device[target].stp, REG_STP(info));
	if (info->device[target].period >= (200 / 4))
		outb(info->scsi.cfg[2] & ~CNTL3_FASTSCSI, REG_CNTL3(info));
	else
		outb(info->scsi.cfg[2], REG_CNTL3(info));
}

/* Synchronous transfer support
 *
 * Note: The SCSI II r10 spec says (5.6.12):
 *
 *  (2)  Due to historical problems with early host adapters that could
 *  not accept an SDTR message, some targets may not initiate synchronous
 *  negotiation after a power cycle as required by this standard.  Host
 *  adapters that support synchronous mode may avoid the ensuing failure
 *  modes when the target is independently power cycled by initiating a
 *  synchronous negotiation on each REQUEST SENSE and INQUIRY command.
 *  This approach increases the SCSI bus overhead and is not recommended
 *  for new implementations.  The correct method is to respond to an
 *  SDTR message with a MESSAGE REJECT message if the either the
 *  initiator or target devices does not support synchronous transfers
 *  or does not want to negotiate for synchronous transfers at the time.
 *  Using the correct method assures compatibility with wide data
 *  transfers and future enhancements.
 *
 * We will always initiate a synchronous transfer negociation request on
 * every INQUIRY or REQUEST SENSE message, unless the target itself has
 * at some point performed a synchronous transfer negociation request, or
 * we have synchronous transfers disabled for this device.
 */

/* Function: void fas216_handlesync(FAS216_Info *info, char *msg)
 * Purpose : Handle a synchronous transfer message from the target
 * Params  : info - state structure for interface
 *         : msg  - message from target
 */
static void
fas216_handlesync(FAS216_Info *info, char *msg)
{
	struct fas216_device *dev = &info->device[info->SCpnt->target];
	enum { sync, async, none, reject } res = none;

#ifdef SCSI2_SYNC
	switch (msg[0]) {
	case MESSAGE_REJECT:
		/* Synchronous transfer request failed.
		 * Note: SCSI II r10:
		 *
		 *  SCSI devices that are capable of synchronous
		 *  data transfers shall not respond to an SDTR
		 *  message with a MESSAGE REJECT message.
		 *
		 * Hence, if we get this condition, we disable
		 * negociation for this device.
		 */
		if (dev->sync_state == neg_inprogress) {
			dev->sync_state = neg_invalid;
			res = async;
		}
		break;

	case EXTENDED_MESSAGE:
		switch (dev->sync_state) {
		/* We don't accept synchronous transfer requests.
		 * Respond with a MESSAGE_REJECT to prevent a
		 * synchronous transfer agreement from being reached.
		 */
		case neg_invalid:
			res = reject;
			break;

		/* We were not negociating a synchronous transfer,
		 * but the device sent us a negociation request.
		 * Honour the request by sending back a SDTR
		 * message containing our capability, limited by
		 * the targets capability.
		 */
		default:
			outb(CMD_SETATN, REG_CMD(info));
			if (msg[4] > info->ifcfg.sync_max_depth)
				msg[4] = info->ifcfg.sync_max_depth;
			if (msg[3] < 1000 / info->ifcfg.clockrate)
				msg[3] = 1000 / info->ifcfg.clockrate;

			msgqueue_flush(&info->scsi.msgs);
			msgqueue_addmsg(&info->scsi.msgs, 5,
					EXTENDED_MESSAGE, 3, EXTENDED_SDTR,
					msg[3], msg[4]);
			info->scsi.phase = PHASE_MSGOUT_EXPECT;

			/* This is wrong.  The agreement is not in effect
			 * until this message is accepted by the device
			 */
			dev->sync_state = neg_targcomplete;
			res = sync;
			break;

		/* We initiated the synchronous transfer negociation,
		 * and have successfully received a response from the
		 * target.  The synchronous transfer agreement has been
		 * reached.  Note: if the values returned are out of our
		 * bounds, we must reject the message.
		 */
		case neg_inprogress:
			res = reject;
			if (msg[4] <= info->ifcfg.sync_max_depth &&
			    msg[3] >= 1000 / info->ifcfg.clockrate) {
				dev->sync_state = neg_complete;
				res = sync;
			}
			break;
		}
	}
#else
	res = reject;
#endif

	switch (res) {
	case sync:
		dev->period = msg[3];
		dev->sof    = msg[4];
		dev->stp    = fas216_syncperiod(info, msg[3] * 4);
		fas216_set_sync(info, info->SCpnt->target);
		break;

	case reject:
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_flush(&info->scsi.msgs);
		msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;

	case async:
		dev->period = info->ifcfg.asyncperiod / 4;
		dev->sof    = 0;
		dev->stp    = info->scsi.async_stp;
		fas216_set_sync(info, info->SCpnt->target);
		break;

	case none:
		break;
	}
}

/* Function: void fas216_handlewide(FAS216_Info *info, char *msg)
 * Purpose : Handle a wide transfer message from the target
 * Params  : info - state structure for interface
 *         : msg  - message from target
 */
static void
fas216_handlewide(FAS216_Info *info, char *msg)
{
	struct fas216_device *dev = &info->device[info->SCpnt->target];
	enum { wide, bit8, none, reject } res = none;

#ifdef SCSI2_WIDE
	switch (msg[0]) {
	case MESSAGE_REJECT:
		/* Wide transfer request failed.
		 * Note: SCSI II r10:
		 *
		 *  SCSI devices that are capable of wide
		 *  data transfers shall not respond to a
		 *  WDTR message with a MESSAGE REJECT message.
		 *
		 * Hence, if we get this condition, we never
		 * reattempt negociation for this device.
		 */
		if (dev->wide_state == neg_inprogress) {
			dev->wide_state = neg_invalid;
			res = bit8;
		}
		break;

	case EXTENDED_MESSAGE:
		switch (dev->wide_state) {
		/* We don't accept wide data transfer requests.
		 * Respond with a MESSAGE REJECT to prevent a
		 * wide data transfer agreement from being reached.
		 */
		case neg_invalid:
			res = reject;
			break;

		/* We were not negociating a wide data transfer,
		 * but the device sent is a negociation request.
		 * Honour the request by sending back a WDTR
		 * message containing our capability, limited by
		 * the targets capability.
		 */
		default:
			outb(CMD_SETATN, REG_CMD(info));
			if (msg[3] > info->ifcfg.wide_max_size)
				msg[3] = info->ifcfg.wide_max_size;

			msgqueue_flush(&info->scsi.msgs);
			msgqueue_addmsg(&info->scsi.msgs, 4,
					EXTENDED_MESSAGE, 2, EXTENDED_WDTR,
					msg[3]);
			info->scsi.phase = PHASE_MSGOUT_EXPECT;
			res = wide;
			break;

		/* We initiated the wide data transfer negociation,
		 * and have successfully received a response from the
		 * target.  The synchronous transfer agreement has been
		 * reached.  Note: if the values returned are out of our
		 * bounds, we must reject the message.
		 */
		case neg_inprogress:
			res = reject;
			if (msg[3] <= info->ifcfg.wide_max_size) {
				dev->wide_state = neg_complete;
				res = wide;
			}
			break;
		}
	}
#else
	res = reject;
#endif

	switch (res) {
	case wide:
		dev->wide_xfer = msg[3];
		break;

	case reject:
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_flush(&info->scsi.msgs);
		msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;

	case bit8:
		dev->wide_xfer = 0;
		break;

	case none:
		break;
	}
}

/* Function: void fas216_updateptrs(FAS216_Info *info, int bytes_transferred)
 * Purpose : update data pointers after transfer suspended/paused
 * Params  : info              - interface's local pointer to update
 *           bytes_transferred - number of bytes transferred
 */
static void
fas216_updateptrs(FAS216_Info *info, int bytes_transferred)
{
	unsigned char *ptr;
	unsigned int residual;

	fas216_checkmagic(info);

	ptr = info->scsi.SCp.ptr;
	residual = info->scsi.SCp.this_residual;

	info->SCpnt->request_bufflen -= bytes_transferred;

	while (residual <= bytes_transferred && bytes_transferred) {
		/* We have used up this buffer */
		bytes_transferred -= residual;
		if (info->scsi.SCp.buffers_residual) {
			info->scsi.SCp.buffer++;
			info->scsi.SCp.buffers_residual--;
			ptr = (unsigned char *)info->scsi.SCp.buffer->address;
			residual = info->scsi.SCp.buffer->length;
		} else {
			ptr = NULL;
			residual = 0;
		}
	}

	residual -= bytes_transferred;
	ptr += bytes_transferred;

	if (residual == 0)
		ptr = NULL;

	info->scsi.SCp.ptr = ptr;
	info->scsi.SCp.this_residual = residual;
}

/* Function: void fas216_pio(FAS216_Info *info, fasdmadir_t direction)
 * Purpose : transfer data off of/on to card using programmed IO
 * Params  : info      - interface to transfer data to/from
 *           direction - direction to transfer data (DMA_OUT/DMA_IN)
 * Notes   : this is incredibly slow
 */
static void
fas216_pio(FAS216_Info *info, fasdmadir_t direction)
{
	unsigned int residual;
	char *ptr;

	fas216_checkmagic(info);

	residual = info->scsi.SCp.this_residual;
	ptr = info->scsi.SCp.ptr;

	if (direction == DMA_OUT)
		outb(*ptr++, REG_FF(info));
	else
		*ptr++ = inb(REG_FF(info));

	residual -= 1;

	if (residual == 0) {
		if (info->scsi.SCp.buffers_residual) {
			info->scsi.SCp.buffer++;
			info->scsi.SCp.buffers_residual--;
			ptr = (unsigned char *)info->scsi.SCp.buffer->address;
			residual = info->scsi.SCp.buffer->length;
		} else {
			ptr = NULL;
			residual = 0;
		}
	}

	info->scsi.SCp.ptr = ptr;
	info->scsi.SCp.this_residual = residual;
}

/* Function: void fas216_starttransfer(FAS216_Info *info,
 *				       fasdmadir_t direction)
 * Purpose : Start a DMA/PIO transfer off of/on to card
 * Params  : info      - interface from which device disconnected from
 *           direction - transfer direction (DMA_OUT/DMA_IN)
 */
static void
fas216_starttransfer(FAS216_Info *info, fasdmadir_t direction, int flush_fifo)
{
	fasdmatype_t dmatype;

	fas216_checkmagic(info);

	info->scsi.phase = (direction == DMA_OUT) ?
				PHASE_DATAOUT : PHASE_DATAIN;

	if (info->dma.transfer_type != fasdma_none &&
	    info->dma.transfer_type != fasdma_pio) {
		unsigned long total, residual;

		if (info->dma.transfer_type == fasdma_real_all)
			total = info->SCpnt->request_bufflen;
		else
			total = info->scsi.SCp.this_residual;

		residual = (inb(REG_CFIS(info)) & CFIS_CF) +
			    inb(REG_CTCL(info)) +
			    (inb(REG_CTCM(info)) << 8) +
			    (inb(REG_CTCH(info)) << 16);
		fas216_updateptrs(info, total - residual);
	}
	info->dma.transfer_type = fasdma_none;

	if (!info->scsi.SCp.ptr) {
		printk("scsi%d.%c: null buffer passed to "
			"fas216_starttransfer\n", info->host->host_no,
			fas216_target(info));
		return;
	}

	/* flush FIFO */
	if (flush_fifo)
		outb(CMD_FLUSHFIFO, REG_CMD(info));

	/*
	 * Default to PIO mode or DMA mode if we have a synchronous
	 * transfer agreement.
	 */
	if (info->device[info->SCpnt->target].sof && info->dma.setup)
		dmatype = fasdma_real_all;
	else
		dmatype = fasdma_pio;

	if (info->dma.setup)
		dmatype = info->dma.setup(info->host, &info->scsi.SCp,
					  direction, dmatype);
	info->dma.transfer_type = dmatype;

	switch (dmatype) {
	case fasdma_pio:
		outb(0, REG_SOF(info));
		outb(info->scsi.async_stp, REG_STP(info));
		outb(info->scsi.SCp.this_residual, REG_STCL(info));
		outb(info->scsi.SCp.this_residual >> 8, REG_STCM(info));
		outb(info->scsi.SCp.this_residual >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		fas216_pio(info, direction);
		break;

	case fasdma_pseudo:
		outb(info->scsi.SCp.this_residual, REG_STCL(info));
		outb(info->scsi.SCp.this_residual >> 8, REG_STCM(info));
		outb(info->scsi.SCp.this_residual >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO | CMD_WITHDMA, REG_CMD(info));
		info->dma.pseudo(info->host, &info->scsi.SCp,
				 direction, info->SCpnt->transfersize);
		break;

	case fasdma_real_block:
		outb(info->scsi.SCp.this_residual, REG_STCL(info));
		outb(info->scsi.SCp.this_residual >> 8, REG_STCM(info));
		outb(info->scsi.SCp.this_residual >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO | CMD_WITHDMA, REG_CMD(info));
		break;

	case fasdma_real_all:
		outb(info->SCpnt->request_bufflen, REG_STCL(info));
		outb(info->SCpnt->request_bufflen >> 8, REG_STCM(info));
		outb(info->SCpnt->request_bufflen >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO | CMD_WITHDMA, REG_CMD(info));
		break;

	default:
		printk(KERN_ERR "scsi%d.%d: invalid FAS216 DMA type\n",
		       info->host->host_no, fas216_target(info));
		break;
	}
}

/* Function: void fas216_stoptransfer(FAS216_Info *info)
 * Purpose : Stop a DMA transfer onto / off of the card
 * Params  : info      - interface from which device disconnected from
 */
static void
fas216_stoptransfer(FAS216_Info *info)
{
	fas216_checkmagic(info);

	if (info->dma.transfer_type != fasdma_none &&
	    info->dma.transfer_type != fasdma_pio) {
		unsigned long total, residual;

		if ((info->dma.transfer_type == fasdma_real_all ||
		     info->dma.transfer_type == fasdma_real_block) &&
		    info->dma.stop)
			info->dma.stop(info->host, &info->scsi.SCp);

		if (info->dma.transfer_type == fasdma_real_all)
			total = info->SCpnt->request_bufflen;
		else
			total = info->scsi.SCp.this_residual;

		residual = (inb(REG_CFIS(info)) & CFIS_CF) +
			    inb(REG_CTCL(info)) +
			    (inb(REG_CTCM(info)) << 8) +
			    (inb(REG_CTCH(info)) << 16);
		fas216_updateptrs(info, total - residual);
		info->dma.transfer_type = fasdma_none;
	}
	if (info->scsi.phase == PHASE_DATAOUT)
		outb(CMD_FLUSHFIFO, REG_CMD(info));
}

/* Function: void fas216_disconnected_intr(FAS216_Info *info)
 * Purpose : handle device disconnection
 * Params  : info - interface from which device disconnected from
 */
static void
fas216_disconnect_intr(FAS216_Info *info)
{
	fas216_checkmagic(info);

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: disconnect phase=%02X\n", info->host->host_no,
		fas216_target(info), info->scsi.phase);
#endif
	msgqueue_flush(&info->scsi.msgs);

	switch (info->scsi.phase) {
	case PHASE_SELECTION:			/* while selecting - no target		*/
	case PHASE_SELSTEPS:
		fas216_done(info, DID_NO_CONNECT);
		break;

	case PHASE_MSGIN_DISCONNECT:		/* message in - disconnecting		*/
		outb(CMD_ENABLESEL, REG_CMD(info));
		info->scsi.disconnectable = 1;
		info->scsi.reconnected.tag = 0;
		info->scsi.phase = PHASE_IDLE;
		info->stats.disconnects += 1;
		break;

	case PHASE_DONE:			/* at end of command - complete		*/
		fas216_done(info, DID_OK);
		break;

	case PHASE_MSGOUT:			/* message out - possible ABORT message	*/
		if (fas216_get_last_msg(info, info->scsi.msgin_fifo) == ABORT) {
			info->scsi.aborting = 0;
			fas216_done(info, DID_ABORT);
			break;
		}

	default:				/* huh?					*/
		printk(KERN_ERR "scsi%d.%c: unexpected disconnect in phase %s\n",
			info->host->host_no, fas216_target(info), fas216_drv_phase(info));
		print_debug_list();
		fas216_stoptransfer(info);
		fas216_done(info, DID_ERROR);
		break;
	}
}

/* Function: void fas216_reselected_intr(FAS216_Info *info)
 * Purpose : Start reconnection of a device
 * Params  : info - interface which was reselected
 */
static void
fas216_reselected_intr(FAS216_Info *info)
{
	unsigned char target, identify_msg, ok;

	fas216_checkmagic(info);

	if ((info->scsi.phase == PHASE_SELECTION ||
	     info->scsi.phase == PHASE_SELSTEPS) && info->SCpnt) {
		Scsi_Cmnd *SCpnt = info->SCpnt;

		info->origSCpnt = SCpnt;
		info->SCpnt = NULL;

		if (info->device[SCpnt->target].wide_state == neg_inprogress)
			info->device[SCpnt->target].wide_state = neg_wait;
		if (info->device[SCpnt->target].sync_state == neg_inprogress)
			info->device[SCpnt->target].sync_state = neg_wait;
	}

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: reconnect phase=%02X\n", info->host->host_no,
		fas216_target(info), info->scsi.phase);
#endif

	if ((inb(REG_CFIS(info)) & CFIS_CF) != 2) {
		printk(KERN_ERR "scsi%d.H: incorrect number of bytes after reselect\n",
			info->host->host_no);
		outb(CMD_SETATN, REG_CMD(info));
		outb(CMD_MSGACCEPTED, REG_CMD(info));
		msgqueue_flush(&info->scsi.msgs);
		msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		return;
	}

	target = inb(REG_FF(info));
	identify_msg = inb(REG_FF(info));

	ok = 1;
	if (!(target & (1 << info->host->this_id))) {
		printk(KERN_ERR "scsi%d.H: invalid host id on reselect\n", info->host->host_no);
		ok = 0;
	}

	if (!(identify_msg & 0x80)) {
		printk(KERN_ERR "scsi%d.H: no IDENTIFY message on reselect, got msg %02X\n",
			info->host->host_no, identify_msg);
		ok = 0;
	}

	if (!ok) {
		/*
		 * Something went wrong - send an initiator error to
		 * the target.
		 */
		outb(CMD_SETATN, REG_CMD(info));
		outb(CMD_MSGACCEPTED, REG_CMD(info));
		msgqueue_flush(&info->scsi.msgs);
		msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		return;
	}

	target &= ~(1 << info->host->this_id);
	switch (target) {
	case   1:  target = 0; break;
	case   2:  target = 1; break;
	case   4:  target = 2; break;
	case   8:  target = 3; break;
	case  16:  target = 4; break;
	case  32:  target = 5; break;
	case  64:  target = 6; break;
	case 128:  target = 7; break;
	default:   target = info->host->this_id; break;
	}

	identify_msg &= 7;
	info->scsi.reconnected.target = target;
	info->scsi.reconnected.lun    = identify_msg;
	info->scsi.reconnected.tag    = 0;

	ok = 0;
	if (info->scsi.disconnectable && info->SCpnt &&
	    info->SCpnt->target == target && info->SCpnt->lun == identify_msg)
		ok = 1;

	if (!ok && queue_probetgtlun(&info->queues.disconnected, target, identify_msg))
		ok = 1;

	msgqueue_flush(&info->scsi.msgs);
	if (ok) {
		info->scsi.phase = PHASE_RECONNECTED;
		outb(target, REG_SDID(info));
	} else {
		/*
		 * Our command structure not found - abort the
		 * command on the target.  Since we have no
		 * record of this command, we can't send
		 * an INITIATOR DETECTED ERROR message.
		 */
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_addmsg(&info->scsi.msgs, 1, ABORT);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
	}

	outb(CMD_MSGACCEPTED, REG_CMD(info));
}

/* Function: void fas216_finish_reconnect(FAS216_Info *info)
 * Purpose : finish reconnection sequence for device
 * Params  : info - interface which caused function done interrupt
 */
static void
fas216_finish_reconnect(FAS216_Info *info)
{
	fas216_checkmagic(info);

#ifdef DEBUG_CONNECT
	printk("Connected: %1X %1X %02X, reconnected: %1X %1X %02X\n",
		info->SCpnt->target, info->SCpnt->lun, info->SCpnt->tag,
		info->scsi.reconnected.target, info->scsi.reconnected.lun,
		info->scsi.reconnected.tag);
#endif

	if (info->scsi.disconnectable && info->SCpnt) {
		info->scsi.disconnectable = 0;
		if (info->SCpnt->target == info->scsi.reconnected.target &&
		    info->SCpnt->lun    == info->scsi.reconnected.lun &&
		    info->SCpnt->tag    == info->scsi.reconnected.tag) {
#ifdef DEBUG_CONNECT
			printk("scsi%d.%c: reconnected",
				info->host->host_no, fas216_target(info));
#endif
		} else {
			queue_add_cmd_tail(&info->queues.disconnected, info->SCpnt);
#ifdef DEBUG_CONNECT
			printk("scsi%d.%c: had to move command to disconnected queue\n",
				info->host->host_no, fas216_target(info));
#endif
			info->SCpnt = NULL;
		}
	}
	if (!info->SCpnt) {
		info->SCpnt = queue_remove_tgtluntag(&info->queues.disconnected,
					info->scsi.reconnected.target,
					info->scsi.reconnected.lun,
					info->scsi.reconnected.tag);
#ifdef DEBUG_CONNECT
		printk("scsi%d.%c: had to get command",
			info->host->host_no, fas216_target(info));
#endif
	}
	if (!info->SCpnt) {
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_flush(&info->scsi.msgs);
#if 0
		if (info->scsi.reconnected.tag)
			msgqueue_addmsg(&info->scsi.msgs, 2, ABORT_TAG, info->scsi.reconnected.tag);
		else
#endif
			msgqueue_addmsg(&info->scsi.msgs, 1, ABORT);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		info->scsi.aborting = 1;
	} else {
		/*
		 * Restore data pointer from SAVED data pointer
		 */
		info->scsi.SCp = info->SCpnt->SCp;
#ifdef DEBUG_CONNECT
		printk(", data pointers: [%p, %X]",
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
	}
#ifdef DEBUG_CONNECT
	printk("\n");
#endif
}

static int fas216_wait_cmd(FAS216_Info *info, int cmd)
{
	int tout;
	int stat;

	outb(cmd, REG_CMD(info));

	for (tout = 1000; tout; tout -= 1) {
		stat = inb(REG_STAT(info));
		if (stat & STAT_INT)
			break;
		udelay(1);
	}

	return stat;
}

static int fas216_get_msg_byte(FAS216_Info *info)
{
	int stat;

	stat = fas216_wait_cmd(info, CMD_MSGACCEPTED);

	if ((stat & STAT_INT) == 0)
		goto timedout;

	if ((stat & STAT_BUSMASK) != STAT_MESGIN)
		goto unexpected_phase_change;

	inb(REG_INST(info));

	stat = fas216_wait_cmd(info, CMD_TRANSFERINFO);

	if ((stat & STAT_INT) == 0)
		goto timedout;

	if ((stat & STAT_BUSMASK) != STAT_MESGIN)
		goto unexpected_phase_change;

	inb(REG_INST(info));

	return inb(REG_FF(info));

timedout:
	printk("scsi%d.%c: timed out waiting for message byte\n",
		info->host->host_no, fas216_target(info));
	return -1;

unexpected_phase_change:
	printk("scsi%d.%c: unexpected phase change: status = %02X\n",
		info->host->host_no, fas216_target(info), stat);

	return -2;
}

/* Function: void fas216_message(FAS216_Info *info)
 * Purpose : handle a function done interrupt from FAS216 chip
 * Params  : info - interface which caused function done interrupt
 */
static void fas216_message(FAS216_Info *info)
{
	unsigned char *message = info->scsi.message;
	unsigned int msglen = 1, i;
	int msgbyte = 0;

	fas216_checkmagic(info);

	message[0] = inb(REG_FF(info));

	if (message[0] == EXTENDED_MESSAGE) {
		msgbyte = fas216_get_msg_byte(info);

		if (msgbyte >= 0) {
			message[1] = msgbyte;

			for (msglen = 2; msglen < message[1] + 2; msglen++) {
				msgbyte = fas216_get_msg_byte(info);

				if (msgbyte >= 0)
					message[msglen] = msgbyte;
				else
					break;
			}
		}
	}

	info->scsi.msglen = msglen;

#ifdef DEBUG_MESSAGES
	{
		int i;

		printk("scsi%d.%c: message in: ",
			info->host->host_no, fas216_target(info));
		for (i = 0; i < msglen; i++)
			printk("%02X ", message[i]);
		printk("\n");
	}
#endif

	if (info->scsi.phase == PHASE_RECONNECTED) {
		if (message[0] == SIMPLE_QUEUE_TAG)
			info->scsi.reconnected.tag = message[1];
		fas216_finish_reconnect(info);
		info->scsi.phase = PHASE_MSGIN;
	}

	switch (message[0]) {
	case COMMAND_COMPLETE:
		if (msglen != 1)
			goto unrecognised;

		printk(KERN_ERR "scsi%d.%c: command complete with no "
			"status in MESSAGE_IN?\n",
			info->host->host_no, fas216_target(info));
		break;

	case SAVE_POINTERS:
		if (msglen != 1)
			goto unrecognised;

		/*
		 * Save current data pointer to SAVED data pointer
		 * SCSI II standard says that we must not acknowledge
		 * this until we have really saved pointers.
		 * NOTE: we DO NOT save the command nor status pointers
		 * as required by the SCSI II standard.  These always
		 * point to the start of their respective areas.
		 */
		info->SCpnt->SCp = info->scsi.SCp;
		info->SCpnt->SCp.sent_command = 0;
#if defined (DEBUG_MESSAGES) || defined (DEBUG_CONNECT)
		printk("scsi%d.%c: save data pointers: [%p, %X]\n",
			info->host->host_no, fas216_target(info),
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
		break;

	case RESTORE_POINTERS:
		if (msglen != 1)
			goto unrecognised;

		/*
		 * Restore current data pointer from SAVED data pointer
		 */
		info->scsi.SCp = info->SCpnt->SCp;
#if defined (DEBUG_MESSAGES) || defined (DEBUG_CONNECT)
		printk("scsi%d.%c: restore data pointers: [%p, %X]\n",
			info->host->host_no, fas216_target(info),
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
		break;

	case DISCONNECT:
		if (msglen != 1)
			goto unrecognised;

		info->scsi.phase = PHASE_MSGIN_DISCONNECT;
		break;

	case MESSAGE_REJECT:
		if (msglen != 1)
			goto unrecognised;

		switch (fas216_get_last_msg(info, info->scsi.msgin_fifo)) {
		case EXTENDED_MESSAGE | EXTENDED_SDTR << 8:
			fas216_handlesync(info, message);
			break;

		case EXTENDED_MESSAGE | EXTENDED_WDTR << 8:
			fas216_handlewide(info, message);
			break;

		default:
			printk("scsi%d.%c: reject, last message %04X\n",
				info->host->host_no, fas216_target(info),
				fas216_get_last_msg(info, info->scsi.msgin_fifo));
		}
		break;

	case NOP:
		break;

	case SIMPLE_QUEUE_TAG:
		if (msglen < 2)
			goto unrecognised;

		/* handled above - print a warning since this is untested */
		printk("scsi%d.%c: reconnect queue tag %02X\n",
			info->host->host_no, fas216_target(info),
			message[1]);
		break;

	case EXTENDED_MESSAGE:
		if (msglen < 3)
			goto unrecognised;

		switch (message[2]) {
		case EXTENDED_SDTR:	/* Sync transfer negociation request/reply */
			fas216_handlesync(info, message);
			break;

		case EXTENDED_WDTR:	/* Wide transfer negociation request/reply */
			fas216_handlewide(info, message);
			break;

		default:
			goto unrecognised;
		}
		break;

	default:
		goto unrecognised;
	}
	outb(CMD_MSGACCEPTED, REG_CMD(info));
	return;

unrecognised:
	printk("scsi%d.%c: unrecognised message, rejecting\n",
		info->host->host_no, fas216_target(info));
	printk("scsi%d.%c: message was", info->host->host_no, fas216_target(info));
	for (i = 0; i < msglen; i++)
		printk("%s%02X", i & 31 ? " " : "\n  ", message[i]);
	printk("\n");

	/*
	 * Something strange seems to be happening here -
	 * I can't use SETATN since the chip gives me an
	 * invalid command interrupt when I do.  Weird.
	 */
outb(CMD_NOP, REG_CMD(info));
fas216_dumpstate(info);
	outb(CMD_SETATN, REG_CMD(info));
	msgqueue_flush(&info->scsi.msgs);
	msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
	info->scsi.phase = PHASE_MSGOUT_EXPECT;
fas216_dumpstate(info);
	outb(CMD_MSGACCEPTED, REG_CMD(info));
}

/* Function: void fas216_send_command(FAS216_Info *info)
 * Purpose : send a command to a target after all message bytes have been sent
 * Params  : info - interface which caused bus service
 */
static void fas216_send_command(FAS216_Info *info)
{
	int i;

	fas216_checkmagic(info);

	outb(CMD_NOP|CMD_WITHDMA, REG_CMD(info));
	outb(CMD_FLUSHFIFO, REG_CMD(info));

	/* load command */
	for (i = info->scsi.SCp.sent_command; i < info->SCpnt->cmd_len; i++)
		outb(info->SCpnt->cmnd[i], REG_FF(info));

	outb(CMD_TRANSFERINFO, REG_CMD(info));

	info->scsi.phase = PHASE_COMMAND;
}

/* Function: void fas216_send_messageout(FAS216_Info *info, int start)
 * Purpose : handle bus service to send a message
 * Params  : info - interface which caused bus service
 * Note    : We do not allow the device to change the data direction!
 */
static void fas216_send_messageout(FAS216_Info *info, int start)
{
	unsigned int tot_msglen = msgqueue_msglength(&info->scsi.msgs);

	fas216_checkmagic(info);

	outb(CMD_FLUSHFIFO, REG_CMD(info));

	if (tot_msglen) {
		struct message *msg;
		int msgnr = 0;

		while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
			int i;

			for (i = start; i < msg->length; i++)
				outb(msg->msg[i], REG_FF(info));

			msg->fifo = tot_msglen - (inb(REG_CFIS(info)) & CFIS_CF);
			start = 0;
		}
	} else
		outb(NOP, REG_FF(info));

	outb(CMD_TRANSFERINFO, REG_CMD(info));

	info->scsi.phase = PHASE_MSGOUT;
}

/* Function: void fas216_busservice_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
 * Purpose : handle a bus service interrupt from FAS216 chip
 * Params  : info - interface which caused bus service interrupt
 *           stat - Status register contents
 *           ssr  - SCSI Status register contents
 */
static void fas216_busservice_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
{
	fas216_checkmagic(info);

#ifdef DEBUG_BUSSERVICE
	printk("scsi%d.%c: bus service: stat=%02X ssr=%02X phase=%02X\n",
		info->host->host_no, fas216_target(info), stat, ssr, info->scsi.phase);
#endif

	switch (info->scsi.phase) {
	case PHASE_SELECTION:
		if ((ssr & IS_BITS) != 1)
			goto bad_is;
		break;

	case PHASE_SELSTEPS:
		switch (ssr & IS_BITS) {
		case IS_SELARB:
		case IS_MSGBYTESENT:
			goto bad_is;

		case IS_NOTCOMMAND:
		case IS_EARLYPHASE:
			if ((stat & STAT_BUSMASK) == STAT_MESGIN)
				break;
			goto bad_is;

		case IS_COMPLETE:
			break;
		}

	default:
		break;
	}

	outb(CMD_NOP, REG_CMD(info));

#define STATE(st,ph) ((ph) << 3 | (st))
	/* This table describes the legal SCSI state transitions,
	 * as described by the SCSI II spec.
	 */
	switch (STATE(stat & STAT_BUSMASK, info->scsi.phase)) {
						/* Reselmsgin   -> Data In	*/
	case STATE(STAT_DATAIN, PHASE_RECONNECTED):
		fas216_finish_reconnect(info);
	case STATE(STAT_DATAIN, PHASE_SELSTEPS):/* Sel w/ steps -> Data In      */
	case STATE(STAT_DATAIN, PHASE_DATAIN):  /* Data In      -> Data In      */
	case STATE(STAT_DATAIN, PHASE_MSGOUT):  /* Message Out  -> Data In      */
	case STATE(STAT_DATAIN, PHASE_COMMAND): /* Command      -> Data In      */
	case STATE(STAT_DATAIN, PHASE_MSGIN):   /* Message In   -> Data In      */
		fas216_starttransfer(info, DMA_IN, 0);
		return;

	case STATE(STAT_DATAOUT, PHASE_DATAOUT):/* Data Out     -> Data Out     */
		fas216_starttransfer(info, DMA_OUT, 0);
		return;

						/* Reselmsgin   -> Data Out     */
	case STATE(STAT_DATAOUT, PHASE_RECONNECTED):
		fas216_finish_reconnect(info);
	case STATE(STAT_DATAOUT, PHASE_SELSTEPS):/* Sel w/ steps-> Data Out     */
	case STATE(STAT_DATAOUT, PHASE_MSGOUT): /* Message Out  -> Data Out     */
	case STATE(STAT_DATAOUT, PHASE_COMMAND):/* Command      -> Data Out     */
	case STATE(STAT_DATAOUT, PHASE_MSGIN):  /* Message In   -> Data Out     */
		fas216_starttransfer(info, DMA_OUT, 1);
		return;

						/* Reselmsgin   -> Status       */
	case STATE(STAT_STATUS, PHASE_RECONNECTED):
		fas216_finish_reconnect(info);
		goto status;
	case STATE(STAT_STATUS, PHASE_DATAOUT): /* Data Out     -> Status       */
	case STATE(STAT_STATUS, PHASE_DATAIN):  /* Data In      -> Status       */
		fas216_stoptransfer(info);
	case STATE(STAT_STATUS, PHASE_SELSTEPS):/* Sel w/ steps -> Status       */
	case STATE(STAT_STATUS, PHASE_MSGOUT):  /* Message Out  -> Status       */
	case STATE(STAT_STATUS, PHASE_COMMAND): /* Command      -> Status       */
	case STATE(STAT_STATUS, PHASE_MSGIN):   /* Message In   -> Status       */
	status:
		outb(CMD_INITCMDCOMPLETE, REG_CMD(info));
		info->scsi.phase = PHASE_STATUS;
		return;

	case STATE(STAT_MESGIN, PHASE_DATAOUT): /* Data Out     -> Message In   */
	case STATE(STAT_MESGIN, PHASE_DATAIN):  /* Data In      -> Message In   */
		fas216_stoptransfer(info);
	case STATE(STAT_MESGIN, PHASE_COMMAND):	/* Command	-> Message In	*/
	case STATE(STAT_MESGIN, PHASE_SELSTEPS):/* Sel w/ steps -> Message In   */
	case STATE(STAT_MESGIN, PHASE_MSGOUT):  /* Message Out  -> Message In   */
		info->scsi.msgin_fifo = inb(REG_CFIS(info)) & CFIS_CF;
		outb(CMD_FLUSHFIFO, REG_CMD(info));
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		info->scsi.phase = PHASE_MSGIN;
		return;

						/* Reselmsgin   -> Message In   */
	case STATE(STAT_MESGIN, PHASE_RECONNECTED):
	case STATE(STAT_MESGIN, PHASE_MSGIN):
		info->scsi.msgin_fifo = inb(REG_CFIS(info)) & CFIS_CF;
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		return;

						/* Reselmsgin   -> Command      */
	case STATE(STAT_COMMAND, PHASE_RECONNECTED):
		fas216_finish_reconnect(info);
	case STATE(STAT_COMMAND, PHASE_MSGOUT): /* Message Out  -> Command      */
	case STATE(STAT_COMMAND, PHASE_MSGIN):  /* Message In   -> Command      */
		fas216_send_command(info);
		info->scsi.phase = PHASE_COMMAND;
		return;
						/* Selection    -> Message Out  */
	case STATE(STAT_MESGOUT, PHASE_SELECTION):
		fas216_send_messageout(info, 1);
		return;
						/* Any          -> Message Out  */
	case STATE(STAT_MESGOUT, PHASE_MSGOUT_EXPECT):
		fas216_send_messageout(info, 0);
		return;

	/* Error recovery rules.
	 *   These either attempt to abort or retry the operation.
	 * TODO: we need more of these
	 */
	case STATE(STAT_COMMAND, PHASE_COMMAND):/* Command      -> Command      */
		/* error - we've sent out all the command bytes
		 * we have.
		 * NOTE: we need SAVE DATA POINTERS/RESTORE DATA POINTERS
		 * to include the command bytes sent for this to work
		 * correctly.
		 */
		printk(KERN_ERR "scsi%d.%c: "
			"target trying to receive more command bytes\n",
			info->host->host_no, fas216_target(info));
		outb(CMD_SETATN, REG_CMD(info));
		outb(15, REG_STCL(info));
		outb(0, REG_STCM(info));
		outb(0, REG_STCH(info));
		outb(CMD_PADBYTES | CMD_WITHDMA, REG_CMD(info));
		msgqueue_flush(&info->scsi.msgs);
		msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		return;

						/* Selection    -> Message Out  */
	case STATE(STAT_MESGOUT, PHASE_SELSTEPS):
	case STATE(STAT_MESGOUT, PHASE_MSGOUT): /* Message Out  -> Message Out  */
		/* If we get another message out phase, this
		 * usually means some parity error occurred.
		 * Resend complete set of messages.  If we have
		 * more than 1 byte to send, we need to assert
		 * ATN again.
		 */
		if (msgqueue_msglength(&info->scsi.msgs) > 1)
			outb(CMD_SETATN, REG_CMD(info));

		fas216_send_messageout(info, 0);
		return;
	}

	if (info->scsi.phase == PHASE_MSGIN_DISCONNECT) {
		printk(KERN_ERR "scsi%d.%c: disconnect message received, but bus service %s?\n",
			info->host->host_no, fas216_target(info),
			fas216_bus_phase(stat));
		msgqueue_flush(&info->scsi.msgs);
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		info->scsi.aborting = 1;
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		return;
	}
	printk(KERN_ERR "scsi%d.%c: bus phase %s after %s?\n",
		info->host->host_no, fas216_target(info),
		fas216_bus_phase(stat),
		fas216_drv_phase(info));
	print_debug_list();
	return;

bad_is:
	printk("scsi%d.%c: bus service at step %d?\n",
		info->host->host_no, fas216_target(info),
		ssr & IS_BITS);
	print_debug_list();

	fas216_done(info, DID_ERROR);
}

/* Function: void fas216_funcdone_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
 * Purpose : handle a function done interrupt from FAS216 chip
 * Params  : info - interface which caused function done interrupt
 *           stat - Status register contents
 *           ssr  - SCSI Status register contents
 */
static void fas216_funcdone_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
{
	int status, message;

	fas216_checkmagic(info);

#ifdef DEBUG_FUNCTIONDONE
	printk("scsi%d.%c: function done: stat=%X ssr=%X phase=%02X\n",
		info->host->host_no, fas216_target(info), stat, ssr, info->scsi.phase);
#endif
	switch (info->scsi.phase) {
	case PHASE_STATUS:			/* status phase - read status and msg	*/
		status = inb(REG_FF(info));
		message = inb(REG_FF(info));
		info->scsi.SCp.Message = message;
		info->scsi.SCp.Status = status;
		info->scsi.phase = PHASE_DONE;
		outb(CMD_MSGACCEPTED, REG_CMD(info));
		break;

	case PHASE_IDLE:			/* reselected?				*/
	case PHASE_MSGIN:			/* message in phase			*/
	case PHASE_RECONNECTED:			/* reconnected command			*/
		if ((stat & STAT_BUSMASK) == STAT_MESGIN) {
			info->scsi.msgin_fifo = inb(REG_CFIS(info)) & CFIS_CF;
			fas216_message(info);
			break;
		}

	default:
		printk("scsi%d.%c: internal phase %s for function done?"
			"  What do I do with this?\n",
			info->host->host_no, fas216_target(info),
			fas216_drv_phase(info));
	}
}

/* Function: void fas216_intr(struct Scsi_Host *instance)
 * Purpose : handle interrupts from the interface to progress a command
 * Params  : instance - interface to service
 */
void fas216_intr(struct Scsi_Host *instance)
{
	FAS216_Info *info = (FAS216_Info *)instance->hostdata;
	unsigned char isr, ssr, stat;

	fas216_checkmagic(info);

	stat = inb(REG_STAT(info));
	ssr = inb(REG_IS(info));
	isr = inb(REG_INST(info));

	add_debug_list(stat, ssr, isr, info->scsi.phase);

	if (stat & STAT_INT) {
		if (isr & INST_BUSRESET) {
			printk(KERN_DEBUG "scsi%d.H: bus reset detected\n", instance->host_no);
			scsi_report_bus_reset(instance, 0);
		} else if (isr & INST_ILLEGALCMD) {
			printk(KERN_CRIT "scsi%d.H: illegal command given\n", instance->host_no);
			fas216_dumpstate(info);
		} else if (isr & INST_DISCONNECT)
			fas216_disconnect_intr(info);
		else if (isr & INST_RESELECTED)		/* reselected			*/
			fas216_reselected_intr(info);
		else if (isr & INST_BUSSERVICE)		/* bus service request		*/
			fas216_busservice_intr(info, stat, ssr);
		else if (isr & INST_FUNCDONE)		/* function done		*/
			fas216_funcdone_intr(info, stat, ssr);
		else
		    	printk("scsi%d.%c: unknown interrupt received:"
				" phase %s isr %02X ssr %02X stat %02X\n",
				instance->host_no, fas216_target(info),
				fas216_drv_phase(info), isr, ssr, stat);
	}
}

/* Function: void fas216_kick(FAS216_Info *info)
 * Purpose : kick a command to the interface - interface should be idle
 * Params  : info - our host interface to kick
 * Notes   : Interrupts are always disabled!
 */
static void fas216_kick(FAS216_Info *info)
{
	Scsi_Cmnd *SCpnt = NULL;
	int tot_msglen, from_queue = 0, disconnect_ok;

	fas216_checkmagic(info);

	/*
	 * Obtain the next command to process.
	 */
	do {
		if (info->reqSCpnt) {
			SCpnt = info->reqSCpnt;
			info->reqSCpnt = NULL;
			break;
		}

		if (info->origSCpnt) {
			SCpnt = info->origSCpnt;
			info->origSCpnt = NULL;
			break;
		}

		/* retrieve next command */
		if (!SCpnt) {
			SCpnt = queue_remove_exclude(&info->queues.issue,
						     info->busyluns);
			from_queue = 1;
			break;
		}
	} while (0);

	if (!SCpnt) /* no command pending - just exit */
		return;

	if (info->scsi.disconnectable && info->SCpnt) {
		queue_add_cmd_tail(&info->queues.disconnected, info->SCpnt);
		info->scsi.disconnectable = 0;
		info->SCpnt = NULL;
		printk("scsi%d.%c: moved command to disconnected queue\n",
			info->host->host_no, fas216_target(info));
	}

	/*
	 * claim host busy
	 */
	info->scsi.phase = PHASE_SELECTION;
	info->SCpnt = SCpnt;
	info->scsi.SCp = SCpnt->SCp;
	info->dma.transfer_type = fasdma_none;

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: starting cmd %02X",
		info->host->host_no, '0' + SCpnt->target,
		SCpnt->cmnd[0]);
#endif

	if (from_queue) {
#ifdef SCSI2_TAG
		/*
		 * tagged queuing - allocate a new tag to this command
		 */
		if (SCpnt->device->tagged_queue && SCpnt->cmnd[0] != REQUEST_SENSE &&
		    SCpnt->cmnd[0] != INQUIRY) {
		    SCpnt->device->current_tag += 1;
			if (SCpnt->device->current_tag == 0)
			    SCpnt->device->current_tag = 1;
				SCpnt->tag = SCpnt->device->current_tag;
		} else
#endif
			set_bit(SCpnt->target * 8 + SCpnt->lun, info->busyluns);

		info->stats.removes += 1;
		switch (SCpnt->cmnd[0]) {
		case WRITE_6:
		case WRITE_10:
		case WRITE_12:
			info->stats.writes += 1;
			break;
		case READ_6:
		case READ_10:
		case READ_12:
			info->stats.reads += 1;
			break;
		default:
			info->stats.miscs += 1;
			break;
		}
	}

	/*
	 * Don't allow request sense commands to disconnect.
	 */
	disconnect_ok = SCpnt->cmnd[0] != REQUEST_SENSE &&
			info->device[SCpnt->target].disconnect_ok;

	/*
	 * build outgoing message bytes
	 */
	msgqueue_flush(&info->scsi.msgs);
	msgqueue_addmsg(&info->scsi.msgs, 1, IDENTIFY(disconnect_ok, SCpnt->lun));

	/*
	 * add tag message if required
	 */
	if (SCpnt->tag)
		msgqueue_addmsg(&info->scsi.msgs, 2, SIMPLE_QUEUE_TAG, SCpnt->tag);

	do {
#ifdef SCSI2_WIDE
		if (info->device[SCpnt->target].wide_state == neg_wait) {
			info->device[SCpnt->target].wide_state = neg_inprogress;
			msgqueue_addmsg(&info->scsi.msgs, 4,
					EXTENDED_MESSAGE, 2, EXTENDED_WDTR,
					info->ifcfg.wide_max_size);
			break;
		}
#endif
#ifdef SCSI2_SYNC
		if ((info->device[SCpnt->target].sync_state == neg_wait ||
		     info->device[SCpnt->target].sync_state == neg_complete) &&
		    (SCpnt->cmnd[0] == REQUEST_SENSE ||
		     SCpnt->cmnd[0] == INQUIRY)) {
			info->device[SCpnt->target].sync_state = neg_inprogress;
			msgqueue_addmsg(&info->scsi.msgs, 5,
					EXTENDED_MESSAGE, 3, EXTENDED_SDTR,
					1000 / info->ifcfg.clockrate,
					info->ifcfg.sync_max_depth);
			break;
		}
#endif
	} while (0);

	/* following what the ESP driver says */
	outb(0, REG_STCL(info));
	outb(0, REG_STCM(info));
	outb(0, REG_STCH(info));
	outb(CMD_NOP | CMD_WITHDMA, REG_CMD(info));

	/* flush FIFO */
	outb(CMD_FLUSHFIFO, REG_CMD(info));

	/* load bus-id and timeout */
	outb(BUSID(SCpnt->target), REG_SDID(info));
	outb(info->ifcfg.select_timeout, REG_STIM(info));

	/* synchronous transfers */
	fas216_set_sync(info, SCpnt->target);

	tot_msglen = msgqueue_msglength(&info->scsi.msgs);

#ifdef DEBUG_MESSAGES
	{
		struct message *msg;
		int msgnr = 0, i;

		printk("scsi%d.%c: message out: ",
			info->host->host_no, '0' + SCpnt->target);
		while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
			printk("{ ");
			for (i = 0; i < msg->length; i++)
				printk("%02x ", msg->msg[i]);
			printk("} ");
		}
		printk("\n");
	}
#endif

	if (tot_msglen == 1 || tot_msglen == 3) {
		/*
		 * We have an easy message length to send...
		 */
		struct message *msg;
		int msgnr = 0, i;

		info->scsi.phase = PHASE_SELSTEPS;

		/* load message bytes */
		while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
			for (i = 0; i < msg->length; i++)
				outb(msg->msg[i], REG_FF(info));
			msg->fifo = tot_msglen - (inb(REG_CFIS(info)) & CFIS_CF);
		}

		/* load command */
		for (i = 0; i < SCpnt->cmd_len; i++)
			outb(SCpnt->cmnd[i], REG_FF(info));

		if (tot_msglen == 1)
			outb(CMD_SELECTATN, REG_CMD(info));
		else
			outb(CMD_SELECTATN3, REG_CMD(info));
	} else {
		/*
		 * We have an unusual number of message bytes to send.
		 *  Load first byte into fifo, and issue SELECT with ATN and
		 *  stop steps.
		 */
		struct message *msg = msgqueue_getmsg(&info->scsi.msgs, 0);

		outb(msg->msg[0], REG_FF(info));
		msg->fifo = 1;

		outb(CMD_SELECTATNSTOP, REG_CMD(info));
	}

#ifdef DEBUG_CONNECT
	printk(", data pointers [%p, %X]\n",
		info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
	/* should now get either DISCONNECT or (FUNCTION DONE with BUS SERVICE) intr */
}

/* Function: void fas216_rq_sns_done(info, SCpnt, result)
 * Purpose : Finish processing automatic request sense command
 * Params  : info   - interface that completed
 *	     SCpnt  - command that completed
 *	     result - driver byte of result
 */
static void
fas216_rq_sns_done(FAS216_Info *info, Scsi_Cmnd *SCpnt, unsigned int result)
{
#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: request sense complete, result=%04X%02X%02X\n",
		info->host->host_no, '0' + SCpnt->target, result,
		SCpnt->SCp.Message, SCpnt->SCp.Status);
#endif

	if (result != DID_OK || SCpnt->SCp.Status != GOOD)
		/*
		 * Something went wrong.  Make sure that we don't
		 * have valid data in the sense buffer that could
		 * confuse the higher levels.
		 */
		memset(SCpnt->sense_buffer, 0, sizeof(SCpnt->sense_buffer));

	/*
	 * Note that we don't set SCpnt->result, since that should
	 * reflect the status of the command that we were asked by
	 * the upper layers to process.  This would have been set
	 * correctly by fas216_std_done.
	 */
	SCpnt->scsi_done(SCpnt);
}

/* Function: void fas216_std_done(info, SCpnt, result)
 * Purpose : Finish processing of standard command
 * Params  : info   - interface that completed
 *	     SCpnt  - command that completed
 *	     result - driver byte of result
 */
static void
fas216_std_done(FAS216_Info *info, Scsi_Cmnd *SCpnt, unsigned int result)
{
	info->stats.fins += 1;

	SCpnt->result = result << 16 | info->scsi.SCp.Message << 8 |
			info->scsi.SCp.Status;

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: command complete, result=%08X, command=",
		info->host->host_no, '0' + SCpnt->target, SCpnt->result);
	print_command(SCpnt->cmnd);
#endif

	/*
	 * If the driver detected an error, or the command
	 * was request sense, then we're all done.
	 */
	if (result != DID_OK || SCpnt->cmnd[0] == REQUEST_SENSE)
		goto done;

	/*
	 * If the command returned CHECK_CONDITION status,
	 * request the sense information.
	 */
	if (info->scsi.SCp.Status == CHECK_CONDITION)
		goto request_sense;

	/*
	 * If the command did not complete with GOOD status,
	 * we are all done here.
	 */
	if (info->scsi.SCp.Status != GOOD)
		goto done;

	/*
	 * We have successfully completed a command.  Make sure that
	 * we do not have any buffers left to transfer.  The world
	 * is not perfect, and we seem to occasionally hit this.
	 * It can be indicative of a buggy driver, target or the upper
	 * levels of the SCSI code.
	 */
	if (info->scsi.SCp.ptr) {
		switch (SCpnt->cmnd[0]) {
		case INQUIRY:
		case START_STOP:
//		case READ_CAPACITY:
		case MODE_SENSE:
			break;

		default:
			printk(KERN_ERR "scsi%d.%c: incomplete data transfer "
				"detected: res=%08X ptr=%p len=%X command=",
				info->host->host_no, '0' + SCpnt->target,
				SCpnt->result, info->scsi.SCp.ptr,
				info->scsi.SCp.this_residual);
			print_command(SCpnt->cmnd);
		}
	}

done:	SCpnt->scsi_done(SCpnt);
	return;

request_sense:
	memset(SCpnt->cmnd, 0, sizeof (SCpnt->cmnd));
	SCpnt->cmnd[0] = REQUEST_SENSE;
	SCpnt->cmnd[1] = SCpnt->lun << 5;
	SCpnt->cmnd[4] = sizeof(SCpnt->sense_buffer);
	SCpnt->cmd_len = COMMAND_SIZE(SCpnt->cmnd[0]);
	SCpnt->SCp.buffer = NULL;
	SCpnt->SCp.buffers_residual = 0;
	SCpnt->SCp.ptr = (char *)SCpnt->sense_buffer;
	SCpnt->SCp.this_residual = sizeof(SCpnt->sense_buffer);
	SCpnt->SCp.Message = 0;
	SCpnt->SCp.Status = 0;
	SCpnt->sc_data_direction = SCSI_DATA_READ;
	SCpnt->use_sg = 0;
	SCpnt->tag = 0;
	SCpnt->host_scribble = (void *)fas216_rq_sns_done;

	/*
	 * Place this command into the high priority "request
	 * sense" slot.  This will be the very next command
	 * executed, unless a target connects to us.
	 */
	if (info->reqSCpnt)
		printk(KERN_WARNING "scsi%d.%c: loosing request command\n",
			info->host->host_no, '0' + SCpnt->target);
	info->reqSCpnt = SCpnt;
}

/* Function: void fas216_done(FAS216_Info *info, unsigned int result)
 * Purpose : complete processing for current command
 * Params  : info   - interface that completed
 *	     result - driver byte of result
 */
static void fas216_done(FAS216_Info *info, unsigned int result)
{
	void (*fn)(FAS216_Info *, Scsi_Cmnd *, unsigned int);
	Scsi_Cmnd *SCpnt;

	fas216_checkmagic(info);

	if (!info->SCpnt)
		goto no_command;

	SCpnt = info->SCpnt;
	info->SCpnt = NULL;
    	info->scsi.phase = PHASE_IDLE;

	if (!SCpnt->scsi_done)
		goto no_done;

	if (info->scsi.aborting) {
		printk("scsi%d.%c: uncaught abort - returning DID_ABORT\n",
			info->host->host_no, fas216_target(info));
		result = DID_ABORT;
		info->scsi.aborting = 0;
	}

	/*
	 * Sanity check the completion - if we have zero bytes left
	 * to transfer, we should not have a valid pointer.
	 */
	if (info->scsi.SCp.ptr && info->scsi.SCp.this_residual == 0) {
		printk("scsi%d.%c: zero bytes left to transfer, but "
		       "buffer pointer still valid: ptr=%p len=%08x command=",
		       info->host->host_no, '0' + SCpnt->target,
		       info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
		info->scsi.SCp.ptr = NULL;
		print_command(SCpnt->cmnd);
	}

	/*
	 * Clear down this command as completed.  If we need to request
	 * the sense information, fas216_kick will re-assert the busy
	 * status.
	 */
	clear_bit(SCpnt->target * 8 + SCpnt->lun, info->busyluns);

	fn = (void (*)(FAS216_Info *, Scsi_Cmnd *, unsigned int))SCpnt->host_scribble;
	fn(info, SCpnt, result);

	if (info->scsi.irq != NO_IRQ)
		fas216_kick(info);
	return;

no_command:
	panic("scsi%d.H: null command in fas216_done",
		info->host->host_no);
no_done:
	panic("scsi%d.H: null scsi_done function in fas216_done",
		info->host->host_no);
}

/* Function: int fas216_queue_command(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
 * Purpose : queue a command for adapter to process.
 * Params  : SCpnt - Command to queue
 *	     done  - done function to call once command is complete
 * Returns : 0 - success, else error
 * Notes   : io_request_lock is held, interrupts are disabled.
 */
int fas216_queue_command(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;
	int result;

	fas216_checkmagic(info);

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: received queuable command (%p) %02X\n",
		SCpnt->host->host_no, '0' + SCpnt->target,
		SCpnt, SCpnt->cmnd[0]);
#endif

	SCpnt->scsi_done = done;
	SCpnt->host_scribble = (void *)fas216_std_done;
	SCpnt->result = 0;
	SCpnt->SCp.Message = 0;
	SCpnt->SCp.Status = 0;

	if (SCpnt->use_sg) {
		unsigned long len = 0;
		int buf;

		SCpnt->SCp.buffer = (struct scatterlist *) SCpnt->buffer;
		SCpnt->SCp.buffers_residual = SCpnt->use_sg - 1;
		SCpnt->SCp.ptr = (char *) SCpnt->SCp.buffer->address;
		SCpnt->SCp.this_residual = SCpnt->SCp.buffer->length;
		/*
		 * Calculate correct buffer length.  Some commands
		 * come in with the wrong request_bufflen.
		 */
		for (buf = 0; buf <= SCpnt->SCp.buffers_residual; buf++)
			len += SCpnt->SCp.buffer[buf].length;

		if (SCpnt->request_bufflen != len)
			printk(KERN_WARNING "scsi%d.%c: bad request buffer "
			       "length %d, should be %ld\n", info->host->host_no,
			       '0' + SCpnt->target, SCpnt->request_bufflen, len);
		SCpnt->request_bufflen = len;
	} else {
		SCpnt->SCp.buffer = NULL;
		SCpnt->SCp.buffers_residual = 0;
		SCpnt->SCp.ptr = (unsigned char *)SCpnt->request_buffer;
		SCpnt->SCp.this_residual = SCpnt->request_bufflen;
	}

	/*
	 * If the upper SCSI layers pass a buffer, but zero length,
	 * we aren't interested in the buffer pointer.
	 */
	if (SCpnt->SCp.this_residual == 0 && SCpnt->SCp.ptr) {
#if 0
		printk(KERN_WARNING "scsi%d.%c: zero length buffer passed for "
		       "command ", info->host->host_no, '0' + SCpnt->target);
		print_command(SCpnt->cmnd);
#endif
		SCpnt->SCp.ptr = NULL;
	}

	info->stats.queues += 1;
	SCpnt->tag = 0;

	/*
	 * Add command into execute queue and let it complete under
	 * whatever scheme we're using.
	 */
	result = !queue_add_cmd_ordered(&info->queues.issue, SCpnt);

	/*
	 * If we successfully added the command,
	 * kick the interface to get it moving.
	 */
	if (result == 0 && (!info->SCpnt || info->scsi.disconnectable))
		fas216_kick(info);

	return result;
}

/* Function: void fas216_internal_done(Scsi_Cmnd *SCpnt)
 * Purpose : trigger restart of a waiting thread in fas216_command
 * Params  : SCpnt - Command to wake
 */
static void fas216_internal_done(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;

	fas216_checkmagic(info);

	info->internal_done = 1;
}

/* Function: int fas216_command(Scsi_Cmnd *SCpnt)
 * Purpose : queue a command for adapter to process.
 * Params  : SCpnt - Command to queue
 * Returns : scsi result code
 * Notes   : io_request_lock is held, interrupts are disabled.
 */
int fas216_command(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;

	fas216_checkmagic(info);

	/*
	 * We should only be using this if we don't have an interrupt.
	 * Provide some "incentive" to use the queueing code.
	 */
	if (info->scsi.irq != NO_IRQ)
		BUG();

	info->internal_done = 0;
	fas216_queue_command(SCpnt, fas216_internal_done);

	/*
	 * This wastes time, since we can't return until the command is
	 * complete. We can't sleep either since we may get re-entered!
	 * However, we must re-enable interrupts, or else we'll be
	 * waiting forever.
	 */
	spin_unlock_irq(&io_request_lock);

	while (!info->internal_done) {
		/*
		 * If we don't have an IRQ, then we must poll the card for
		 * it's interrupt, and use that to call this driver's
		 * interrupt routine.  That way, we keep the command
		 * progressing.  Maybe we can add some inteligence here
		 * and go to sleep if we know that the device is going
		 * to be some time (eg, disconnected).
		 */
		if (inb(REG_STAT(info)) & STAT_INT) {
			spin_lock_irq(&io_request_lock);
			fas216_intr(info->host);
			spin_unlock_irq(&io_request_lock);
		}
	}

	spin_lock_irq(&io_request_lock);

	return SCpnt->result;
}

enum res_abort {
	res_failed,		/* unable to abort		*/
	res_success,		/* command on issue queue	*/
	res_success_clear,	/* command marked tgt/lun busy	*/
	res_hw_abort		/* command on disconnected dev	*/
};

/*
 * Prototype: enum res_abort fas216_do_abort(FAS216_Info *info, Scsi_Cmnd *SCpnt)
 * Purpose  : decide how to abort a command
 * Params   : SCpnt - command to abort
 * Returns  : abort status
 */
static enum res_abort
fas216_do_abort(FAS216_Info *info, Scsi_Cmnd *SCpnt)
{
	enum res_abort res = res_failed;

	if (queue_remove_cmd(&info->queues.issue, SCpnt)) {
		/*
		 * The command was on the issue queue, and has not been
		 * issued yet.  We can remove the command from the queue,
		 * and acknowledge the abort.  Neither the device nor the
		 * interface know about the command.
		 */
		printk("on issue queue ");

		res = res_success;
	} else if (queue_remove_cmd(&info->queues.disconnected, SCpnt)) {
		/*
		 * The command was on the disconnected queue.  We must
		 * reconnect with the device if possible, and send it
		 * an abort message.
		 */
		printk("on disconnected queue ");

		res = res_hw_abort;
	} else if (info->SCpnt == SCpnt) {
		printk("executing ");

		switch (info->scsi.phase) {
		/*
		 * If the interface is idle, and the command is 'disconnectable',
		 * then it is the same as on the disconnected queue.
		 */
		case PHASE_IDLE:
			if (info->scsi.disconnectable) {
				info->scsi.disconnectable = 0;
				info->SCpnt = NULL;
				res = res_hw_abort;
			}
			break;

		default:
			break;
		}
	} else if (info->origSCpnt == SCpnt) {
		/*
		 * The command will be executed next, but a command
		 * is currently using the interface.  This is similar to
		 * being on the issue queue, except the busylun bit has
		 * been set.
		 */
		info->origSCpnt = NULL;
		printk("waiting for execution ");
		res = res_success_clear;
	} else
		printk("unknown ");

	return res;
}

/* Function: int fas216_eh_abort(Scsi_Cmnd *SCpnt)
 * Purpose : abort this command
 * Params  : SCpnt - command to abort
 * Returns : FAILED if unable to abort
 * Notes   : io_request_lock is taken, and irqs are disabled
 */
int fas216_eh_abort(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;
	int result = FAILED;

	fas216_checkmagic(info);

	info->stats.aborts += 1;

	print_debug_list();
	fas216_dumpstate(info);
	fas216_dumpinfo(info);

	printk(KERN_WARNING "scsi%d: abort ", info->host->host_no);

	switch (fas216_do_abort(info, SCpnt)) {
	/*
	 * We managed to find the command and cleared it out.
	 * We do not expect the command to be executing on the
	 * target, but we have set the busylun bit.
	 */
	case res_success_clear:
		printk("clear ");
		clear_bit(SCpnt->target * 8 + SCpnt->lun, info->busyluns);

	/*
	 * We found the command, and cleared it out.  Either
	 * the command is still known to be executing on the
	 * target, or the busylun bit is not set.
	 */
	case res_success:
		printk("success\n");
		result = SUCCESS;
		break;

	/*
	 * We need to reconnect to the target and send it an
	 * ABORT or ABORT_TAG message.  We can only do this
	 * if the bus is free.
	 */
	case res_hw_abort:
		

	/*
	 * We are unable to abort the command for some reason.
	 */
	default:
	case res_failed:
		printk("failed\n");
		break;
	}

	return result;
}

/* Function: void fas216_reset_state(FAS216_Info *info)
 * Purpose : Initialise driver internal state
 * Params  : info - state to initialise
 */
static void fas216_reset_state(FAS216_Info *info)
{
	neg_t sync_state, wide_state;
	int i;

	fas216_checkmagic(info);

	/*
	 * Clear out all stale info in our state structure
	 */
	memset(info->busyluns, 0, sizeof(info->busyluns));
	msgqueue_flush(&info->scsi.msgs);
	info->scsi.reconnected.target = 0;
	info->scsi.reconnected.lun = 0;
	info->scsi.reconnected.tag = 0;
	info->scsi.disconnectable = 0;
	info->scsi.aborting = 0;
	info->scsi.phase = PHASE_IDLE;
	info->scsi.async_stp =
			fas216_syncperiod(info, info->ifcfg.asyncperiod);

	if (info->ifcfg.wide_max_size == 0)
		wide_state = neg_invalid;
	else
#ifdef SCSI2_WIDE
		wide_state = neg_wait;
#else
		wide_state = neg_invalid;
#endif

	if (info->host->dma_channel == NO_DMA || !info->dma.setup)
		sync_state = neg_invalid;
	else
#ifdef SCSI2_SYNC
		sync_state = neg_wait;
#else
		sync_state = neg_invalid;
#endif

	for (i = 0; i < 8; i++) {
		info->device[i].disconnect_ok	= info->ifcfg.disconnect_ok;
		info->device[i].sync_state	= sync_state;
		info->device[i].wide_state	= wide_state;
		info->device[i].period		= info->ifcfg.asyncperiod / 4;
		info->device[i].stp		= info->scsi.async_stp;
		info->device[i].sof		= 0;
		info->device[i].wide_xfer	= 0;
	}

	/*
	 * Drain all commands on disconnected queue
	 */
	while (queue_remove(&info->queues.disconnected) != NULL);

	/*
	 * Remove executing commands.
	 */
	info->SCpnt     = NULL;
	info->reqSCpnt  = NULL;
	info->origSCpnt = NULL;
}

/* Function: int fas216_eh_device_reset(Scsi_Cmnd *SCpnt)
 * Purpose : Reset the device associated with this command
 * Params  : SCpnt - command specifing device to reset
 * Returns : FAILED if unable to reset
 */
int fas216_eh_device_reset(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;

	printk("scsi%d.%c: "__FUNCTION__": called\n",
		info->host->host_no, '0' + SCpnt->target);
	return FAILED;
}

/* Function: int fas216_eh_bus_reset(Scsi_Cmnd *SCpnt)
 * Purpose : Reset the bus associated with the command
 * Params  : SCpnt - command specifing bus to reset
 * Returns : FAILED if unable to reset
 * Notes   : io_request_lock is taken, and irqs are disabled
 */
int fas216_eh_bus_reset(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;
	int result = FAILED;

	fas216_checkmagic(info);

	info->stats.bus_resets += 1;

	printk("scsi%d.%c: "__FUNCTION__": resetting bus\n",
		info->host->host_no, '0' + SCpnt->target);

	/*
	 * Attempt to stop all activity on this interface.
	 */
	outb(info->scsi.cfg[2], REG_CNTL3(info));
	fas216_stoptransfer(info);

	/*
	 * Clear any pending interrupts
	 */
	while (inb(REG_STAT(info)) & STAT_INT)
		inb(REG_INST(info));

	/*
	 * Reset the SCSI bus
	 */
	outb(CMD_RESETSCSI, REG_CMD(info));
	udelay(5);

	/*
	 * Clear reset interrupt
	 */
	if (inb(REG_STAT(info)) & STAT_INT &&
	    inb(REG_INST(info)) & INST_BUSRESET)
		result = SUCCESS;

	fas216_reset_state(info);

	return result;
}

/* Function: void fas216_init_chip(FAS216_Info *info)
 * Purpose : Initialise FAS216 state after reset
 * Params  : info - state structure for interface
 */
static void fas216_init_chip(FAS216_Info *info)
{
	outb(fas216_clockrate(info->ifcfg.clockrate), REG_CLKF(info));
	outb(info->scsi.cfg[0], REG_CNTL1(info));
	outb(info->scsi.cfg[1], REG_CNTL2(info));
	outb(info->scsi.cfg[2], REG_CNTL3(info));
	outb(info->ifcfg.select_timeout, REG_STIM(info));
	outb(0, REG_SOF(info));
	outb(info->scsi.async_stp, REG_STP(info));
	outb(info->scsi.cfg[0], REG_CNTL1(info));
}

/* Function: int fas216_eh_host_reset(Scsi_Cmnd *SCpnt)
 * Purpose : Reset the host associated with this command
 * Params  : SCpnt - command specifing host to reset
 * Returns : FAILED if unable to reset
 * Notes   : io_request_lock is taken, and irqs are disabled
 */
int fas216_eh_host_reset(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;

	fas216_checkmagic(info);

	printk("scsi%d.%c: "__FUNCTION__": resetting host\n",
		info->host->host_no, '0' + SCpnt->target);

	/*
	 * Reset the SCSI chip.
	 */
	outb(CMD_RESETCHIP, REG_CMD(info));

	/*
	 * Ugly ugly ugly!
	 * We need to release the io_request_lock and enable
	 * IRQs if we sleep, but we must relock and disable
	 * IRQs after the sleep.
	 */
	spin_unlock_irq(&io_request_lock);
	scsi_sleep(25*HZ/100);
	spin_lock_irq(&io_request_lock);

	/*
	 * Release the SCSI reset.
	 */
	outb(CMD_NOP, REG_CMD(info));

	fas216_init_chip(info);

	return SUCCESS;
}

#define TYPE_UNKNOWN	0
#define TYPE_NCR53C90	1
#define TYPE_NCR53C90A	2
#define TYPE_NCR53C9x	3
#define TYPE_Am53CF94	4
#define TYPE_EmFAS216	5
#define TYPE_QLFAS216	6

static char *chip_types[] = {
	"unknown",
	"NS NCR53C90",
	"NS NCR53C90A",
	"NS NCR53C9x",
	"AMD Am53CF94",
	"Emulex FAS216",
	"QLogic FAS216"
};

static int fas216_detect_type(FAS216_Info *info)
{
	int family, rev;

	/*
	 * Reset the chip.
	 */
	outb(CMD_RESETCHIP, REG_CMD(info));
	udelay(50);
	outb(CMD_NOP, REG_CMD(info));

	/*
	 * Check to see if control reg 2 is present.
	 */
	outb(0, REG_CNTL3(info));
	outb(CNTL2_S2FE, REG_CNTL2(info));

	/*
	 * If we are unable to read back control reg 2
	 * correctly, it is not present, and we have a
	 * NCR53C90.
	 */
	if ((inb(REG_CNTL2(info)) & (~0xe0)) != CNTL2_S2FE)
		return TYPE_NCR53C90;

	/*
	 * Now, check control register 3
	 */
	outb(0, REG_CNTL2(info));
	outb(0, REG_CNTL3(info));
	outb(5, REG_CNTL3(info));

	/*
	 * If we are unable to read the register back
	 * correctly, we have a NCR53C90A
	 */
	if (inb(REG_CNTL3(info)) != 5)
		return TYPE_NCR53C90A;

	/*
	 * Now read the ID from the chip.
	 */
	outb(0, REG_CNTL3(info));

	outb(CNTL3_ADIDCHK, REG_CNTL3(info));
	outb(0, REG_CNTL3(info));

	outb(CMD_RESETCHIP, REG_CMD(info));
	udelay(5);
	outb(CMD_WITHDMA | CMD_NOP, REG_CMD(info));

	outb(CNTL2_ENF, REG_CNTL2(info));
	outb(CMD_RESETCHIP, REG_CMD(info));
	udelay(5);
	outb(CMD_NOP, REG_CMD(info));

	rev     = inb(REG1_ID(info));
	family  = rev >> 3;
	rev    &= 7;

	switch (family) {
	case 0x01:
		if (rev == 4)
			return TYPE_Am53CF94;
		break;

	case 0x02:
		switch (rev) {
		case 2:
			return TYPE_EmFAS216;
		case 3:
			return TYPE_QLFAS216;
		}
		break;

	default:
		break;
	}
	printk("family %x rev %x\n", family, rev);
	return TYPE_NCR53C9x;
}

/* Function: int fas216_init(struct Scsi_Host *instance)
 * Purpose : initialise FAS/NCR/AMD SCSI ic.
 * Params  : instance - a driver-specific filled-out structure
 * Returns : 0 on success
 */
int fas216_init(struct Scsi_Host *instance)
{
	FAS216_Info *info = (FAS216_Info *)instance->hostdata;
	int type;

	info->magic_start = MAGIC;
	info->magic_end   = MAGIC;
	info->host        = instance;
	info->scsi.cfg[0] = instance->this_id;
	info->scsi.cfg[1] = CNTL2_ENF | CNTL2_S2FE;
	info->scsi.cfg[2] = info->ifcfg.cntl3 | CNTL3_ADIDCHK | CNTL3_G2CB;

	memset(&info->stats, 0, sizeof(info->stats));

	msgqueue_initialise(&info->scsi.msgs);

	if (!queue_initialise(&info->queues.issue))
		return 1;

	if (!queue_initialise(&info->queues.disconnected)) {
		queue_free(&info->queues.issue);
		return 1;
	}

	fas216_reset_state(info);
	type = fas216_detect_type(info);
	info->scsi.type = chip_types[type];

	udelay(300);

	/*
	 * Initialise the chip correctly.
	 */
	fas216_init_chip(info);

	/*
	 * Reset the SCSI bus.  We don't want to see
	 * the resulting reset interrupt, so mask it
	 * out.
	 */
	outb(info->scsi.cfg[0] | CNTL1_DISR, REG_CNTL1(info));
	outb(CMD_RESETSCSI, REG_CMD(info));

	/*
	 * scsi standard says wait 250ms
	 */
	spin_unlock_irq(&io_request_lock);
	scsi_sleep(25*HZ/100);
	spin_lock_irq(&io_request_lock);

	outb(info->scsi.cfg[0], REG_CNTL1(info));
	inb(REG_INST(info));

	fas216_checkmagic(info);

	return 0;
}

/* Function: int fas216_release(struct Scsi_Host *instance)
 * Purpose : release all resources and put everything to bed for
 *           FAS/NCR/AMD SCSI ic.
 * Params  : instance - a driver-specific filled-out structure
 * Returns : 0 on success
 */
int fas216_release(struct Scsi_Host *instance)
{
	FAS216_Info *info = (FAS216_Info *)instance->hostdata;

	fas216_checkmagic(info);

	outb(CMD_RESETCHIP, REG_CMD(info));
	queue_free(&info->queues.disconnected);
	queue_free(&info->queues.issue);

	return 0;
}

/*
 * Function: int fas216_info(FAS216_Info *info, char *buffer)
 * Purpose : generate a string containing information about this
 *	     host.
 * Params  : info   - FAS216 host information
 *	     buffer - string buffer to build string
 * Returns : size of built string
 */
int fas216_info(FAS216_Info *info, char *buffer)
{
	char *p = buffer;

	p += sprintf(p, "(%s) at port 0x%08lX ",
		     info->scsi.type, info->host->io_port);

	if (info->host->irq != NO_IRQ)
		p += sprintf(p, "irq %d ", info->host->irq);
	else
		p += sprintf(p, "no irq ");

	if (info->host->dma_channel != NO_DMA)
		p += sprintf(p, "dma %d ", info->host->dma_channel);
	else
		p += sprintf(p, "no dma ");

	return p - buffer;
}

int fas216_print_host(FAS216_Info *info, char *buffer)
{
	
	return sprintf(buffer,
			"\n"
			"Chip    : %s\n"
			" Address: 0x%08lX\n"
			" IRQ    : %d\n"
			" DMA    : %d\n",
			info->scsi.type, info->host->io_port,
			info->host->irq, info->host->dma_channel);
}

int fas216_print_stats(FAS216_Info *info, char *buffer)
{
	return sprintf(buffer,
			"\n"
			"Command Statistics:\n"
			" Queued     : %u\n"
			" Issued     : %u\n"
			" Completed  : %u\n"
			" Reads      : %u\n"
			" Writes     : %u\n"
			" Others     : %u\n"
			" Disconnects: %u\n"
			" Aborts     : %u\n"
			" Bus resets : %u\n"
			" Host resets: %u\n",
			info->stats.queues,	 info->stats.removes,
			info->stats.fins,	 info->stats.reads,
			info->stats.writes,	 info->stats.miscs,
			info->stats.disconnects, info->stats.aborts,
			info->stats.bus_resets,	 info->stats.host_resets);
}

int fas216_print_device(FAS216_Info *info, Scsi_Device *scd, char *buffer)
{
	struct fas216_device *dev = &info->device[scd->id];
	int len = 0;
	char *p;

	proc_print_scsidevice(scd, buffer, &len, 0);
	p = buffer + len;

	p += sprintf(p, "  Extensions: ");

	if (scd->tagged_supported)
		p += sprintf(p, "TAG %sabled [%d] ",
			     scd->tagged_queue ? "en" : "dis",
			     scd->current_tag);

	p += sprintf(p, "\n  Transfers : %d-bit ",
		     8 << dev->wide_xfer);

	if (dev->sof)
		p += sprintf(p, "sync offset %d, %d ns\n",
				dev->sof, dev->period * 4);
	else
		p += sprintf(p, "async\n");

	return p - buffer;
}

EXPORT_SYMBOL(fas216_info);
EXPORT_SYMBOL(fas216_init);
EXPORT_SYMBOL(fas216_queue_command);
EXPORT_SYMBOL(fas216_command);
EXPORT_SYMBOL(fas216_intr);
EXPORT_SYMBOL(fas216_release);
EXPORT_SYMBOL(fas216_eh_abort);
EXPORT_SYMBOL(fas216_eh_device_reset);
EXPORT_SYMBOL(fas216_eh_bus_reset);
EXPORT_SYMBOL(fas216_eh_host_reset);
EXPORT_SYMBOL(fas216_print_host);
EXPORT_SYMBOL(fas216_print_stats);
EXPORT_SYMBOL(fas216_print_device);

#ifdef MODULE
int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}
#endif

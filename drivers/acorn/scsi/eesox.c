/*
 *  linux/drivers/acorn/scsi/eesox.c
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This driver is based on experimentation.  Hence, it may have made
 *  assumptions about the particular card that I have available, and
 *  may not be reliable!
 *
 *  Changelog:
 *   01-10-1997	RMK		Created, READONLY version
 *   15-02-1998	RMK		READ/WRITE version
 *				added DMA support and hardware definitions
 *   14-03-1998	RMK		Updated DMA support
 *				Added terminator control
 *   15-04-1998	RMK		Only do PIO if FAS216 will allow it.
 *   27-06-1998	RMK		Changed asm/delay.h to linux/delay.h
 *   02-04-2000	RMK	0.0.3	Fixed NO_IRQ/NO_DMA problem, updated for new
 *				error handling code.
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
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/pgtable.h>

#include "../../scsi/sd.h"
#include "../../scsi/hosts.h"
#include "eesox.h"

/* Configuration */
#define EESOX_XTALFREQ		40
#define EESOX_ASYNC_PERIOD	200
#define EESOX_SYNC_DEPTH	7

/*
 * List of devices that the driver will recognise
 */
#define EESOXSCSI_LIST	{ MANU_EESOX, PROD_EESOX_SCSI2 }

#define EESOX_FAS216_OFFSET	0xc00
#define EESOX_FAS216_SHIFT	3

#define EESOX_STATUS		0xa00
#define EESOX_STAT_INTR		0x01
#define EESOX_STAT_DMA		0x02

#define EESOX_CONTROL		0xa00
#define EESOX_INTR_ENABLE	0x04
#define EESOX_TERM_ENABLE	0x02
#define EESOX_RESET		0x01

#define EESOX_DMA_OFFSET	0xe00

/*
 * Version
 */
#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	3

static struct expansion_card *ecs[MAX_ECARDS];

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("EESOX SCSI driver");
MODULE_PARM(term, "1-8i");
MODULE_PARM_DESC(term, "SCSI bus termination");

/*
 * Use term=0,1,0,0,0 to turn terminators on/off
 */
int term[MAX_ECARDS] = { 1, 1, 1, 1, 1, 1, 1, 1 };

/* Prototype: void eesoxscsi_irqenable(ec, irqnr)
 * Purpose  : Enable interrupts on EESOX SCSI card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
eesoxscsi_irqenable(struct expansion_card *ec, int irqnr)
{
	struct control *control = (struct control *)ec->irq_data;

	control->control |= EESOX_INTR_ENABLE;

	outb(control->control, control->io_port);
}

/* Prototype: void eesoxscsi_irqdisable(ec, irqnr)
 * Purpose  : Disable interrupts on EESOX SCSI card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
eesoxscsi_irqdisable(struct expansion_card *ec, int irqnr)
{
	struct control *control = (struct control *)ec->irq_data;

	control->control &= ~EESOX_INTR_ENABLE;

	outb(control->control, control->io_port);
}

static const expansioncard_ops_t eesoxscsi_ops = {
	eesoxscsi_irqenable,
	eesoxscsi_irqdisable,
	NULL,
	NULL,
	NULL,
	NULL
};

/* Prototype: void eesoxscsi_terminator_ctl(*host, on_off)
 * Purpose  : Turn the EESOX SCSI terminators on or off
 * Params   : host   - card to turn on/off
 *          : on_off - !0 to turn on, 0 to turn off
 */
static void
eesoxscsi_terminator_ctl(struct Scsi_Host *host, int on_off)
{
	EESOXScsi_Info *info = (EESOXScsi_Info *)host->hostdata;
	unsigned long flags;

	save_flags_cli(flags);
	if (on_off)
		info->control.control |= EESOX_TERM_ENABLE;
	else
		info->control.control &= ~EESOX_TERM_ENABLE;
	restore_flags(flags);

	outb(info->control.control, info->control.io_port);
}

/* Prototype: void eesoxscsi_intr(irq, *dev_id, *regs)
 * Purpose  : handle interrupts from EESOX SCSI card
 * Params   : irq    - interrupt number
 *	      dev_id - user-defined (Scsi_Host structure)
 *	      regs   - processor registers at interrupt
 */
static void
eesoxscsi_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct Scsi_Host *host = (struct Scsi_Host *)dev_id;

	fas216_intr(host);
}

/* Prototype: fasdmatype_t eesoxscsi_dma_setup(host, SCpnt, direction, min_type)
 * Purpose  : initialises DMA/PIO
 * Params   : host      - host
 *	      SCpnt     - command
 *	      direction - DMA on to/off of card
 *	      min_type  - minimum DMA support that we must have for this transfer
 * Returns  : type of transfer to be performed
 */
static fasdmatype_t
eesoxscsi_dma_setup(struct Scsi_Host *host, Scsi_Pointer *SCp,
		       fasdmadir_t direction, fasdmatype_t min_type)
{
	EESOXScsi_Info *info = (EESOXScsi_Info *)host->hostdata;
	int dmach = host->dma_channel;

	if (dmach != NO_DMA &&
	    (min_type == fasdma_real_all || SCp->this_residual >= 512)) {
		int bufs = SCp->buffers_residual;
		int pci_dir, dma_dir;

		if (bufs)
			memcpy(info->sg + 1, SCp->buffer + 1,
				sizeof(struct scatterlist) * bufs);
		info->sg[0].address = SCp->ptr;
		info->sg[0].length = SCp->this_residual;

		if (direction == DMA_OUT)
			pci_dir = PCI_DMA_TODEVICE,
			dma_dir = DMA_MODE_WRITE;
		else
			pci_dir = PCI_DMA_FROMDEVICE,
			dma_dir = DMA_MODE_READ;

		pci_map_sg(NULL, info->sg, bufs + 1, pci_dir);

		disable_dma(dmach);
		set_dma_sg(dmach, info->sg, bufs + 1);
		set_dma_mode(dmach, dma_dir);
		enable_dma(dmach);
		return fasdma_real_all;
	}
	/*
	 * We don't do DMA, we only do slow PIO
	 *
	 * Some day, we will do Pseudo DMA
	 */
	return fasdma_pseudo;
}

static void
eesoxscsi_dma_pseudo(struct Scsi_Host *host, Scsi_Pointer *SCp,
		     fasdmadir_t dir, int transfer_size)
{
	EESOXScsi_Info *info = (EESOXScsi_Info *)host->hostdata;
	unsigned int status;
	unsigned int length = SCp->this_residual;
	union {
		unsigned char *c;
		unsigned short *s;
		unsigned long *l;
	} buffer;

	buffer.c = SCp->ptr;

	status = inb(host->io_port + EESOX_STATUS);
	if (dir == DMA_IN) {
		while (length > 8) {
			if (status & EESOX_STAT_DMA) {
				unsigned long l1, l2;

				l1 = inw(info->dmaarea);
				l1 |= inw(info->dmaarea) << 16;
				l2 = inw(info->dmaarea);
				l2 |= inw(info->dmaarea) << 16;
				*buffer.l++ = l1;
				*buffer.l++ = l2;
				length -= 8;
			} else if (status & EESOX_STAT_INTR)
				goto end;
			status = inb(host->io_port + EESOX_STATUS);
		}

		while (length > 1) {
			if (status & EESOX_STAT_DMA) {
				*buffer.s++ = inw(info->dmaarea);
				length -= 2;
			} else if (status & EESOX_STAT_INTR)
				goto end;
			status = inb(host->io_port + EESOX_STATUS);
		}

		while (length > 0) {
			if (status & EESOX_STAT_DMA) {
				*buffer.c++ = inw(info->dmaarea);
				length -= 1;
			} else if (status & EESOX_STAT_INTR)
				goto end;
			status = inb(host->io_port + EESOX_STATUS);
		}
	} else {
		while (length > 8) {
			if (status & EESOX_STAT_DMA) {
				unsigned long l1, l2;

				l1 = *buffer.l++;
				l2 = *buffer.l++;

				outw(l1, info->dmaarea);
				outw(l1 >> 16, info->dmaarea);
				outw(l2, info->dmaarea);
				outw(l2 >> 16, info->dmaarea);
				length -= 8;
			} else if (status & EESOX_STAT_INTR)
				goto end;
			status = inb(host->io_port + EESOX_STATUS);
		}

		while (length > 1) {
			if (status & EESOX_STAT_DMA) {
				outw(*buffer.s++, info->dmaarea);
				length -= 2;
			} else if (status & EESOX_STAT_INTR)
				goto end;
			status = inb(host->io_port + EESOX_STATUS);
		}

		while (length > 0) {
			if (status & EESOX_STAT_DMA) {
				outw(*buffer.c++, info->dmaarea);
				length -= 1;
			} else if (status & EESOX_STAT_INTR)
				goto end;
			status = inb(host->io_port + EESOX_STATUS);
		}
	}
end:
}

/* Prototype: int eesoxscsi_dma_stop(host, SCpnt)
 * Purpose  : stops DMA/PIO
 * Params   : host  - host
 *	      SCpnt - command
 */
static void
eesoxscsi_dma_stop(struct Scsi_Host *host, Scsi_Pointer *SCp)
{
	if (host->dma_channel != NO_DMA)
		disable_dma(host->dma_channel);
}

/* Prototype: int eesoxscsi_detect(Scsi_Host_Template * tpnt)
 * Purpose  : initialises EESOX SCSI driver
 * Params   : tpnt - template for this SCSI adapter
 * Returns  : >0 if host found, 0 otherwise.
 */
int
eesoxscsi_detect(Scsi_Host_Template *tpnt)
{
	static const card_ids eesoxscsi_cids[] =
			{ EESOXSCSI_LIST, { 0xffff, 0xffff} };
	int count = 0;
	struct Scsi_Host *host;
  
	tpnt->proc_name = "eesox";
	memset(ecs, 0, sizeof (ecs));

	ecard_startfind();

	while(1) {
	    	EESOXScsi_Info *info;

		ecs[count] = ecard_find(0, eesoxscsi_cids);
		if (!ecs[count])
			break;

		ecard_claim(ecs[count]);

		host = scsi_register(tpnt, sizeof (EESOXScsi_Info));
		if (!host) {
			ecard_release(ecs[count]);
			break;
		}

		host->io_port = ecard_address(ecs[count], ECARD_IOC, ECARD_FAST);
		host->irq = ecs[count]->irq;
		host->dma_channel = ecs[count]->dma;
		info = (EESOXScsi_Info *)host->hostdata;

		info->control.io_port = host->io_port + EESOX_CONTROL;
		info->control.control = term[count] ? EESOX_TERM_ENABLE : 0;
		outb(info->control.control, info->control.io_port);

		ecs[count]->irqaddr = (unsigned char *)
			    ioaddr(host->io_port + EESOX_STATUS);
		ecs[count]->irqmask = EESOX_STAT_INTR;
		ecs[count]->irq_data = &info->control;
		ecs[count]->ops = (expansioncard_ops_t *)&eesoxscsi_ops;

		info->info.scsi.io_port		= host->io_port + EESOX_FAS216_OFFSET;
		info->info.scsi.io_shift	= EESOX_FAS216_SHIFT;
		info->info.scsi.irq		= host->irq;
		info->info.ifcfg.clockrate	= EESOX_XTALFREQ;
		info->info.ifcfg.select_timeout	= 255;
		info->info.ifcfg.asyncperiod	= EESOX_ASYNC_PERIOD;
		info->info.ifcfg.sync_max_depth	= EESOX_SYNC_DEPTH;
		info->info.ifcfg.cntl3		= CNTL3_BS8 | CNTL3_FASTSCSI | CNTL3_FASTCLK;
		info->info.ifcfg.disconnect_ok	= 1;
		info->info.ifcfg.wide_max_size	= 0;
		info->info.dma.setup		= eesoxscsi_dma_setup;
		info->info.dma.pseudo		= eesoxscsi_dma_pseudo;
		info->info.dma.stop		= eesoxscsi_dma_stop;
		info->dmaarea			= host->io_port + EESOX_DMA_OFFSET;

		request_region(host->io_port + EESOX_FAS216_OFFSET,
				16 << EESOX_FAS216_SHIFT, "eesox2-fas");

		if (host->irq != NO_IRQ &&
		    request_irq(host->irq, eesoxscsi_intr,
				SA_INTERRUPT, "eesox", host)) {
			printk("scsi%d: IRQ%d not free, interrupts disabled\n",
			       host->host_no, host->irq);
			host->irq = NO_IRQ;
		}

		if (host->dma_channel != NO_DMA &&
		    request_dma(host->dma_channel, "eesox")) {
			printk("scsi%d: DMA%d not free, DMA disabled\n",
			       host->host_no, host->dma_channel);
			host->dma_channel = NO_DMA;
		}

		fas216_init(host);
		++count;
	}
	return count;
}

/* Prototype: int eesoxscsi_release(struct Scsi_Host * host)
 * Purpose  : releases all resources used by this adapter
 * Params   : host - driver host structure to return info for.
 */
int eesoxscsi_release(struct Scsi_Host *host)
{
	int i;

	fas216_release(host);

	if (host->irq != NO_IRQ)
		free_irq(host->irq, host);
	if (host->dma_channel != NO_DMA)
		free_dma(host->dma_channel);
	release_region(host->io_port + EESOX_FAS216_OFFSET, 16 << EESOX_FAS216_SHIFT);

	for (i = 0; i < MAX_ECARDS; i++)
		if (ecs[i] &&
		    host->io_port == ecard_address(ecs[i], ECARD_IOC, ECARD_FAST))
			ecard_release(ecs[i]);
	return 0;
}

/* Prototype: const char *eesoxscsi_info(struct Scsi_Host * host)
 * Purpose  : returns a descriptive string about this interface,
 * Params   : host - driver host structure to return info for.
 * Returns  : pointer to a static buffer containing null terminated string.
 */
const char *eesoxscsi_info(struct Scsi_Host *host)
{
	EESOXScsi_Info *info = (EESOXScsi_Info *)host->hostdata;
	static char string[100], *p;

	p = string;
	p += sprintf(p, "%s ", host->hostt->name);
	p += fas216_info(&info->info, p);
	p += sprintf(p, "v%d.%d.%d terminators o%s",
		     VER_MAJOR, VER_MINOR, VER_PATCH,
		     info->control.control & EESOX_TERM_ENABLE ? "n" : "ff");

	return string;
}

/* Prototype: int eesoxscsi_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
 * Purpose  : Set a driver specific function
 * Params   : host   - host to setup
 *          : buffer - buffer containing string describing operation
 *          : length - length of string
 * Returns  : -EINVAL, or 0
 */
static int
eesoxscsi_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
{
	int ret = length;

	if (length >= 9 && strncmp(buffer, "EESOXSCSI", 9) == 0) {
		buffer += 9;
		length -= 9;

		if (length >= 5 && strncmp(buffer, "term=", 5) == 0) {
			if (buffer[5] == '1')
				eesoxscsi_terminator_ctl(host, 1);
			else if (buffer[5] == '0')
				eesoxscsi_terminator_ctl(host, 0);
			else
				ret = -EINVAL;
		} else
			ret = -EINVAL;
	} else
		ret = -EINVAL;

	return ret;
}

/* Prototype: int eesoxscsi_proc_info(char *buffer, char **start, off_t offset,
 *				      int length, int host_no, int inout)
 * Purpose  : Return information about the driver to a user process accessing
 *	      the /proc filesystem.
 * Params   : buffer - a buffer to write information to
 *	      start  - a pointer into this buffer set by this routine to the start
 *		       of the required information.
 *	      offset - offset into information that we have read upto.
 *	      length - length of buffer
 *	      host_no - host number to return information for
 *	      inout  - 0 for reading, 1 for writing.
 * Returns  : length of data written to buffer.
 */
int eesoxscsi_proc_info(char *buffer, char **start, off_t offset,
			    int length, int host_no, int inout)
{
	int pos, begin;
	struct Scsi_Host *host = scsi_hostlist;
	EESOXScsi_Info *info;
	Scsi_Device *scd;

	while (host) {
		if (host->host_no == host_no)
			break;
		host = host->next;
	}
	if (!host)
		return 0;

	if (inout == 1)
		return eesoxscsi_set_proc_info(host, buffer, length);

	info = (EESOXScsi_Info *)host->hostdata;

	begin = 0;
	pos = sprintf(buffer,
			"EESOX SCSI driver version %d.%d.%d\n",
			VER_MAJOR, VER_MINOR, VER_PATCH);
	pos += fas216_print_host(&info->info, buffer + pos);
	pos += sprintf(buffer + pos, "Term    : o%s\n",
			info->control.control & EESOX_TERM_ENABLE ? "n" : "ff");

	pos += fas216_print_stats(&info->info, buffer + pos);

	pos += sprintf (buffer+pos, "\nAttached devices:\n");

	for (scd = host->host_queue; scd; scd = scd->next) {
		int len;

		proc_print_scsidevice (scd, buffer, &len, pos);
		pos += len;
		pos += sprintf (buffer+pos, "Extensions: ");
		if (scd->tagged_supported)
			pos += sprintf (buffer+pos, "TAG %sabled [%d] ",
					scd->tagged_queue ? "en" : "dis",
					scd->current_tag);
		pos += sprintf (buffer+pos, "\n");

		if (pos + begin < offset) {
			begin += pos;
			pos = 0;
		}
	}
	*start = buffer + (offset - begin);
	pos -= offset - begin;
	if (pos > length)
		pos = length;

	return pos;
}

#ifdef MODULE
Scsi_Host_Template driver_template = EESOXSCSI;

#include "../../scsi/scsi_module.c"
#endif

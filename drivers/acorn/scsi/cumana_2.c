/*
 *  linux/drivers/acorn/scsi/cumana_2.c
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   30-08-1997	RMK	0.0.0	Created, READONLY version.
 *   22-01-1998	RMK	0.0.1	Updated to 2.1.80.
 *   15-04-1998	RMK	0.0.1	Only do PIO if FAS216 will allow it.
 *   02-05-1998	RMK	0.0.2	Updated & added DMA support.
 *   27-06-1998	RMK		Changed asm/delay.h to linux/delay.h
 *   18-08-1998	RMK	0.0.3	Fixed synchronous transfer depth.
 *   02-04-2000	RMK	0.0.4	Updated for new error handling code.
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
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

#include "../../scsi/sd.h"
#include "../../scsi/hosts.h"
#include "fas216.h"

#include <scsi/scsicam.h>

/* Configuration */
#define CUMANASCSI2_XTALFREQ		40
#define CUMANASCSI2_ASYNC_PERIOD	200
#define CUMANASCSI2_SYNC_DEPTH		7

/*
 * List of devices that the driver will recognise
 */
#define CUMANASCSI2_LIST		{ MANU_CUMANA, PROD_CUMANA_SCSI_2 }

#define CUMANASCSI2_STATUS		(0)
#define STATUS_INT			(1 << 0)
#define STATUS_DRQ			(1 << 1)
#define STATUS_LATCHED			(1 << 3)

#define CUMANASCSI2_ALATCH		(5)
#define ALATCH_ENA_INT			(3)
#define ALATCH_DIS_INT			(2)
#define ALATCH_ENA_TERM			(5)
#define ALATCH_DIS_TERM			(4)
#define ALATCH_ENA_BIT32		(11)
#define ALATCH_DIS_BIT32		(10)
#define ALATCH_ENA_DMA			(13)
#define ALATCH_DIS_DMA			(12)
#define ALATCH_DMA_OUT			(15)
#define ALATCH_DMA_IN			(14)

#define CUMANASCSI2_PSEUDODMA		(0x80)

#define CUMANASCSI2_FAS216_OFFSET	(0xc0)
#define CUMANASCSI2_FAS216_SHIFT	0

/*
 * Version
 */
#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	4

static struct expansion_card *ecs[MAX_ECARDS];

/*
 * Use term=0,1,0,0,0 to turn terminators on/off
 */
static int term[MAX_ECARDS] = { 1, 1, 1, 1, 1, 1, 1, 1 };

#define NR_SG	256

typedef struct {
	FAS216_Info info;

	/* other info... */
	unsigned int	status;		/* card status register	*/
	unsigned int	alatch;		/* Control register	*/
	unsigned int	terms;		/* Terminator state	*/
	unsigned int	dmaarea;	/* Pseudo DMA area	*/
	struct scatterlist sg[NR_SG];	/* Scatter DMA list	*/
} CumanaScsi2_Info;

#define CSTATUS_IRQ	(1 << 0)
#define CSTATUS_DRQ	(1 << 1)

/* Prototype: void cumanascsi_2_irqenable(ec, irqnr)
 * Purpose  : Enable interrupts on Cumana SCSI 2 card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
cumanascsi_2_irqenable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(ALATCH_ENA_INT, port);
}

/* Prototype: void cumanascsi_2_irqdisable(ec, irqnr)
 * Purpose  : Disable interrupts on Cumana SCSI 2 card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
cumanascsi_2_irqdisable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(ALATCH_DIS_INT, port);
}

static const expansioncard_ops_t cumanascsi_2_ops = {
	cumanascsi_2_irqenable,
	cumanascsi_2_irqdisable,
	NULL,
	NULL,
	NULL,
	NULL
};

/* Prototype: void cumanascsi_2_terminator_ctl(host, on_off)
 * Purpose  : Turn the Cumana SCSI 2 terminators on or off
 * Params   : host   - card to turn on/off
 *          : on_off - !0 to turn on, 0 to turn off
 */
static void
cumanascsi_2_terminator_ctl(struct Scsi_Host *host, int on_off)
{
	CumanaScsi2_Info *info = (CumanaScsi2_Info *)host->hostdata;

	if (on_off) {
		info->terms = 1;
		outb (ALATCH_ENA_TERM, info->alatch);
	} else {
		info->terms = 0;
		outb (ALATCH_DIS_TERM, info->alatch);
	}
}

/* Prototype: void cumanascsi_2_intr(irq, *dev_id, *regs)
 * Purpose  : handle interrupts from Cumana SCSI 2 card
 * Params   : irq    - interrupt number
 *	      dev_id - user-defined (Scsi_Host structure)
 *	      regs   - processor registers at interrupt
 */
static void
cumanascsi_2_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct Scsi_Host *host = (struct Scsi_Host *)dev_id;

	fas216_intr(host);
}

/* Prototype: fasdmatype_t cumanascsi_2_dma_setup(host, SCpnt, direction, min_type)
 * Purpose  : initialises DMA/PIO
 * Params   : host      - host
 *	      SCpnt     - command
 *	      direction - DMA on to/off of card
 *	      min_type  - minimum DMA support that we must have for this transfer
 * Returns  : type of transfer to be performed
 */
static fasdmatype_t
cumanascsi_2_dma_setup(struct Scsi_Host *host, Scsi_Pointer *SCp,
		       fasdmadir_t direction, fasdmatype_t min_type)
{
	CumanaScsi2_Info *info = (CumanaScsi2_Info *)host->hostdata;
	int dmach = host->dma_channel;

	outb(ALATCH_DIS_DMA, info->alatch);

	if (dmach != NO_DMA &&
	    (min_type == fasdma_real_all || SCp->this_residual >= 512)) {
		int bufs = SCp->buffers_residual;
		int pci_dir, dma_dir, alatch_dir;

		if (bufs)
			memcpy(info->sg + 1, SCp->buffer + 1,
				sizeof(struct scatterlist) * bufs);
		info->sg[0].address = SCp->ptr;
		info->sg[0].page    = NULL;
		info->sg[0].length  = SCp->this_residual;

		if (direction == DMA_OUT)
			pci_dir = PCI_DMA_TODEVICE,
			dma_dir = DMA_MODE_WRITE,
			alatch_dir = ALATCH_DMA_OUT;
		else
			pci_dir = PCI_DMA_FROMDEVICE,
			dma_dir = DMA_MODE_READ,
			alatch_dir = ALATCH_DMA_IN;

		pci_map_sg(NULL, info->sg, bufs + 1, pci_dir);

		disable_dma(dmach);
		set_dma_sg(dmach, info->sg, bufs + 1);
		outb(alatch_dir, info->alatch);
		set_dma_mode(dmach, dma_dir);
		enable_dma(dmach);
		outb(ALATCH_ENA_DMA, info->alatch);
		outb(ALATCH_DIS_BIT32, info->alatch);
		return fasdma_real_all;
	}

	/*
	 * If we're not doing DMA,
	 *  we'll do pseudo DMA
	 */
	return fasdma_pio;
}

/*
 * Prototype: void cumanascsi_2_dma_pseudo(host, SCpnt, direction, transfer)
 * Purpose  : handles pseudo DMA
 * Params   : host      - host
 *	      SCpnt     - command
 *	      direction - DMA on to/off of card
 *	      transfer  - minimum number of bytes we expect to transfer
 */
static void
cumanascsi_2_dma_pseudo(struct Scsi_Host *host, Scsi_Pointer *SCp,
			fasdmadir_t direction, int transfer)
{
	CumanaScsi2_Info *info = (CumanaScsi2_Info *)host->hostdata;
	unsigned int length;
	unsigned char *addr;

	length = SCp->this_residual;
	addr = SCp->ptr;

	if (direction == DMA_OUT)
#if 0
		while (length > 1) {
			unsigned long word;
			unsigned int status = inb(info->status);

			if (status & STATUS_INT)
				goto end;

			if (!(status & STATUS_DRQ))
				continue;

			word = *addr | *(addr + 1) << 8;
			outw (info->dmaarea);
			addr += 2;
			length -= 2;
		}
#else
		printk ("PSEUDO_OUT???\n");
#endif
	else {
		if (transfer && (transfer & 255)) {
			while (length >= 256) {
				unsigned int status = inb(info->status);

				if (status & STATUS_INT)
					goto end;
	    
				if (!(status & STATUS_DRQ))
					continue;

				insw(info->dmaarea, addr, 256 >> 1);
				addr += 256;
				length -= 256;
			}
		}

		while (length > 0) {
			unsigned long word;
			unsigned int status = inb(info->status);

			if (status & STATUS_INT)
				goto end;

			if (!(status & STATUS_DRQ))
				continue;

			word = inw (info->dmaarea);
			*addr++ = word;
			if (--length > 0) {
				*addr++ = word >> 8;
				length --;
			}
		}
	}

end:
}

/* Prototype: int cumanascsi_2_dma_stop(host, SCpnt)
 * Purpose  : stops DMA/PIO
 * Params   : host  - host
 *	      SCpnt - command
 */
static void
cumanascsi_2_dma_stop(struct Scsi_Host *host, Scsi_Pointer *SCp)
{
	CumanaScsi2_Info *info = (CumanaScsi2_Info *)host->hostdata;
	if (host->dma_channel != NO_DMA) {
		outb(ALATCH_DIS_DMA, info->alatch);
		disable_dma(host->dma_channel);
	}
}

/* Prototype: int cumanascsi_2_detect(Scsi_Host_Template * tpnt)
 * Purpose  : initialises Cumana SCSI 2 driver
 * Params   : tpnt - template for this SCSI adapter
 * Returns  : >0 if host found, 0 otherwise.
 */
int
cumanascsi_2_detect(Scsi_Host_Template *tpnt)
{
	static const card_ids cumanascsi_2_cids[] =
			{ CUMANASCSI2_LIST, { 0xffff, 0xffff} };
	int count = 0;
	struct Scsi_Host *host;
  
	tpnt->proc_name = "cumanascs2";
	memset(ecs, 0, sizeof (ecs));

	ecard_startfind();

	while (1) {
	    	CumanaScsi2_Info *info;

		ecs[count] = ecard_find(0, cumanascsi_2_cids);
		if (!ecs[count])
			break;

		ecard_claim(ecs[count]);

		host = scsi_register(tpnt, sizeof (CumanaScsi2_Info));
		if (!host) {
			ecard_release(ecs[count]);
			break;
		}

		host->io_port = ecard_address(ecs[count], ECARD_MEMC, 0);
		host->irq = ecs[count]->irq;
		host->dma_channel = ecs[count]->dma;
		info = (CumanaScsi2_Info *)host->hostdata;

		info->terms			= term[count] ? 1 : 0;
		cumanascsi_2_terminator_ctl(host, info->terms);

		info->info.scsi.io_port		= host->io_port + CUMANASCSI2_FAS216_OFFSET;
		info->info.scsi.io_shift	= CUMANASCSI2_FAS216_SHIFT;
		info->info.scsi.irq		= host->irq;
		info->info.ifcfg.clockrate	= CUMANASCSI2_XTALFREQ;
		info->info.ifcfg.select_timeout	= 255;
		info->info.ifcfg.asyncperiod	= CUMANASCSI2_ASYNC_PERIOD;
		info->info.ifcfg.sync_max_depth	= CUMANASCSI2_SYNC_DEPTH;
		info->info.ifcfg.cntl3		= CNTL3_BS8 | CNTL3_FASTSCSI | CNTL3_FASTCLK;
		info->info.ifcfg.disconnect_ok	= 1;
		info->info.ifcfg.wide_max_size	= 0;
		info->info.dma.setup		= cumanascsi_2_dma_setup;
		info->info.dma.pseudo		= cumanascsi_2_dma_pseudo;
		info->info.dma.stop		= cumanascsi_2_dma_stop;
		info->dmaarea			= host->io_port + CUMANASCSI2_PSEUDODMA;
		info->status			= host->io_port + CUMANASCSI2_STATUS;
		info->alatch			= host->io_port + CUMANASCSI2_ALATCH;

		ecs[count]->irqaddr	= (unsigned char *)ioaddr(info->status);
		ecs[count]->irqmask	= STATUS_INT;
		ecs[count]->irq_data	= (void *)info->alatch;
		ecs[count]->ops		= (expansioncard_ops_t *)&cumanascsi_2_ops;

		request_region(host->io_port + CUMANASCSI2_FAS216_OFFSET,
			       16 << CUMANASCSI2_FAS216_SHIFT, "cumanascsi2-fas");

		if (host->irq != NO_IRQ &&
		    request_irq(host->irq, cumanascsi_2_intr,
				SA_INTERRUPT, "cumanascsi2", host)) {
			printk("scsi%d: IRQ%d not free, interrupts disabled\n",
			       host->host_no, host->irq);
			host->irq = NO_IRQ;
			info->info.scsi.irq = NO_IRQ;
		}

		if (host->dma_channel != NO_DMA &&
		    request_dma(host->dma_channel, "cumanascsi2")) {
			printk("scsi%d: DMA%d not free, DMA disabled\n",
			       host->host_no, host->dma_channel);
			host->dma_channel = NO_DMA;
		}

		fas216_init(host);
		++count;
	}
	return count;
}

/* Prototype: int cumanascsi_2_release(struct Scsi_Host * host)
 * Purpose  : releases all resources used by this adapter
 * Params   : host - driver host structure to return info for.
 */
int cumanascsi_2_release(struct Scsi_Host *host)
{
	int i;

	fas216_release(host);

	if (host->irq != NO_IRQ)
		free_irq(host->irq, host);
	if (host->dma_channel != NO_DMA)
		free_dma(host->dma_channel);
	release_region(host->io_port + CUMANASCSI2_FAS216_OFFSET,
		       16 << CUMANASCSI2_FAS216_SHIFT);

	for (i = 0; i < MAX_ECARDS; i++)
		if (ecs[i] && host->io_port == ecard_address (ecs[i], ECARD_MEMC, 0))
			ecard_release (ecs[i]);
	return 0;
}

/* Prototype: const char *cumanascsi_2_info(struct Scsi_Host * host)
 * Purpose  : returns a descriptive string about this interface,
 * Params   : host - driver host structure to return info for.
 * Returns  : pointer to a static buffer containing null terminated string.
 */
const char *cumanascsi_2_info(struct Scsi_Host *host)
{
	CumanaScsi2_Info *info = (CumanaScsi2_Info *)host->hostdata;
	static char string[100], *p;

	p = string;
	p += sprintf(p, "%s ", host->hostt->name);
	p += fas216_info(&info->info, p);
	p += sprintf(p, "v%d.%d.%d terminators o%s",
		     VER_MAJOR, VER_MINOR, VER_PATCH,
		     info->terms ? "n" : "ff");

	return string;
}

/* Prototype: int cumanascsi_2_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
 * Purpose  : Set a driver specific function
 * Params   : host   - host to setup
 *          : buffer - buffer containing string describing operation
 *          : length - length of string
 * Returns  : -EINVAL, or 0
 */
static int
cumanascsi_2_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
{
	int ret = length;

	if (length >= 11 && strcmp(buffer, "CUMANASCSI2") == 0) {
		buffer += 11;
		length -= 11;

		if (length >= 5 && strncmp(buffer, "term=", 5) == 0) {
			if (buffer[5] == '1')
				cumanascsi_2_terminator_ctl(host, 1);
			else if (buffer[5] == '0')
				cumanascsi_2_terminator_ctl(host, 0);
			else
				ret = -EINVAL;
		} else
			ret = -EINVAL;
	} else
		ret = -EINVAL;

	return ret;
}

/* Prototype: int cumanascsi_2_proc_info(char *buffer, char **start, off_t offset,
 *					 int length, int host_no, int inout)
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
int cumanascsi_2_proc_info (char *buffer, char **start, off_t offset,
			    int length, int host_no, int inout)
{
	int pos, begin;
	struct Scsi_Host *host = scsi_hostlist;
	CumanaScsi2_Info *info;
	Scsi_Device *scd;

	while (host) {
		if (host->host_no == host_no)
			break;
		host = host->next;
	}
	if (!host)
		return 0;

	if (inout == 1)
		return cumanascsi_2_set_proc_info(host, buffer, length);

	info = (CumanaScsi2_Info *)host->hostdata;

	begin = 0;
	pos = sprintf(buffer,
			"Cumana SCSI II driver version %d.%d.%d\n",
			VER_MAJOR, VER_MINOR, VER_PATCH);

	pos += fas216_print_host(&info->info, buffer + pos);
	pos += sprintf(buffer + pos, "Term    : o%s\n",
			info->terms ? "n" : "ff");

	pos += fas216_print_stats(&info->info, buffer + pos);

	pos += sprintf(buffer+pos, "\nAttached devices:\n");

	for (scd = host->host_queue; scd; scd = scd->next) {
		int len;

		proc_print_scsidevice(scd, buffer, &len, pos);
		pos += len;
		pos += sprintf(buffer+pos, "Extensions: ");
		if (scd->tagged_supported)
			pos += sprintf(buffer+pos, "TAG %sabled [%d] ",
				       scd->tagged_queue ? "en" : "dis",
				       scd->current_tag);
		pos += sprintf(buffer+pos, "\n");

		if (pos + begin < offset) {
			begin += pos;
			pos = 0;
		}
		if (pos + begin > offset + length)
			break;
	}

	*start = buffer + (offset - begin);
	pos -= offset - begin;
	if (pos > length)
		pos = length;

	return pos;
}

static Scsi_Host_Template cumanascsi2_template = {
	module:				THIS_MODULE,
	proc_info:			cumanascsi_2_proc_info,
	name:				"Cumana SCSI II",
	detect:				cumanascsi_2_detect,
	release:			cumanascsi_2_release,
	info:				cumanascsi_2_info,
	bios_param:			scsicam_bios_param,
	can_queue:			1,
	this_id:			7,
	sg_tablesize:			SG_ALL,
	cmd_per_lun:			1,
	use_clustering:			DISABLE_CLUSTERING,
	command:			fas216_command,
	queuecommand:			fas216_queue_command,
	eh_host_reset_handler:		fas216_eh_host_reset,
	eh_bus_reset_handler:		fas216_eh_bus_reset,
	eh_device_reset_handler:	fas216_eh_device_reset,
	eh_abort_handler:		fas216_eh_abort,
	use_new_eh_code:		1
};

static int __init cumanascsi2_init(void)
{
	scsi_register_module(MODULE_SCSI_HA, &cumanascsi2_template);
	if (cumanascsi2_template.present)
		return 0;

	scsi_unregister_module(MODULE_SCSI_HA, &cumanascsi2_template);
	return -ENODEV;
}

static void __exit cumanascsi2_exit(void)
{
	scsi_unregister_module(MODULE_SCSI_HA, &cumanascsi2_template);
}

module_init(cumanascsi2_init);
module_exit(cumanascsi2_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Cumana SCSI-2 driver for Acorn machines");
MODULE_PARM(term, "1-8i");
MODULE_PARM_DESC(term, "SCSI bus termination");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;

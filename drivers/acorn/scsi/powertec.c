/*
 *  linux/drivers/acorn/scsi/powertec.c
 *
 *  Copyright (C) 1997-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"
#include "fas216.h"
#include "scsi.h"

#include <scsi/scsicam.h>

#define POWERTEC_FAS216_OFFSET	0xc00
#define POWERTEC_FAS216_SHIFT	4
#define POWERTEC_FAS216_SIZE	(16 << POWERTEC_FAS216_SHIFT)

#define POWERTEC_INTR_STATUS	0x800
#define POWERTEC_INTR_BIT	0x80

#define POWERTEC_RESET_CONTROL	0x406
#define POWERTEC_RESET_BIT	1

#define POWERTEC_TERM_CONTROL	0x806
#define POWERTEC_TERM_ENABLE	1

#define POWERTEC_INTR_CONTROL	0x407
#define POWERTEC_INTR_ENABLE	1
#define POWERTEC_INTR_DISABLE	0

#define VERSION	"1.00 (13/11/2002 2.5.47)"

/*
 * Use term=0,1,0,0,0 to turn terminators on/off.
 * One entry per slot.
 */
static int term[MAX_ECARDS] = { 1, 1, 1, 1, 1, 1, 1, 1 };

#define NR_SG	256

struct powertec_info {
	FAS216_Info	info;
	unsigned int	term_port;
	unsigned int	term_ctl;
	struct scatterlist sg[NR_SG];
};

/* Prototype: void powertecscsi_irqenable(ec, irqnr)
 * Purpose  : Enable interrupts on Powertec SCSI card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
powertecscsi_irqenable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(POWERTEC_INTR_ENABLE, port);
}

/* Prototype: void powertecscsi_irqdisable(ec, irqnr)
 * Purpose  : Disable interrupts on Powertec SCSI card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
powertecscsi_irqdisable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(POWERTEC_INTR_DISABLE, port);
}

static const expansioncard_ops_t powertecscsi_ops = {
	.irqenable	= powertecscsi_irqenable,
	.irqdisable	= powertecscsi_irqdisable,
};

/* Prototype: void powertecscsi_terminator_ctl(host, on_off)
 * Purpose  : Turn the Powertec SCSI terminators on or off
 * Params   : host   - card to turn on/off
 *          : on_off - !0 to turn on, 0 to turn off
 */
static void
powertecscsi_terminator_ctl(struct Scsi_Host *host, int on_off)
{
	struct powertec_info *info = (struct powertec_info *)host->hostdata;

	info->term_ctl = on_off ? POWERTEC_TERM_ENABLE : 0;
	outb(info->term_ctl, info->term_port);
}

/* Prototype: void powertecscsi_intr(irq, *dev_id, *regs)
 * Purpose  : handle interrupts from Powertec SCSI card
 * Params   : irq    - interrupt number
 *	      dev_id - user-defined (Scsi_Host structure)
 *	      regs   - processor registers at interrupt
 */
static void
powertecscsi_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct Scsi_Host *host = (struct Scsi_Host *)dev_id;

	fas216_intr(host);
}

/* Prototype: fasdmatype_t powertecscsi_dma_setup(host, SCpnt, direction, min_type)
 * Purpose  : initialises DMA/PIO
 * Params   : host      - host
 *	      SCpnt     - command
 *	      direction - DMA on to/off of card
 *	      min_type  - minimum DMA support that we must have for this transfer
 * Returns  : type of transfer to be performed
 */
static fasdmatype_t
powertecscsi_dma_setup(struct Scsi_Host *host, Scsi_Pointer *SCp,
		       fasdmadir_t direction, fasdmatype_t min_type)
{
	struct powertec_info *info = (struct powertec_info *)host->hostdata;
	struct device *dev = scsi_get_device(host);
	int dmach = host->dma_channel;

	if (dmach != NO_DMA &&
	    (min_type == fasdma_real_all || SCp->this_residual >= 512)) {
		int bufs, map_dir, dma_dir;

		bufs = copy_SCp_to_sg(&info->sg[0], SCp, NR_SG);

		if (direction == DMA_OUT)
			map_dir = DMA_TO_DEVICE,
			dma_dir = DMA_MODE_WRITE;
		else
			map_dir = DMA_FROM_DEVICE,
			dma_dir = DMA_MODE_READ;

		dma_map_sg(dev, info->sg, bufs + 1, map_dir);

		disable_dma(dmach);
		set_dma_sg(dmach, info->sg, bufs + 1);
		set_dma_mode(dmach, dma_dir);
		enable_dma(dmach);
		return fasdma_real_all;
	}

	/*
	 * If we're not doing DMA,
	 *  we'll do slow PIO
	 */
	return fasdma_pio;
}

/* Prototype: int powertecscsi_dma_stop(host, SCpnt)
 * Purpose  : stops DMA/PIO
 * Params   : host  - host
 *	      SCpnt - command
 */
static void
powertecscsi_dma_stop(struct Scsi_Host *host, Scsi_Pointer *SCp)
{
	if (host->dma_channel != NO_DMA)
		disable_dma(host->dma_channel);
}

/* Prototype: const char *powertecscsi_info(struct Scsi_Host * host)
 * Purpose  : returns a descriptive string about this interface,
 * Params   : host - driver host structure to return info for.
 * Returns  : pointer to a static buffer containing null terminated string.
 */
const char *powertecscsi_info(struct Scsi_Host *host)
{
	struct powertec_info *info = (struct powertec_info *)host->hostdata;
	static char string[100], *p;

	p = string;
	p += sprintf(p, "%s ", host->hostt->name);
	p += fas216_info(&info->info, p);
	p += sprintf(p, "v%s terminators o%s",
		     VERSION, info->term_ctl ? "n" : "ff");

	return string;
}

/* Prototype: int powertecscsi_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
 * Purpose  : Set a driver specific function
 * Params   : host   - host to setup
 *          : buffer - buffer containing string describing operation
 *          : length - length of string
 * Returns  : -EINVAL, or 0
 */
static int
powertecscsi_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
{
	int ret = length;

	if (length >= 12 && strncmp(buffer, "POWERTECSCSI", 12) == 0) {
		buffer += 12;
		length -= 12;

		if (length >= 5 && strncmp(buffer, "term=", 5) == 0) {
			if (buffer[5] == '1')
				powertecscsi_terminator_ctl(host, 1);
			else if (buffer[5] == '0')
				powertecscsi_terminator_ctl(host, 0);
			else
				ret = -EINVAL;
		} else
			ret = -EINVAL;
	} else
		ret = -EINVAL;

	return ret;
}

/* Prototype: int powertecscsi_proc_info(char *buffer, char **start, off_t offset,
 *					int length, int host_no, int inout)
 * Purpose  : Return information about the driver to a user process accessing
 *	      the /proc filesystem.
 * Params   : buffer  - a buffer to write information to
 *	      start   - a pointer into this buffer set by this routine to the start
 *		        of the required information.
 *	      offset  - offset into information that we have read upto.
 *	      length  - length of buffer
 *	      host_no - host number to return information for
 *	      inout   - 0 for reading, 1 for writing.
 * Returns  : length of data written to buffer.
 */
int powertecscsi_proc_info(char *buffer, char **start, off_t offset,
			    int length, int host_no, int inout)
{
	int pos, begin;
	struct Scsi_Host *host;
	struct powertec_info *info;
	Scsi_Device *scd;

	host = scsi_host_hn_get(host_no);
	if (!host)
		return 0;

	if (inout == 1)
		return powertecscsi_set_proc_info(host, buffer, length);

	info = (struct powertec_info *)host->hostdata;

	begin = 0;
	pos = sprintf(buffer, "PowerTec SCSI driver v%s\n", VERSION);

	pos += fas216_print_host(&info->info, buffer + pos);
	pos += sprintf(buffer + pos, "Term    : o%s\n",
			info->term_ctl ? "n" : "ff");

	pos += fas216_print_stats(&info->info, buffer + pos);

	pos += sprintf(buffer+pos, "\nAttached devices:\n");

	list_for_each_entry(scd, &host->my_devices, siblings) {
		pos += fas216_print_device(&info->info, scd, buffer + pos);

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

static Scsi_Host_Template powertecscsi_template = {
	.module				= THIS_MODULE,
	.proc_info			= powertecscsi_proc_info,
	.name				= "PowerTec SCSI",
	.info				= powertecscsi_info,
	.command			= fas216_command,
	.queuecommand			= fas216_queue_command,
	.eh_host_reset_handler		= fas216_eh_host_reset,
	.eh_bus_reset_handler		= fas216_eh_bus_reset,
	.eh_device_reset_handler	= fas216_eh_device_reset,
	.eh_abort_handler		= fas216_eh_abort,

	.can_queue			= 1,
	.this_id			= 7,
	.sg_tablesize			= SG_ALL,
	.cmd_per_lun			= 1,
	.use_clustering			= ENABLE_CLUSTERING,
	.proc_name			= "powertec",
};

static int __devinit
powertecscsi_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct Scsi_Host *host;
    	struct powertec_info *info;
    	int ret = -ENOMEM;

	host = scsi_register(&powertecscsi_template,
			     sizeof (struct powertec_info));
	if (!host)
		goto out;

	host->io_port	  = ecard_address(ec, ECARD_IOC, ECARD_FAST);
	host->irq	  = ec->irq;
	host->dma_channel = ec->dma;

	if (!request_region(host->io_port + POWERTEC_FAS216_OFFSET,
			    POWERTEC_FAS216_SIZE, "powertec2-fas")) {
		ret = -EBUSY;
		goto out_free;
	}

	ec->irqaddr	= (unsigned char *)
			    ioaddr(host->io_port + POWERTEC_INTR_STATUS);
	ec->irqmask	= POWERTEC_INTR_BIT;
	ec->irq_data	= (void *)(host->io_port + POWERTEC_INTR_CONTROL);
	ec->ops		= (expansioncard_ops_t *)&powertecscsi_ops;

	ecard_set_drvdata(ec, host);

	info = (struct powertec_info *)host->hostdata;
	info->term_port = host->io_port + POWERTEC_TERM_CONTROL;
	powertecscsi_terminator_ctl(host, term[ec->slot_no]);

	info->info.scsi.io_port		= host->io_port + POWERTEC_FAS216_OFFSET;
	info->info.scsi.io_shift	= POWERTEC_FAS216_SHIFT;
	info->info.scsi.irq		= host->irq;
	info->info.ifcfg.clockrate	= 40; /* MHz */
	info->info.ifcfg.select_timeout	= 255;
	info->info.ifcfg.asyncperiod	= 200; /* ns */
	info->info.ifcfg.sync_max_depth	= 7;
	info->info.ifcfg.cntl3		= CNTL3_BS8 | CNTL3_FASTSCSI | CNTL3_FASTCLK;
	info->info.ifcfg.disconnect_ok	= 1;
	info->info.ifcfg.wide_max_size	= 0;
	info->info.dma.setup		= powertecscsi_dma_setup;
	info->info.dma.pseudo		= NULL;
	info->info.dma.stop		= powertecscsi_dma_stop;

	ret = request_irq(host->irq, powertecscsi_intr,
			  SA_INTERRUPT, "powertec", host);
	if (ret) {
		printk("scsi%d: IRQ%d not free: %d\n",
		       host->host_no, host->irq, ret);
		goto out_region;
	}

	if (host->dma_channel != NO_DMA) {
		if (request_dma(host->dma_channel, "powertec")) {
			printk("scsi%d: DMA%d not free, using PIO\n",
			       host->host_no, host->dma_channel);
			host->dma_channel = NO_DMA;
		} else {
			set_dma_speed(host->dma_channel, 180);
		}
	}

	fas216_init(host);

	ret = scsi_add_host(host, &ec->dev);
	if (ret == 0)
		goto out;

	fas216_release(host);

	if (host->dma_channel != NO_DMA)
		free_dma(host->dma_channel);
	free_irq(host->irq, host);
 out_region:
	release_region(host->io_port + POWERTEC_FAS216_OFFSET,
		       POWERTEC_FAS216_SIZE);
 out_free:
	scsi_unregister(host);

 out:
	return ret;
}

static void __devexit powertecscsi_remove(struct expansion_card *ec)
{
	struct Scsi_Host *host = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);
	scsi_remove_host(host);
	fas216_release(host);

	if (host->dma_channel != NO_DMA)
		free_dma(host->dma_channel);
	free_irq(host->irq, host);
	release_region(host->io_port + POWERTEC_FAS216_OFFSET,
		       POWERTEC_FAS216_SIZE);
	scsi_unregister(host);
}

static const struct ecard_id powertecscsi_cids[] = {
	{ MANU_ALSYSTEMS, PROD_ALSYS_SCSIATAPI },
	{ 0xffff, 0xffff },
};

static struct ecard_driver powertecscsi_driver = {
	.probe		= powertecscsi_probe,
	.remove		= __devexit_p(powertecscsi_remove),
	.id_table	= powertecscsi_cids,
	.drv = {
		.name	= "powertecscsi",
	},
};

static int __init powertecscsi_init(void)
{
	return ecard_register_driver(&powertecscsi_driver);
}

static void __exit powertecscsi_exit(void)
{
	ecard_remove_driver(&powertecscsi_driver);
}

module_init(powertecscsi_init);
module_exit(powertecscsi_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Powertec SCSI driver");
MODULE_PARM(term, "1-8i");
MODULE_PARM_DESC(term, "SCSI bus termination");
MODULE_LICENSE("GPL");

/*
 *
 *  drivers/scsi/pc980155.c
 *
 *  PC-9801-55 SCSI host adapter driver
 *
 *  Copyright (C) 1997-2003  Kyoto University Microcomputer Club
 *			     (Linux/98 project)
 *			     Tomoharu Ugawa <ohirune@kmc.gr.jp>
 *
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/delay.h>

#include <asm/dma.h>

#include "scsi.h"
#include "hosts.h"
#include "wd33c93.h"
#include "pc980155.h"

extern int pc98_bios_param(struct scsi_device *, struct block_device *,
				sector_t, int *);
static int scsi_pc980155_detect(Scsi_Host_Template *);
static int scsi_pc980155_release(struct Scsi_Host *);

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#undef PC_9801_55_DEBUG
#undef PC_9801_55_DEBUG_VERBOSE

#define NR_BASE_IOS 4
static int nr_base_ios = NR_BASE_IOS;
static unsigned int base_ios[NR_BASE_IOS] = {0xcc0, 0xcd0, 0xce0, 0xcf0};
static wd33c93_regs init_regs;
static int io;

static struct Scsi_Host *pc980155_host = NULL;

static void pc980155_intr_handle(int irq, void *dev_id, struct pt_regs *regp);

static inline void pc980155_dma_enable(unsigned int base_io)
{
	outb(0x01, REG_CWRITE);
}

static inline void pc980155_dma_disable(unsigned int base_io)
{
	outb(0x02, REG_CWRITE);
}


static void pc980155_intr_handle(int irq, void *dev_id, struct pt_regs *regp)
{
	wd33c93_intr(pc980155_host);
}

static int dma_setup(Scsi_Cmnd *sc, int dir_in)
{
  /*
   * sc->SCp.this_residual : transfer count
   * sc->SCp.ptr : distination address (virtual address)
   * dir_in : data direction (DATA_OUT_DIR:0 or DATA_IN_DIR:1)
   *
   * if success return 0
   */

   /*
    * DMA WRITE MODE
    * bit 7,6 01b single mode (this mode only)
    * bit 5   inc/dec (default:0 = inc)
    * bit 4   auto initialize (normaly:0 = off)
    * bit 3,2 01b memory -> io
    *         10b io -> memory
    *         00b verify
    * bit 1,0 channel
    */
	disable_dma(sc->device->host->dma_channel);
	set_dma_mode(sc->device->host->dma_channel,
			0x40 | (dir_in ? 0x04 : 0x08));
	clear_dma_ff(sc->device->host->dma_channel);
	set_dma_addr(sc->device->host->dma_channel, virt_to_phys(sc->SCp.ptr));
	set_dma_count(sc->device->host->dma_channel, sc->SCp.this_residual);
#ifdef PC_9801_55_DEBUG
	printk("D%d(%x)D", sc->device->host->dma_channel,
		sc->SCp.this_residual);
#endif
	enable_dma(sc->device->host->dma_channel);
	pc980155_dma_enable(sc->device->host->io_port);
	return 0;
}

static void dma_stop(struct Scsi_Host *instance, Scsi_Cmnd *sc, int status)
{
  /*
   * instance: Hostadapter's instance
   * sc: scsi command
   * status: True if success
   */
	pc980155_dma_disable(sc->device->host->io_port);
	disable_dma(sc->device->host->dma_channel);
}  

/* return non-zero on detection */
static inline int pc980155_test_port(wd33c93_regs regs)
{
	/* Quick and dirty test for presence of the card. */
	if (inb(regs.SASR) == 0xff)
		return 0;

	return 1;
}

static inline int pc980155_getconfig(unsigned int base_io, wd33c93_regs regs,
					unsigned char* irq, unsigned char* dma,
					unsigned char* scsi_id)
{
	static unsigned char irqs[] = {3, 5, 6, 9, 12, 13};
	unsigned char result;
  
	printk(KERN_DEBUG "PC-9801-55: base_io=%x SASR=%x SCMD=%x\n",
		base_io, regs.SASR, regs.SCMD);
	result = read_pc980155_resetint(regs);
	printk(KERN_DEBUG "PC-9801-55: getting config (%x)\n", result);
	*scsi_id = result & 0x07;
	*irq = (result >> 3) & 0x07;
	if (*irq > 5) {
		printk(KERN_ERR "PC-9801-55 (base %#x): impossible IRQ (%d)"
			" - other device here?\n", base_io, *irq);
		return 0;
	}

	*irq = irqs[*irq];
	result = inb(REG_STATRD);
	*dma = result & 0x03;
	if (*dma == 1) {
		printk(KERN_ERR
			"PC-9801-55 (base %#x): impossible DMA channl (%d)"
			" - other device here?\n", base_io, *dma);
		return 0;
	}
#ifdef PC_9801_55_DEBUG
	printk("PC-9801-55: end of getconfig\n");
#endif
	return 1;
}

/* return non-zero on detection */
static int scsi_pc980155_detect(Scsi_Host_Template* tpnt)
{
	unsigned int base_io;
	unsigned char irq, dma, scsi_id;
	int i;
#ifdef PC_9801_55_DEBUG
	unsigned char debug;
#endif
  
	if (io) {
		base_ios[0] = io;
		nr_base_ios = 1;
	}

	for (i = 0; i < nr_base_ios; i++) {
		base_io = base_ios[i];
		init_regs.SASR = REG_ADDRST;
		init_regs.SCMD = REG_CONTRL;
#ifdef PC_9801_55_DEBUG
		printk("PC-9801-55: SASR(%x = %x)\n", SASR, REG_ADDRST);
#endif
		if (!request_region(base_io, 6, "PC-9801-55"))
			continue;

		if (pc980155_test_port(init_regs) &&
		    pc980155_getconfig(base_io, init_regs,
					&irq, &dma, &scsi_id))
			goto found;

		release_region(base_io, 6);
	}

	printk("PC-9801-55: not found\n");
	return 0;

	found:
#ifdef PC_9801_55_DEBUG
	printk("PC-9801-55: config: base io = %x, irq = %d, dma channel = %d, scsi id = %d\n", base_io, irq, dma, scsi_id);
#endif
	if (request_irq(irq, pc980155_intr_handle, 0, "PC-9801-55", NULL)) {
		printk(KERN_ERR "PC-9801-55: unable to allocate IRQ %d\n", irq);
		goto err1;
	}

	if (request_dma(dma, "PC-9801-55")) {
		printk(KERN_ERR "PC-9801-55: unable to allocate DMA channel %d\n", dma);
		goto err2;
	}

	pc980155_host = scsi_register(tpnt, sizeof(struct WD33C93_hostdata));
	if (pc980155_host) {
		pc980155_host->this_id = scsi_id;
		pc980155_host->io_port = base_io;
		pc980155_host->n_io_port = 6;
		pc980155_host->irq = irq;
		pc980155_host->dma_channel = dma;
		printk("PC-9801-55: scsi host found at %x irq = %d, use dma channel %d.\n", base_io, irq, dma);
		pc980155_int_enable(init_regs);
		wd33c93_init(pc980155_host, init_regs, dma_setup, dma_stop,
				WD33C93_FS_12_15);
		return 1;
	}

	printk(KERN_ERR "PC-9801-55: failed to register device\n");

err2:
	free_irq(irq, NULL);
err1:
	release_region(base_io, 6);
	return 0;
}

static int scsi_pc980155_release(struct Scsi_Host *shost)
{
	struct WD33C93_hostdata *hostdata
		= (struct WD33C93_hostdata *)shost->hostdata;

	pc980155_int_disable(hostdata->regs);
	release_region(shost->io_port, shost->n_io_port);
	free_irq(shost->irq, NULL);
	free_dma(shost->dma_channel);
	wd33c93_release();
	return 1;
}

static int pc980155_bus_reset(Scsi_Cmnd *cmd)
{
	struct WD33C93_hostdata *hostdata
		= (struct WD33C93_hostdata *)cmd->device->host->hostdata;

	pc980155_int_disable(hostdata->regs);
	pc980155_assert_bus_reset(hostdata->regs);
	udelay(50);
	pc980155_negate_bus_reset(hostdata->regs);
	(void) inb(hostdata->regs.SASR);
	(void) read_pc980155(hostdata->regs, WD_SCSI_STATUS);
	pc980155_int_enable(hostdata->regs);
	wd33c93_host_reset(cmd);
	return SUCCESS;
}


#ifndef MODULE
static int __init pc980155_setup(char *str)
{
        int ints[4];

        str = get_options(str, ARRAY_SIZE(ints), ints);
        if (ints[0] > 0)
		io = ints[1];
        return 1;
}
__setup("pc980155_io=", pc980155_setup);
#endif

MODULE_PARM(io, "i");
MODULE_AUTHOR("Tomoharu Ugawa <ohirune@kmc.gr.jp>");
MODULE_DESCRIPTION("PC-9801-55 SCSI host adapter driver");
MODULE_LICENSE("GPL");

static Scsi_Host_Template driver_template = {
	.proc_info		= wd33c93_proc_info,
	.name			= "SCSI PC-9801-55",
	.detect			= scsi_pc980155_detect,
	.release		= scsi_pc980155_release,
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_bus_reset_handler	= pc980155_bus_reset,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.bios_param		= pc98_bios_param,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN, /* dont use link command */
	.unchecked_isa_dma	= 1, /* use dma **XXXX***/
	.use_clustering		= ENABLE_CLUSTERING,
	.proc_name		= "PC_9801_55",
};

#include "scsi_module.c"

/*
 * Zalon 53c7xx device driver.
 * By Richard Hirst (rhirst@linuxcare.com)
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/config.h>
#include <linux/module.h>

#include <scsi/scsicam.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/delay.h>
#include <asm/io.h>

#include "../parisc/gsc.h"
#include "scsi.h"
#include <scsi/scsi_host.h>

#include "ncr53c8xx.h"

MODULE_AUTHOR("Richard Hirst");
MODULE_DESCRIPTION("Bluefish/Zalon 720 SCSI Driver");
MODULE_LICENSE("GPL");

#define GSC_SCSI_ZALON_OFFSET 0x800

#define IO_MODULE_EIM		(1*4)
#define IO_MODULE_DC_ADATA	(2*4)
#define IO_MODULE_II_CDATA	(3*4)
#define IO_MODULE_IO_COMMAND	(12*4)
#define IO_MODULE_IO_STATUS	(13*4)

#define IOSTATUS_RY             0x40
#define IOSTATUS_FE             0x80
#define IOIIDATA_SMINT5L        0x40000000
#define IOIIDATA_MINT5EN        0x20000000
#define IOIIDATA_PACKEN         0x10000000
#define IOIIDATA_PREFETCHEN     0x08000000
#define IOIIDATA_IOII           0x00000020

#define CMD_RESET		5

static ncr_chip zalon720_chip __initdata = {
	.device_id =	PSEUDO_720_ID,
	.revision_id =	0x0f,
	.name =		"720",
	.burst_max =	3,
	.offset_max =	8,
	.nr_divisor =	4,
	.features =	FE_WIDE | FE_DIFF | FE_EHP| FE_MUX | FE_EA,
};



#if 0
/* FIXME:
 * Is this function dead code? or is someone planning on using it in the
 * future.  The clock = (int) pdc_result[16] does not look correct to
 * me ... I think it should be iodc_data[16].  Since this cause a compile
 * error with the new encapsulated PDC, I'm not compiling in this function.
 * - RB
 */
/* poke SCSI clock out of iodc data */

static u8 iodc_data[32] __attribute__ ((aligned (64)));
static unsigned long pdc_result[32] __attribute__ ((aligned (16))) ={0,0,0,0};

static int 
lasi_scsi_clock(void * hpa, int defaultclock)
{
	int clock, status;

	status = pdc_iodc_read(&pdc_result, hpa, 0, &iodc_data, 32 );
	if (status == PDC_RET_OK) {
		clock = (int) pdc_result[16];
	} else {
		printk(KERN_WARNING "%s: pdc_iodc_read returned %d\n", __FUNCTION__, status);
		clock = defaultclock; 
	}

	printk(KERN_DEBUG "%s: SCSI clock %d\n", __FUNCTION__, clock);
 	return clock;
}
#endif

static Scsi_Host_Template zalon7xx_template = {
	.module		= THIS_MODULE,
	.proc_name	= "zalon7xx",
};

static int __init
zalon_probe(struct parisc_device *dev)
{
	struct gsc_irq gsc_irq;
	u32 zalon_vers;
	int irq, error = -ENODEV;
	unsigned long zalon = dev->hpa;
	unsigned long io_port = zalon + GSC_SCSI_ZALON_OFFSET;
	static int unit = 0;
	struct Scsi_Host *host;
	struct ncr_device device;

	__raw_writel(CMD_RESET, zalon + IO_MODULE_IO_COMMAND);
	while (!(__raw_readl(zalon + IO_MODULE_IO_STATUS) & IOSTATUS_RY))
		;
	__raw_writel(IOIIDATA_MINT5EN | IOIIDATA_PACKEN | IOIIDATA_PREFETCHEN,
		zalon + IO_MODULE_II_CDATA);

	/* XXX: Save the Zalon version for bug workarounds? */
	zalon_vers = __raw_readl(dev->hpa + IO_MODULE_II_CDATA) & 0x07000000;
	zalon_vers >>= 24;

	/* Setup the interrupts first.
	** Later on request_irq() will register the handler.
	*/
        irq = gsc_alloc_irq(&gsc_irq);

	printk("%s: Zalon vers field is 0x%x, IRQ %d\n", __FUNCTION__,
		zalon_vers, irq);

	__raw_writel(gsc_irq.txn_addr | gsc_irq.txn_data, dev->hpa + IO_MODULE_EIM);

	if (zalon_vers == 0)
		printk(KERN_WARNING "%s: Zalon 1.1 or earlier\n", __FUNCTION__);

	memset(&device, 0, sizeof(struct ncr_device));

	/* The following three are needed before any other access. */
	writeb(0x20, io_port + 0x38); /* DCNTL_REG,  EA  */
	writeb(0x04, io_port + 0x1b); /* CTEST0_REG, EHP */
	writeb(0x80, io_port + 0x22); /* CTEST4_REG, MUX */

	/* Initialise ncr_device structure with items required by ncr_attach. */
	device.chip		= zalon720_chip;
	device.host_id		= 7;
	device.dev		= &dev->dev;
	device.slot.base	= (u_long)io_port;
	device.slot.base_c	= (u_long)io_port;
	device.slot.irq		= irq;
	device.differential	= 2;

	host = ncr_attach(&zalon7xx_template, unit, &device);
	if (!host)
		goto fail;

	if (request_irq(irq, ncr53c8xx_intr, SA_SHIRQ, dev->dev.bus_id, host)) {
		printk(KERN_ERR "%s: irq problem with %d, detaching\n ",
		       dev->dev.bus_id, irq);
		goto fail;
	}

	unit++;

	dev_set_drvdata(&dev->dev, host);

	error = scsi_add_host(host, &dev->dev);
	if (error)
		goto fail_free_irq;

	scsi_scan_host(host);
	return 0;

 fail_free_irq:
	free_irq(irq, host);
 fail:
	ncr53c8xx_release(host);
	return error;
}

static struct parisc_device_id zalon_tbl[] = {
	{ HPHW_A_DMA, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00089 }, 
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, zalon_tbl);

static int __exit zalon_remove(struct parisc_device *dev)
{
	struct Scsi_Host *host = dev_get_drvdata(&dev->dev);
	int irq = host->irq;

	scsi_remove_host(host);
	ncr53c8xx_release(host);
	free_irq(irq, host);

	return 0;
}
	

static struct parisc_driver zalon_driver = {
	.name =		"GSC SCSI (Zalon)",
	.id_table =	zalon_tbl,
	.probe =	zalon_probe,
	.remove =	__devexit_p(zalon_remove),
};

static int __init zalon7xx_init(void)
{
	return register_parisc_driver(&zalon_driver);
}

static void __exit zalon7xx_exit(void)
{
	unregister_parisc_driver(&zalon_driver);
}

module_init(zalon7xx_init);
module_exit(zalon7xx_exit);

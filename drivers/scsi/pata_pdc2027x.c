/*
 *  Promise PATA TX2/TX4/TX2000/133 IDE driver for pdc20268 to pdc20277.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Ported to libata by:
 *  Albert Lee <albertcc@tw.ibm.com> IBM Corporation 
 *
 *  Copyright (C) 1998-2002		Andre Hedrick <andre@linux-ide.org>
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
 * 
 * 
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/io.h>

#define DRV_NAME	"pata_pdc2027x"
#define DRV_VERSION	"0.57"
#undef PDC_DEBUG

#ifdef PDC_DEBUG
#define PDPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__, ## args)
#else
#define PDPRINTK(fmt, args...)
#endif

enum {
	PDC_UDMA_100        = 0,
	PDC_UDMA_133        = 1,
	
	PDC_100_MHZ         = 100000000,
	PDC_133_MHZ         = 133333333,
};

static int pdc2027x_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static void pdc2027x_remove_one(struct pci_dev *pdev);
static void pdc2027x_phy_reset(struct ata_port *ap);
static void pdc2027x_set_piomode(struct ata_port *ap, struct ata_device *adev);
static void pdc2027x_set_dmamode(struct ata_port *ap, struct ata_device *adev);
static void pdc2027x_post_set_mode(struct ata_port *ap);
static int pdc2027x_check_atapi_dma(struct ata_queued_cmd *qc);

/* 
 * ATA Timing Tables based on 133MHz controller clock.
 * These tables are only used when the controller is in 133MHz clock.
 * If the controller is in 100MHz clock, the ASIC hardware will 
 * set the timing registers automatically when "set feature" command 
 * is issued to the device. However, if the controller clock is 133MHz, 
 * the following tables must be used.
 */
static struct pdc2027x_pio_timing {
	u8 value0, value1, value2;
} pdc2027x_pio_timing_tbl [] = {
	{ 0xfb, 0x2b, 0xac }, /* PIO mode 0 */
	{ 0x46, 0x29, 0xa4 }, /* PIO mode 1 */
	{ 0x23, 0x26, 0x64 }, /* PIO mode 2 */
	{ 0x27, 0x0d, 0x35 }, /* PIO mode 3, IORDY on, Prefetch off */
	{ 0x23, 0x09, 0x25 }, /* PIO mode 4, IORDY on, Prefetch off */
};

static struct pdc2027x_mdma_timing {
	u8 value0, value1;
} pdc2027x_mdma_timing_tbl [] = {
	{ 0xdf, 0x5f }, /* MDMA mode 0 */
	{ 0x6b, 0x27 }, /* MDMA mode 1 */
	{ 0x69, 0x25 }, /* MDMA mode 2 */
};

static struct pdc2027x_udma_timing {
	u8 value0, value1, value2;
} pdc2027x_udma_timing_tbl [] = {
	{ 0x4a, 0x0f, 0xd5 }, /* UDMA mode 0 */
	{ 0x3a, 0x0a, 0xd0 }, /* UDMA mode 1 */
	{ 0x2a, 0x07, 0xcd }, /* UDMA mode 2 */
	{ 0x1a, 0x05, 0xcd }, /* UDMA mode 3 */
	{ 0x1a, 0x03, 0xcd }, /* UDMA mode 4 */
	{ 0x1a, 0x02, 0xcb }, /* UDMA mode 5 */
	{ 0x1a, 0x01, 0xcb }, /* UDMA mode 6 */
};

static struct pci_device_id pdc2027x_pci_tbl[] = {
#ifdef ATA_ENABLE_PATA
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20268, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PDC_UDMA_100 },
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20269, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PDC_UDMA_133 },
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20270, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PDC_UDMA_100 },
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20271, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PDC_UDMA_133 },
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20275, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PDC_UDMA_133 },
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20276, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PDC_UDMA_133 },
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20277, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PDC_UDMA_133 },
#endif
	{ }	/* terminate list */
};

static struct pci_driver pdc2027x_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= pdc2027x_pci_tbl,
	.probe			= pdc2027x_init_one,
	.remove			= __devexit_p(pdc2027x_remove_one),
};

static Scsi_Host_Template pdc2027x_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations pdc2027x_pata100_ops = {
	.port_disable		= ata_port_disable,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.phy_reset		= pdc2027x_phy_reset,

	.check_atapi_dma	= pdc2027x_check_atapi_dma,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_operations pdc2027x_pata133_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= pdc2027x_set_piomode,   
	.set_dmamode		= pdc2027x_set_dmamode,  

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.phy_reset		= pdc2027x_phy_reset,   
	.post_set_mode		= pdc2027x_post_set_mode,

	.check_atapi_dma	= pdc2027x_check_atapi_dma,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_info pdc2027x_port_info[] = {
	/* PDC_UDMA_100 */
	{
		.sht		= &pdc2027x_sht,
		.host_flags	= ATA_FLAG_NO_LEGACY | ATA_FLAG_SLAVE_POSS | 
		                  ATA_FLAG_SRST,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA5, /* udma0-5 */
		.port_ops	= &pdc2027x_pata100_ops,
	},
	/* PDC_UDMA_133 */
	{
		.sht		= &pdc2027x_sht,
		.host_flags	= ATA_FLAG_NO_LEGACY | ATA_FLAG_SLAVE_POSS | 
                        	  ATA_FLAG_SRST,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6, /* udma0-6 */
		.port_ops	= &pdc2027x_pata133_ops,
	},
};

MODULE_AUTHOR("Andre Hedrick, Frank Tiernan, Albert Lee");
MODULE_DESCRIPTION("libata driver module for Promise PDC20268 to PDC20277");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pdc2027x_pci_tbl);

/**
 * pdc_get_indexed_reg - Set pdc202xx extended register
 * @ap: Port to which the extended register is set
 * @index: index of the extended register
 */
static u8 pdc_get_indexed_reg(struct ata_port *ap, u8 index)
{
	u8 tmp8;

	outb(index, ap->ioaddr.bmdma_addr + 1); 
	tmp8 = inb(ap->ioaddr.bmdma_addr + 3);

	PDPRINTK("Get index reg%X[%X] \n", index, tmp8);
	return tmp8;
}
/**
 * pdc_set_indexed_reg - Read pdc202xx extended register
 * @ap: Port to which the extended register is read
 * @index: index of the extended register
 */
static void pdc_set_indexed_reg(struct ata_port *ap, u8 index, u8 value)
{
	outb(index, ap->ioaddr.bmdma_addr + 1); 
	outb(value, ap->ioaddr.bmdma_addr + 3);
	PDPRINTK("Set index reg%X[%X] \n", index, value);
}
/**
 *	pdc2027x_pata_cbl_detect - Probe host controller cable detect info
 *	@ap: Port for which cable detect info is desired
 *
 *	Read 80c cable indicator from Promise extended register.  
 *      This register is latched when the system is reset.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void pdc2027x_cbl_detect(struct ata_port *ap)
{
	u8 cbl40c;

	/* check cable detect results */
	cbl40c = pdc_get_indexed_reg(ap, 0x0b) & 0x04;

	if (cbl40c)
		goto cbl40;

	PDPRINTK("No cable or 80-conductor cable on port %d\n", ap->port_no);

	ap->cbl = ATA_CBL_PATA80;
	return;

cbl40:
	printk(KERN_INFO DRV_NAME ": 40-conductor cable detected on port %d\n", ap->port_no);
	ap->cbl = ATA_CBL_PATA40;
	ap->udma_mask &= ATA_UDMA_MASK_40C;
}
/**
 * pdc2027x_port_enabled - Check extended register at 0x04 to see whether the port is enabled.
 * @ap: Port to check
 */
static inline int pdc2027x_port_enabled(struct ata_port *ap)
{
	return pdc_get_indexed_reg(ap, 0x04) & 0x02;
}
/**
 *	pdc2027x_phy_reset - Probe specified port on PATA host controller
 *	@ap: Port to probe
 *
 *	Probe PATA phy.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void pdc2027x_phy_reset(struct ata_port *ap)
{
	/* Check whether port enabled */
	if (!pdc2027x_port_enabled(ap)) {
		ata_port_disable(ap);
		printk(KERN_INFO "ata%u: port disabled. ignoring.\n", ap->id);
		return;
	}

	pdc2027x_cbl_detect(ap);
	ata_port_probe(ap);
	ata_bus_reset(ap);
}
/**
 *	pdc2027x_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port to configure
 *	@adev: um
 *	@pio: PIO mode, 0 - 4
 *
 *	Set PIO mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void pdc2027x_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio      = adev->pio_mode - XFER_PIO_0;
	unsigned int drive_dn = (ap->port_no ? 2 : 0) + adev->devno;
	u8 adj		      = (drive_dn%2) ? 0x08 : 0x00;

	PDPRINTK("adev->pio_mode[%X]\n", adev->pio_mode);

	/* Sanity check */
	if(pio > 4) {
		printk(KERN_ERR DRV_NAME ": Unknown pio mode [%d] ignored\n", pio);
		return;

	}

	/* Set the PIO timing registers using value table for 133MHz */
	PDPRINTK("Set pio regs... \n");

	pdc_set_indexed_reg(ap, 0x0c + adj, pdc2027x_pio_timing_tbl[pio].value0);
	pdc_set_indexed_reg(ap, 0x0d + adj, pdc2027x_pio_timing_tbl[pio].value1);
	pdc_set_indexed_reg(ap, 0x13 + adj, pdc2027x_pio_timing_tbl[pio].value2);

	PDPRINTK("Set pio regs done\n");

	PDPRINTK("Set to pio mode[%u] \n", pio);
}
/**
 *	pdc2027x_set_dmamode - Initialize host controller PATA UDMA timings
 *	@ap: Port to configure
 *	@adev: um
 *	@udma: udma mode, XFER_UDMA_0 to XFER_UDMA_6
 *
 *	Set UDMA mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void pdc2027x_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int dma_mode	= adev->dma_mode;
	unsigned int drive_dn	= (ap->port_no ? 2 : 0) + adev->devno;
	u8 adj			= (drive_dn%2) ? 0x08 : 0x00;
	u8 tmp8;

	if((dma_mode >= XFER_UDMA_0) && 
	   (dma_mode <= XFER_UDMA_6)) {
		/* Set the UDMA timing registers with value table for 133MHz */
		unsigned int udma_mode = dma_mode & 0x07;

		if (dma_mode == XFER_UDMA_2) {
			/*
			 * Turn off tHOLD.
			 * If tHOLD is '1', the hardware will add half clock for data hold time.
			 * This code segment seems to be no effect. tHOLD will be overwritten below.
			 */
			tmp8 = pdc_get_indexed_reg(ap, 0x10 + adj);
			pdc_set_indexed_reg(ap, 0x10 + adj, tmp8 & 0x7f);
		} 

		PDPRINTK("Set udma regs... \n");
		pdc_set_indexed_reg(ap, 0x10 + adj, pdc2027x_udma_timing_tbl[udma_mode].value0);
		pdc_set_indexed_reg(ap, 0x11 + adj, pdc2027x_udma_timing_tbl[udma_mode].value1);
		pdc_set_indexed_reg(ap, 0x12 + adj, pdc2027x_udma_timing_tbl[udma_mode].value2);
		PDPRINTK("Set udma regs done\n");
		
		PDPRINTK("Set to udma mode[%u] \n", udma_mode);

	} else  if((dma_mode >= XFER_MW_DMA_0) && 
		   (dma_mode <= XFER_MW_DMA_2)) {
		/* Set the MDMA timing registers with value table for 133MHz */
		unsigned int mdma_mode = dma_mode & 0x07;

		PDPRINTK("Set mdma regs... \n");
		pdc_set_indexed_reg(ap, 0x0e + adj, pdc2027x_mdma_timing_tbl[mdma_mode].value0);
		pdc_set_indexed_reg(ap, 0x0f + adj, pdc2027x_mdma_timing_tbl[mdma_mode].value1);
		PDPRINTK("Set mdma regs done\n");
		
		PDPRINTK("Set to mdma mode[%u] \n", mdma_mode);
	} else {
		printk(KERN_ERR DRV_NAME ": Unknown dma mode [%u] ignored\n", dma_mode);
	}
}

/**
 *	pdc2027x_post_set_mode - Set the timing registers back to correct values.
 *	@ap: Port to configure
 *	
 *	The pdc2027x hardware will look at "SET FEATURES" and change the timing registers 
 *	automatically. The values set by the hardware might be incorrect, under 133Mhz PLL.
 *	This function overwrites the possibly incorrect values set by the hardware to be correct.
 */
static void pdc2027x_post_set_mode(struct ata_port *ap)
{
	int i;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];

		if (ata_dev_present(dev)) {
			u8 adj = (i % 2) ? 0x08 : 0x00;
			u8 tmp8;
			
			pdc2027x_set_piomode(ap, dev);

			/*
			 * Enable prefetch if the device support PIO only.
			 */
			if (dev->xfer_shift == ATA_SHIFT_PIO) { 
				tmp8 = pdc_get_indexed_reg(ap, 0x13 + adj);
				pdc_set_indexed_reg(ap, 0x13 + adj, tmp8 | 0x02);
				
				PDPRINTK("Turn on prefetch\n");
			} else {
				pdc2027x_set_dmamode(ap, dev);
			}
		}
	}
}

/**
 *	pdc2027x_check_atapi_dma - Check whether ATAPI DMA can be supported for this command
 *	@qc: Metadata associated with taskfile to check
 *
 *	LOCKING:
 *	None (inherited from caller).
 *
 *	RETURNS: 0 when ATAPI DMA can be used
 *		 1 otherwise
 */
static int pdc2027x_check_atapi_dma(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	int rc = 0;

	/* pdc2027x can only do ATAPI DMA for specific buffer size */
	if (cmd->request_bufflen % 256)
		rc = 1;

	return rc;
}

/**
 * adjust_pll - Adjust the PLL input clock in Hz.
 *
 * @pdc_controller: controller specific information
 * @probe_ent: For the port address
 * @pll_clock: The input of PLL in HZ
 */
static void pdc_adjust_pll(struct ata_probe_ent *probe_ent, long pll_clock, unsigned int board_idx) 
{

	u8 pll_ctl0, pll_ctl1;
	long pll_clock_khz = pll_clock / 1000;
	long pout_required = board_idx? PDC_133_MHZ:PDC_100_MHZ;
	long ratio = pout_required / pll_clock_khz;
	int F, R;
	

	/* Sanity check */
	if (unlikely(pll_clock_khz < 5000L || pll_clock_khz > 70000L)) {
		printk(KERN_ERR DRV_NAME ": Invalid PLL input clock %ldkHz, give up!\n", pll_clock_khz);
		return;
	} 

#ifdef PDC_DEBUG
	PDPRINTK("pout_required is %ld\n", pout_required);

	/* Show the current clock value of PLL control register
	 * (maybe already configured by the firmware)
	 */
	outb(0x02, probe_ent->port[1].bmdma_addr + 0x01);
	pll_ctl0 = inb(probe_ent->port[1].bmdma_addr + 0x03);
	outb(0x03, probe_ent->port[1].bmdma_addr + 0x01);
	pll_ctl1 = inb(probe_ent->port[1].bmdma_addr + 0x03);

	PDPRINTK("pll_ctl[%X][%X]\n", pll_ctl0, pll_ctl1);
#endif

	/* 
	 * Calculate the ratio of F, R and OD
	 * POUT = (F + 2) / (( R + 2) * NO)
	 */
	if (ratio < 8600L) { // 8.6x
		/* Using NO = 0x01, R = 0x0D */
		R = 0x0d;
	} else if (ratio < 12900L) { // 12.9x
		/* Using NO = 0x01, R = 0x08 */
		R = 0x08;
	} else if (ratio < 16100L) { // 16.1x
		/* Using NO = 0x01, R = 0x06 */
		R = 0x06;
	} else if (ratio < 64000L) { // 64x
		R = 0x00;
	} else {
		/* Invalid ratio */
		printk(KERN_ERR DRV_NAME ": Invalid ratio %ld, give up!\n", ratio);
		return;
	}

	F = (ratio * (R+2)) / 1000 - 2;

	if (unlikely(F < 0 || F > 127)) {
		/* Invalid F */
		printk(KERN_ERR DRV_NAME ": F[%d] invalid!\n", F);
		return;
	}

	PDPRINTK("F[%d] R[%d] ratio*1000[%ld]\n", F, R, ratio);

	pll_ctl0 = (u8) F;
	pll_ctl1 = (u8) R;

	PDPRINTK("Writing pll_ctl[%X][%X]\n", pll_ctl0, pll_ctl1);

	outb(0x02,     probe_ent->port[1].bmdma_addr + 0x01);
	outb(pll_ctl0, probe_ent->port[1].bmdma_addr + 0x03);
	outb(0x03,     probe_ent->port[1].bmdma_addr + 0x01);
	outb(pll_ctl1, probe_ent->port[1].bmdma_addr + 0x03);

	/* Wait the PLL circuit to be stable */
	mdelay(30);

#ifdef PDC_DEBUG
	/*
	 *  Show the current clock value of PLL control register
	 * (maybe configured by the firmware)
	 */
	outb(0x02, probe_ent->port[1].bmdma_addr + 0x01);
	pll_ctl0 = inb(probe_ent->port[1].bmdma_addr + 0x03);
	outb(0x03, probe_ent->port[1].bmdma_addr + 0x01);
	pll_ctl1 = inb(probe_ent->port[1].bmdma_addr + 0x03);

	PDPRINTK("pll_ctl[%X][%X]\n", pll_ctl0, pll_ctl1);
#endif

	return;
}
/**
 * detect_pll_input_clock - Detect the PLL input clock in Hz.
 * @probe_ent: for the port address
 * Ex. 16949000 on 33MHz PCI bus for pdc20275. 
 *     Half of the PCI clock.
 */
static long pdc_detect_pll_input_clock(struct ata_probe_ent *probe_ent) 
{
	u8 scr1;
	unsigned long ctr0;
	unsigned long ctr1;
	unsigned long ctr2 = 0;
	unsigned long ctr3 = 0;

	unsigned long start_count, end_count;
	long pll_clock;

	/* Read current counter value */
	outb(0x20, probe_ent->port[0].bmdma_addr + 0x01);
	ctr0 = inb(probe_ent->port[0].bmdma_addr + 0x03);
	outb(0x21, probe_ent->port[0].bmdma_addr + 0x01);
	ctr1 = inb(probe_ent->port[0].bmdma_addr + 0x03);
	
	outb(0x20, probe_ent->port[1].bmdma_addr + 0x01);
	ctr2 = inb(probe_ent->port[1].bmdma_addr + 0x03);
	outb(0x21, probe_ent->port[1].bmdma_addr + 0x01);
	ctr3 = inb(probe_ent->port[1].bmdma_addr + 0x03);

	start_count = (ctr3 << 23 ) | (ctr2 << 15) | (ctr1 << 8) | ctr0;

	PDPRINTK("ctr0[%lX] ctr1[%lX] ctr2 [%lX] ctr3 [%lX]\n", ctr0, ctr1, ctr2, ctr3);

	/* Start the test mode */
	outb(0x01, probe_ent->port[0].bmdma_addr + 0x01);
	scr1 = inb(probe_ent->port[0].bmdma_addr + 0x03);
	PDPRINTK("scr1[%X]\n", scr1);
	outb(scr1 | 0x40, probe_ent->port[0].bmdma_addr + 0x03);

	/* Let the counter run for 1000 us. */
	udelay(1000);

	/* Read the counter values again */
	outb(0x20, probe_ent->port[0].bmdma_addr + 0x01);
	ctr0 = inb(probe_ent->port[0].bmdma_addr + 0x03);
	outb(0x21, probe_ent->port[0].bmdma_addr + 0x01);
	ctr1 = inb(probe_ent->port[0].bmdma_addr + 0x03);
	
	outb(0x20, probe_ent->port[1].bmdma_addr + 0x01);
	ctr2 = inb(probe_ent->port[1].bmdma_addr + 0x03);
	outb(0x21, probe_ent->port[1].bmdma_addr + 0x01);
	ctr3 = inb(probe_ent->port[1].bmdma_addr + 0x03);

	end_count = (ctr3 << 23 ) | (ctr2 << 15) | (ctr1 << 8) | ctr0;

	PDPRINTK("ctr0[%lX] ctr1[%lX] ctr2 [%lX] ctr3 [%lX]\n", ctr0, ctr1, ctr2, ctr3);

	/* Stop the test mode */
	outb(0x01, probe_ent->port[0].bmdma_addr + 0x01);
	scr1 = inb(probe_ent->port[0].bmdma_addr + 0x03);
	PDPRINTK("scr1[%X]\n", scr1);
	outb(scr1 & 0xBF, probe_ent->port[0].bmdma_addr + 0x03);

	/* calculate the input clock in Hz */
	pll_clock = (long) ((start_count - end_count) * 1000);

	PDPRINTK("start[%lu] end[%lu] \n", start_count, end_count);
	PDPRINTK("PLL input clock[%ld]Hz\n", pll_clock);

	return pll_clock;
}
/**
 * pdc_hardware_init - Initialize the hardware.
 * @pdev: instance of pci_dev found
 * @pdc_controller: controller specific information
 * @pe:  for the port address
 */
static int pdc_hardware_init(struct pci_dev *pdev, struct ata_probe_ent *pe, unsigned int board_idx)
{
	long pll_clock;

	/*
	 * Detect PLL input clock rate.
	 * On some system, where PCI bus is running at non-standard clock rate.
	 * Ex. 25MHz or 40MHz, we have to adjust the cycle_time.
	 * The pdc20275 controller employs PLL circuit to help correct timing registers setting.
	 */
	pll_clock = pdc_detect_pll_input_clock(pe);

	if(pll_clock < 0) /* counter overflow? Try again. */
		pll_clock = pdc_detect_pll_input_clock(pe);

	printk(KERN_INFO DRV_NAME ": PLL input clock %ld kHz\n", pll_clock/1000);

	/* Adjust PLL control register */
	pdc_adjust_pll(pe, pll_clock, board_idx);

	return 0;
}
/**
 * pdc2027x_init_one - PCI probe function
 * Called when an instance of PCI adapter is inserted.
 * This function checks whether the hardware is supported,
 * initialize hardware and register an instance of ata_host_set to 
 * libata by providing struct ata_probe_ent and ata_device_add().
 * (implements struct pci_driver.probe() )
 * 
 * @pdev: instance of pci_dev found
 * @ent:  matching entry in the id_tbl[]
 */
static int __devinit pdc2027x_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	unsigned int board_idx = (unsigned int) ent->driver_data;

	struct ata_probe_ent *probe_ent = NULL;
	int rc;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	/* Prepare the probe entry */
	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	probe_ent->sht		= pdc2027x_port_info[board_idx].sht;
	probe_ent->host_flags	= pdc2027x_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= pdc2027x_port_info[board_idx].pio_mask;
	probe_ent->udma_mask	= pdc2027x_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= pdc2027x_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;

	probe_ent->port[0].cmd_addr = pci_resource_start(pdev, 0);
	ata_std_ports(&probe_ent->port[0]);
	probe_ent->port[0].altstatus_addr =
		probe_ent->port[0].ctl_addr =
		pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS;
	probe_ent->port[0].bmdma_addr = pci_resource_start(pdev, 4);

	probe_ent->port[1].cmd_addr = pci_resource_start(pdev, 2);
	ata_std_ports(&probe_ent->port[1]);
	probe_ent->port[1].altstatus_addr =
		probe_ent->port[1].ctl_addr =
		pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS;
	probe_ent->port[1].bmdma_addr = pci_resource_start(pdev, 4) + 8;

	probe_ent->n_ports = 2;

	pci_set_master(pdev);
	//pci_enable_intx(pdev);

	/* initialize adapter */
	if(pdc_hardware_init(pdev, probe_ent, board_idx) != 0)
		goto err_out_free_ent;

	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}
/**
 * pdc2027x_remove_one - Called to remove a single instance of the
 * adapter.
 *
 * @dev: The PCI device to remove.
 * FIXME: module load/unload not working yet
 */
static void __devexit pdc2027x_remove_one(struct pci_dev *pdev)
{
	ata_pci_remove_one(pdev);
}
/**
 * pdc2027x_init - Called after this module is loaded into the kernel.
 */
static int __init pdc2027x_init(void)
{
	return pci_module_init(&pdc2027x_pci_driver);
}
/**
 * pdc2027x_exit - Called before this module unloaded from the kernel
 */
static void __exit pdc2027x_exit(void)
{
	pci_unregister_driver(&pdc2027x_pci_driver);
}

module_init(pdc2027x_init);
module_exit(pdc2027x_exit);

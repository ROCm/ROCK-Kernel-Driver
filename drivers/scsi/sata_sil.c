/*
 *  ata_sil.c - Silicon Image SATA
 *
 *  Copyright 2003 Red Hat, Inc.
 *  Copyright 2003 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 *  The contents of this file are subject to the Open
 *  Software License version 1.1 that can be found at
 *  http://www.opensource.org/licenses/osl-1.1.txt and is included herein
 *  by reference.
 *
 *  Alternatively, the contents of this file may be used under the terms
 *  of the GNU General Public License version 2 (the "GPL") as distributed
 *  in the kernel source COPYING file, in which case the provisions of
 *  the GPL are applicable instead of the above.  If you wish to allow
 *  the use of your version of this file only under the terms of the
 *  GPL and not to allow others to use your version of this file under
 *  the OSL, indicate your decision by deleting the provisions above and
 *  replace them with the notice and other provisions required by the GPL.
 *  If you do not delete the provisions above, a recipient may use your
 *  version of this file under either the OSL or the GPL.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/libata.h>

#define DRV_NAME	"ata_sil"
#define DRV_VERSION	"0.51"

enum {
	sil_3112		= 0,

	SIL_IDE0_TF		= 0x80,
	SIL_IDE0_CTL		= 0x8A,
	SIL_IDE0_BMDMA		= 0x00,
	SIL_IDE0_SCR		= 0x100,

	SIL_IDE1_TF		= 0xC0,
	SIL_IDE1_CTL		= 0xCA,
	SIL_IDE1_BMDMA		= 0x08,
	SIL_IDE1_SCR		= 0x180,
};

static void sil_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio);
static void sil_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma);
static int sil_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void sil_dev_config(struct ata_port *ap, struct ata_device *dev);
static u32 sil_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void sil_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);

static struct pci_device_id sil_pci_tbl[] = {
	{ 0x1095, 0x3112, PCI_ANY_ID, PCI_ANY_ID, 0, 0, sil_3112 },
	{ }	/* terminate list */
};

static struct pci_driver sil_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= sil_pci_tbl,
	.probe			= sil_init_one,
	.remove			= ata_pci_remove_one,
};

static Scsi_Host_Template sil_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= ATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
};

static struct ata_port_operations sil_ops = {
	.port_disable		= ata_port_disable,
	.dev_config		= sil_dev_config,
	.set_piomode		= sil_set_piomode,
	.set_udmamode		= sil_set_udmamode,
	.tf_load		= ata_tf_load_mmio,
	.tf_read		= ata_tf_read_mmio,
	.check_status		= ata_check_status_mmio,
	.exec_command		= ata_exec_command_mmio,
	.phy_reset		= sata_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */
	.bmdma_start            = ata_bmdma_start_mmio,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= ata_eng_timeout,
	.irq_handler		= ata_interrupt,
	.scr_read		= sil_scr_read,
	.scr_write		= sil_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_info sil_port_info[] = {
	/* sil_3112 */
	{
		.sht		= &sil_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_SRST | ATA_FLAG_MMIO,
		.pio_mask	= 0x03,			/* pio3-4 */
		.udma_mask	= 0x7f,			/* udma0-6; FIXME */
		.port_ops	= &sil_ops,
	},
};

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("low-level driver for Silicon Image SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, sil_pci_tbl);

static inline unsigned long sil_scr_addr(struct ata_port *ap, unsigned int sc_reg)
{
	unsigned long offset = ap->ioaddr.scr_addr;

	switch (sc_reg) {
	case SCR_STATUS:
		return offset + 4;
	case SCR_ERROR:
		return offset + 8;
	case SCR_CONTROL:
		return offset;
	default:
		/* do nothing */
		break;
	}

	return 0;
}

static u32 sil_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	void *mmio = (void *) sil_scr_addr(ap, sc_reg);
	if (mmio)
		return readl(mmio);
	return 0xffffffffU;
}

static void sil_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	void *mmio = (void *) sil_scr_addr(ap, sc_reg);
	if (mmio)
		writel(val, mmio);
}

/**
 *	sil_dev_config - Apply device/host-specific errata fixups
 *	@ap: Port containing device to be examined
 *	@dev: Device to be examined
 *
 *	After the IDENTIFY [PACKET] DEVICE step is complete, and a
 *	device is known to be present, this function is called.
 *	We apply two errata fixups which are specific to Silicon Image,
 *	a Seagate and a Maxtor fixup.
 *
 *	For certain Seagate devices, we must limit the maximum sectors
 *	to under 8K.
 *
 *	For certain Maxtor devices, we must not program the drive
 *	beyond udma5.
 *
 *	Both fixups are unfairly pessimistic.  As soon as I get more
 *	information on these errata, I will create a more exhaustive
 *	list, and apply the fixups to only the specific
 *	devices/hosts/firmwares that need it.
 */
static void sil_dev_config(struct ata_port *ap, struct ata_device *dev)
{
	const char *s = &dev->product[0];
	unsigned int len = strnlen(s, sizeof(dev->product));

	/* ATAPI specifies that empty space is blank-filled; remove blanks */
	while ((len > 0) && (s[len - 1] == ' '))
		len--;

	/* limit to udma5 */
	if (!memcmp(s, "Maxtor ", 7)) {
		printk(KERN_INFO "ata%u(%u): applying pessimistic Maxtor errata fix\n",
		       ap->id, dev->devno);
		ap->udma_mask &= ATA_UDMA5;
		return;
	}

	/* limit requests to 15 sectors */
	if ((len > 4) && (!memcmp(s, "ST", 2))) {
		if ((!memcmp(s + len - 2, "AS", 2)) ||
		    (!memcmp(s + len - 3, "ASL", 3))) {
			printk(KERN_INFO "ata%u(%u): applying pessimistic Seagate errata fix\n",
			       ap->id, dev->devno);
			ap->host->max_sectors = 15;
			ap->host->hostt->max_sectors = 15;
			return;
		}
	}
}

static void sil_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio)
{
	/* We need empty implementation, the core doesn't test for NULL
	 * function pointer
	 */
}

static void sil_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma)
{
	/* We need empty implementation, the core doesn't test for NULL
	 * function pointer
	 */
}

static int sil_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	unsigned long base;
	void *mmio_base;
	int rc;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	/*
	 * If this driver happens to only be useful on Apple's K2, then
	 * we should check that here as it has a normal Serverworks ID
	 */
	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	INIT_LIST_HEAD(&probe_ent->node);
	probe_ent->pdev = pdev;
	probe_ent->port_ops = sil_port_info[ent->driver_data].port_ops;
	probe_ent->sht = sil_port_info[ent->driver_data].sht;
	probe_ent->n_ports = 2;
	probe_ent->pio_mask = sil_port_info[ent->driver_data].pio_mask;
	probe_ent->udma_mask = sil_port_info[ent->driver_data].udma_mask;
       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->host_flags = sil_port_info[ent->driver_data].host_flags;

	mmio_base = ioremap(pci_resource_start(pdev, 5),
		            pci_resource_len(pdev, 5));
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}

	probe_ent->mmio_base = mmio_base;

	base = (unsigned long) mmio_base;
	probe_ent->port[0].cmd_addr = base + SIL_IDE0_TF;
	probe_ent->port[0].ctl_addr = base + SIL_IDE0_CTL;
	probe_ent->port[0].bmdma_addr = base + SIL_IDE0_BMDMA;
	probe_ent->port[0].scr_addr = base + SIL_IDE0_SCR;
	ata_std_ports(&probe_ent->port[0]);

	probe_ent->port[1].cmd_addr = base + SIL_IDE1_TF;
	probe_ent->port[1].ctl_addr = base + SIL_IDE1_CTL;
	probe_ent->port[1].bmdma_addr = base + SIL_IDE1_BMDMA;
	probe_ent->port[1].scr_addr = base + SIL_IDE1_SCR;
	ata_std_ports(&probe_ent->port[1]);

	pci_set_master(pdev);

	/* FIXME: check ata_device_add return value */
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

static int __init sil_init(void)
{
	int rc;

	rc = pci_module_init(&sil_pci_driver);
	if (rc)
		return rc;

	return 0;
}

static void __exit sil_exit(void)
{
	pci_unregister_driver(&sil_pci_driver);
}


module_init(sil_init);
module_exit(sil_exit);

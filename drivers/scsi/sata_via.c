/*
   sata_via.c - VIA Serial ATA controllers

   Copyright 2003 Red Hat, Inc.  All rights reserved.
   Copyright 2003 Jeff Garzik

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
#include <linux/blkdev.h>
#include <linux/delay.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/libata.h>

#define DRV_NAME	"sata_via"
#define DRV_VERSION	"0.11"

enum {
	via_sata		= 0,
};

static int svia_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void svia_sata_phy_reset(struct ata_port *ap);
static void svia_port_disable(struct ata_port *ap);
static void svia_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio);
static void svia_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			       unsigned int udma);

static unsigned int in_module_init = 1;

static struct pci_device_id svia_pci_tbl[] = {
	{ 0x1106, 0x3149, PCI_ANY_ID, PCI_ANY_ID, 0, 0, via_sata },

	{ }	/* terminate list */
};

static struct pci_driver svia_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= svia_pci_tbl,
	.probe			= svia_init_one,
	.remove			= ata_pci_remove_one,
};

static Scsi_Host_Template svia_sht = {
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

static struct ata_port_operations svia_sata_ops = {
	.port_disable		= svia_port_disable,
	.set_piomode		= svia_set_piomode,
	.set_udmamode		= svia_set_udmamode,

	.tf_load		= ata_tf_load_pio,
	.tf_read		= ata_tf_read_pio,
	.check_status		= ata_check_status_pio,
	.exec_command		= ata_exec_command_pio,

	.phy_reset		= svia_sata_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */

	.bmdma_start            = ata_bmdma_start_pio,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_info svia_port_info[] = {
	/* via_sata */
	{
		.sht		= &svia_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY
				  | ATA_FLAG_SRST,
		.pio_mask	= 0x03,	/* pio3-4 */
		.udma_mask	= 0x7f,	/* udma0-6 ; FIXME */
		.port_ops	= &svia_sata_ops,
	},
};

static struct pci_bits svia_enable_bits[] = {
	{ 0x40U, 1U, 0x02UL, 0x02UL },	/* port 0 */
	{ 0x40U, 1U, 0x01UL, 0x01UL },	/* port 1 */
};


MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("SCSI low-level driver for VIA SATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, svia_pci_tbl);

/**
 *	svia_sata_phy_reset -
 *	@ap:
 *
 *	LOCKING:
 *
 */

static void svia_sata_phy_reset(struct ata_port *ap)
{
	if (!pci_test_config_bits(ap->host_set->pdev,
				  &svia_enable_bits[ap->port_no])) {
		ata_port_disable(ap);
		printk(KERN_INFO "ata%u: port disabled. ignoring.\n", ap->id);
		return;
	}

	ata_port_probe(ap);
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

	ata_bus_reset(ap);
}

/**
 *	svia_port_disable -
 *	@ap:
 *
 *	LOCKING:
 *
 */

static void svia_port_disable(struct ata_port *ap)
{
	ata_port_disable(ap);

	/* FIXME */
}

/**
 *	svia_set_piomode -
 *	@ap:
 *	@adev:
 *	@pio:
 *
 *	LOCKING:
 *
 */

static void svia_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio)
{
	/* FIXME: needed? */
}

/**
 *	svia_set_udmamode -
 *	@ap:
 *	@adev:
 *	@udma:
 *
 *	LOCKING:
 *
 */

static void svia_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma)
{
	/* FIXME: needed? */
}

/**
 *	svia_init_one -
 *	@pdev:
 *	@ent:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int svia_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info *port_info[1];
	unsigned int n_ports = 1;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	/* no hotplugging support (FIXME) */
	if (!in_module_init)
		return -ENODEV;

	port_info[0] = &svia_port_info[ent->driver_data];

	return ata_pci_init_one(pdev, port_info, n_ports);
}

/**
 *	svia_init -
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int __init svia_init(void)
{
	int rc;

	DPRINTK("pci_module_init\n");
	rc = pci_module_init(&svia_pci_driver);
	if (rc)
		return rc;

	in_module_init = 0;

	DPRINTK("done\n");
	return 0;
}

/**
 *	svia_exit -
 *
 *	LOCKING:
 *
 */

static void __exit svia_exit(void)
{
	pci_unregister_driver(&svia_pci_driver);
}

module_init(svia_init);
module_exit(svia_exit);


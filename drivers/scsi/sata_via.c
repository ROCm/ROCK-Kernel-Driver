/*
   sata_via.c - VIA Serial ATA controllers

   Maintained by:  Jeff Garzik <jgarzik@pobox.com>
   		   Please ALWAYS copy linux-ide@vger.kernel.org
 		   on emails.

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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/libata.h>
#include <asm/io.h>

#define DRV_NAME	"sata_via"
#define DRV_VERSION	"0.20"

enum {
	via_sata		= 0,

	SATA_CHAN_ENAB		= 0x40, /* SATA channel enable */
	SATA_INT_GATE		= 0x41, /* SATA interrupt gating */
	SATA_NATIVE_MODE	= 0x42, /* Native mode enable */
	SATA_PATA_SHARING	= 0x49, /* PATA/SATA sharing func ctrl */

	PORT0			= (1 << 1),
	PORT1			= (1 << 0),

	ENAB_ALL		= PORT0 | PORT1,

	INT_GATE_ALL		= PORT0 | PORT1,

	NATIVE_MODE_ALL		= (1 << 7) | (1 << 6) | (1 << 5) | (1 << 4),

	SATA_EXT_PHY		= (1 << 6), /* 0==use PATA, 1==ext phy */
	SATA_2DEV		= (1 << 5), /* SATA is master/slave */
};

static int svia_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static u32 svia_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void svia_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);

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

static struct ata_port_operations svia_sata_ops = {
	.port_disable		= ata_port_disable,

	.tf_load		= ata_tf_load_pio,
	.tf_read		= ata_tf_read_pio,
	.check_status		= ata_check_status_pio,
	.exec_command		= ata_exec_command_pio,

	.phy_reset		= sata_phy_reset,

	.bmdma_setup            = ata_bmdma_setup_pio,
	.bmdma_start            = ata_bmdma_start_pio,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,

	.scr_read		= svia_scr_read,
	.scr_write		= svia_scr_write,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("SCSI low-level driver for VIA SATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, svia_pci_tbl);

static u32 svia_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;
	return inl(ap->ioaddr.scr_addr + (4 * sc_reg));
}

static void svia_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;
	outl(val, ap->ioaddr.scr_addr + (4 * sc_reg));
}

static const unsigned int svia_bar_sizes[] = {
	8, 4, 8, 4, 16, 256
};

static unsigned long svia_scr_addr(unsigned long addr, unsigned int port)
{
	return addr + (port * 128);
}

static int svia_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	unsigned int i;
	int rc;
	struct ata_probe_ent *probe_ent;
	u8 tmp8;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	pci_read_config_byte(pdev, SATA_PATA_SHARING, &tmp8);
	if (tmp8 & SATA_2DEV) {
		printk(KERN_ERR DRV_NAME "(%s): SATA master/slave not supported (0x%x)\n",
		       pci_name(pdev), (int) tmp8);
		rc = -EIO;
		goto err_out_regions;
	}

	for (i = 0; i < ARRAY_SIZE(svia_bar_sizes); i++)
		if ((pci_resource_start(pdev, i) == 0) ||
		    (pci_resource_len(pdev, i) < svia_bar_sizes[i])) {
			printk(KERN_ERR DRV_NAME "(%s): invalid PCI BAR %u (sz 0x%lx, val 0x%lx)\n",
			       pci_name(pdev), i,
			       pci_resource_start(pdev, i),
			       pci_resource_len(pdev, i));
			rc = -ENODEV;
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
		printk(KERN_ERR DRV_NAME "(%s): out of memory\n",
		       pci_name(pdev));
		rc = -ENOMEM;
		goto err_out_regions;
	}
	memset(probe_ent, 0, sizeof(*probe_ent));
	INIT_LIST_HEAD(&probe_ent->node);
	probe_ent->pdev = pdev;
	probe_ent->sht = &svia_sht;
	probe_ent->host_flags = ATA_FLAG_SATA | ATA_FLAG_SRST |
				ATA_FLAG_NO_LEGACY;
	probe_ent->port_ops = &svia_sata_ops;
	probe_ent->n_ports = 2;
	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->pio_mask = 0x1f;
	probe_ent->udma_mask = 0x7f;

	probe_ent->port[0].cmd_addr = pci_resource_start(pdev, 0);
	ata_std_ports(&probe_ent->port[0]);
	probe_ent->port[0].altstatus_addr =
	probe_ent->port[0].ctl_addr =
		pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS;
	probe_ent->port[0].bmdma_addr = pci_resource_start(pdev, 4);
	probe_ent->port[0].scr_addr =
		svia_scr_addr(pci_resource_start(pdev, 5), 0);

	probe_ent->port[1].cmd_addr = pci_resource_start(pdev, 2);
	ata_std_ports(&probe_ent->port[1]);
	probe_ent->port[1].altstatus_addr =
	probe_ent->port[1].ctl_addr =
		pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS;
	probe_ent->port[1].bmdma_addr = pci_resource_start(pdev, 4) + 8;
	probe_ent->port[1].scr_addr =
		svia_scr_addr(pci_resource_start(pdev, 5), 1);

	pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &tmp8);
	printk(KERN_INFO DRV_NAME "(%s): routed to hard irq line %d\n",
	       pci_name(pdev),
	       (int) (tmp8 & 0xf0) == 0xf0 ? 0 : tmp8 & 0x0f);

	/* make sure SATA channels are enabled */
	pci_read_config_byte(pdev, SATA_CHAN_ENAB, &tmp8);
	if ((tmp8 & ENAB_ALL) != ENAB_ALL) {
		printk(KERN_DEBUG DRV_NAME "(%s): enabling SATA channels (0x%x)\n",
		       pci_name(pdev), (int) tmp8);
		tmp8 |= ENAB_ALL;
		pci_write_config_byte(pdev, SATA_CHAN_ENAB, tmp8);
	}

	/* make sure interrupts for each channel sent to us */
	pci_read_config_byte(pdev, SATA_INT_GATE, &tmp8);
	if ((tmp8 & INT_GATE_ALL) != INT_GATE_ALL) {
		printk(KERN_DEBUG DRV_NAME "(%s): enabling SATA channel interrupts (0x%x)\n",
		       pci_name(pdev), (int) tmp8);
		tmp8 |= INT_GATE_ALL;
		pci_write_config_byte(pdev, SATA_INT_GATE, tmp8);
	}

	/* make sure native mode is enabled */
	pci_read_config_byte(pdev, SATA_NATIVE_MODE, &tmp8);
	if ((tmp8 & NATIVE_MODE_ALL) != NATIVE_MODE_ALL) {
		printk(KERN_DEBUG DRV_NAME "(%s): enabling SATA channel native mode (0x%x)\n",
		       pci_name(pdev), (int) tmp8);
		tmp8 |= NATIVE_MODE_ALL;
		pci_write_config_byte(pdev, SATA_NATIVE_MODE, tmp8);
	}

	pci_set_master(pdev);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}

static int __init svia_init(void)
{
	return pci_module_init(&svia_pci_driver);
}

static void __exit svia_exit(void)
{
	pci_unregister_driver(&svia_pci_driver);
}

module_init(svia_init);
module_exit(svia_exit);


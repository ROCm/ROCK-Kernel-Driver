/*
 *  ata_k2.c - Broadcom (Apple K2) SATA
 *
 *  Copyright 2003 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 *  Bits from Jeff Garzik, Copyright RedHat, Inc.
 *
 *  This driver probably works with non-Apple versions of the
 *  Broadcom chipset...
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

#ifdef CONFIG_ALL_PPC
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif /* CONFIG_ALL_PPC */

#define DRV_NAME	"ata_k2"
#define DRV_VERSION	"1.03"


static u32 k2_sata_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;
	return readl((void *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void k2_sata_scr_write (struct ata_port *ap, unsigned int sc_reg,
			       u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;
	writel(val, (void *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void k2_sata_tf_load(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		writeb(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}
	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		writew(tf->feature | (((u16)tf->hob_feature) << 8), ioaddr->error_addr);
		writew(tf->nsect | (((u16)tf->hob_nsect) << 8), ioaddr->nsect_addr);
		writew(tf->lbal | (((u16)tf->hob_lbal) << 8), ioaddr->lbal_addr);
		writew(tf->lbam | (((u16)tf->hob_lbam) << 8), ioaddr->lbam_addr);
		writew(tf->lbah | (((u16)tf->hob_lbah) << 8), ioaddr->lbah_addr);
	} else if (is_addr) {
		writew(tf->feature, ioaddr->error_addr);
		writew(tf->nsect, ioaddr->nsect_addr);
		writew(tf->lbal, ioaddr->lbal_addr);
		writew(tf->lbam, ioaddr->lbam_addr);
		writew(tf->lbah, ioaddr->lbah_addr);
	}

	if (tf->flags & ATA_TFLAG_DEVICE)
		writeb(tf->device, ioaddr->device_addr);

	ata_wait_idle(ap);
}


static void k2_sata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u16 nsect, lbal, lbam, lbah;

	nsect = tf->nsect = readw(ioaddr->nsect_addr);
	lbal = tf->lbal = readw(ioaddr->lbal_addr);
	lbam = tf->lbam = readw(ioaddr->lbam_addr);
	lbah = tf->lbah = readw(ioaddr->lbah_addr);
	tf->device = readw(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		tf->hob_feature = readw(ioaddr->error_addr) >> 8;
		tf->hob_nsect = nsect >> 8;
		tf->hob_lbal = lbal >> 8;
		tf->hob_lbam = lbam >> 8;
		tf->hob_lbah = lbah >> 8;
        }
}


static u8 k2_stat_check_status(struct ata_port *ap)
{
       	return readl((void *) ap->ioaddr.cmdstat_addr);
}

static void k2_sata_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio)
{
	/* We need empty implementation, the core doesn't test for NULL
	 * function pointer
	 */
}


static void k2_sata_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma)
{
	/* We need empty implementation, the core doesn't test for NULL
	 * function pointer
	 */
}


#ifdef CONFIG_ALL_PPC
/*
 * k2_sata_proc_info
 * inout : decides on the direction of the dataflow and the meaning of the
 *	   variables
 * buffer: If inout==FALSE data is being written to it else read from it
 * *start: If inout==FALSE start of the valid data in the buffer
 * offset: If inout==FALSE offset from the beginning of the imaginary file
 *	   from which we start writing into the buffer
 * length: If inout==FALSE max number of bytes to be written into the buffer
 *	   else number of bytes in the buffer
 */
static int k2_sata_proc_info(char *page, char **start, off_t offset, int count,
		   int hostno, int inout)
{
	struct Scsi_Host *hpnt;
	struct ata_port *ap;
	struct device_node *np;
	int len, index;

	/* Find ourself. That's locking-broken, shitty etc... but thanks to
	 * /proc/scsi interface and lack of state kept around in this driver,
	 * its best I want to do for now...
	 */
	hpnt = scsi_hostlist;
	while (hpnt) {
		if (hostno == hpnt->host_no)
			break;
		hpnt = hpnt->next;
	}
	if (!hpnt)
		return 0;

	/* Find  the ata_port */
	ap = (struct ata_port *) &hpnt->hostdata[0];
	if (ap == NULL)
		return 0;

	/* Find the OF node for the PCI device proper */
	np = pci_device_to_OF_node(ap->host_set->pdev);
	if (np == NULL)
		return 0;

	/* Match it to a port node */
	index = (ap == ap->host_set->ports[0]) ? 0 : 1;
	for (np = np->child; np != NULL; np = np->sibling) {
		u32 *reg = (u32 *)get_property(np, "reg", NULL);
		if (!reg)
			continue;
		if (index == *reg)
			break;
	}
	if (np == NULL)
		return 0;

	len = sprintf(page, "devspec: %s\n", np->full_name);

	return len;
}
#endif /* CONFIG_ALL_PPC */


static Scsi_Host_Template k2_sata_sht = {
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
#ifdef CONFIG_ALL_PPC
	.proc_info		= k2_sata_proc_info
#endif
};


static struct ata_port_operations k2_sata_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= k2_sata_set_piomode,
	.set_udmamode		= k2_sata_set_udmamode,
	.tf_load		= k2_sata_tf_load,
	.tf_read		= k2_sata_tf_read,
	.check_status		= k2_stat_check_status,
	.exec_command		= ata_exec_command_mmio,
	.phy_reset		= sata_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */
	.bmdma_start            = ata_bmdma_start_mmio,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= ata_eng_timeout,
	.irq_handler		= ata_interrupt,
	.scr_read		= k2_sata_scr_read,
	.scr_write		= k2_sata_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};


static void k2_sata_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr = base;
	port->data_addr = base;
	port->error_addr = base + 0x4;
	port->nsect_addr = base + 0x8;
	port->lbal_addr = base + 0xc;
	port->lbam_addr = base + 0x10;
	port->lbah_addr = base + 0x14;
	port->device_addr = base + 0x18;
	port->cmdstat_addr = base + 0x1c;
	port->ctl_addr = base + 0x20;
	port->bmdma_addr = base + 0x30;
	port->scr_addr = base + 0x40;
}


static int k2_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
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
	probe_ent->pdev = pdev;
	INIT_LIST_HEAD(&probe_ent->node);

	mmio_base = ioremap(pci_resource_start(pdev, 5),
		            pci_resource_len(pdev, 5));
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	/*
	 * Check for the "disabled" second function to avoid registering
	 * useless interfaces on K2
	 */
	if (readl(mmio_base + 0x40)  == 0xffffffffUL &&
	    readl(mmio_base + 0x140) == 0xffffffffUL) {
		rc = -ENODEV;
		goto err_out_unmap;
	}

	/* Clear a magic bit in SCR1 according to Darwin, those help
	 * some funky seagate drives (though so far, those were already
	 * set by the firmware on the machines I had access to
	 */
	writel(readl(mmio_base + 0x80) & ~0x00040000, mmio_base + 0x80);

	/* Clear SATA error & interrupts we don't use */
	writel(0xffffffff, mmio_base + 0x44);
	writel(0x0, mmio_base + 0x88);

	probe_ent->sht = &k2_sata_sht;
	probe_ent->host_flags = ATA_FLAG_SATA | ATA_FLAG_SATA_RESET |
				ATA_FLAG_NO_LEGACY | ATA_FLAG_MMIO;
	probe_ent->port_ops = &k2_sata_ops;
	probe_ent->n_ports = 2;
	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;

	/*
	 * We don't care much about the PIO/UDMA masks, but the core won't like us
	 * if we don't fill these
	 */
	probe_ent->pio_mask = 0x1f;
	probe_ent->udma_mask = 0x7f;

	k2_sata_setup_port(&probe_ent->port[0], base);
	k2_sata_setup_port(&probe_ent->port[1], base + 0x100);

	pci_set_master(pdev);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_unmap:
	iounmap((void *)base);
err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}


static struct pci_device_id k2_sata_pci_tbl[] = {
	{ 0x1166, 0x0240, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ }
};


static struct pci_driver k2_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= k2_sata_pci_tbl,
	.probe			= k2_sata_init_one,
	.remove			= ata_pci_remove_one,
};


static int __init k2_sata_init(void)
{
	int rc;

	rc = pci_module_init(&k2_sata_pci_driver);
	if (rc)
		return rc;

	return 0;
}


static void __exit k2_sata_exit(void)
{
	pci_unregister_driver(&k2_sata_pci_driver);
}


MODULE_AUTHOR("Benjamin Herrenschmidt");
MODULE_DESCRIPTION("low-level driver for K2 SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, k2_sata_pci_tbl);

module_init(k2_sata_init);
module_exit(k2_sata_exit);

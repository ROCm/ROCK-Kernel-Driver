/*
 *  sata_promise.c - Promise SATA
 *
 *  Copyright 2003 Red Hat, Inc.
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

#define DRV_NAME	"sata_promise"
#define DRV_VERSION	"0.83"


enum {
	PDC_PRD_TBL		= 0x44,	/* Direct command DMA table addr */

	PDC_INT_SEQMASK		= 0x40,	/* Mask of asserted SEQ INTs */
	PDC_TBG_MODE		= 0x41,	/* TBG mode */
	PDC_FLASH_CTL		= 0x44, /* Flash control register */
	PDC_CTLSTAT		= 0x60,	/* IDE control and status register */
	PDC_SATA_PLUG_CSR	= 0x6C, /* SATA Plug control/status reg */
	PDC_SLEW_CTL		= 0x470, /* slew rate control reg */
	PDC_20621_SEQCTL	= 0x400,
	PDC_20621_SEQMASK	= 0x480,

	PDC_CHIP0_OFS		= 0xC0000, /* offset of chip #0 */

	board_2037x		= 0,	/* FastTrak S150 TX2plus */
	board_20319		= 1,	/* FastTrak S150 TX4 */
	board_20621		= 2,	/* FastTrak S150 SX4 */

	PDC_FLAG_20621		= (1 << 30), /* we have a 20621 */
};


static u32 pdc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void pdc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static void pdc_sata_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio);
static void pdc_sata_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma);
static int pdc_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void pdc_dma_start(struct ata_queued_cmd *qc);
static irqreturn_t pdc_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static void pdc_eng_timeout(struct ata_port *ap);
static void pdc_20621_phy_reset (struct ata_port *ap);


static Scsi_Host_Template pdc_sata_sht = {
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

static struct ata_port_operations pdc_sata_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= pdc_sata_set_piomode,
	.set_udmamode		= pdc_sata_set_udmamode,
	.tf_load		= ata_tf_load_mmio,
	.tf_read		= ata_tf_read_mmio,
	.check_status		= ata_check_status_mmio,
	.exec_command		= ata_exec_command_mmio,
	.phy_reset		= sata_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */
	.bmdma_start            = pdc_dma_start,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= pdc_eng_timeout,
	.irq_handler		= pdc_interrupt,
	.scr_read		= pdc_sata_scr_read,
	.scr_write		= pdc_sata_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_operations pdc_20621_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= pdc_sata_set_piomode,
	.set_udmamode		= pdc_sata_set_udmamode,
	.tf_load		= ata_tf_load_mmio,
	.tf_read		= ata_tf_read_mmio,
	.check_status		= ata_check_status_mmio,
	.exec_command		= ata_exec_command_mmio,
	.phy_reset		= pdc_20621_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */
	.bmdma_start            = pdc_dma_start,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= pdc_eng_timeout,
	.irq_handler		= pdc_interrupt,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_info pdc_port_info[] = {
	/* board_2037x */
	{
		.sht		= &pdc_sata_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_SRST | ATA_FLAG_MMIO,
		.pio_mask	= 0x03, /* pio3-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},

	/* board_20319 */
	{
		.sht		= &pdc_sata_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_SRST | ATA_FLAG_MMIO,
		.pio_mask	= 0x03, /* pio3-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},

	/* board_20621 */
	{
		.sht		= &pdc_sata_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_SRST | ATA_FLAG_MMIO |
				  PDC_FLAG_20621,
		.pio_mask	= 0x03, /* pio3-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_20621_ops,
	},

};

static struct pci_device_id pdc_sata_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, 0x3371, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3375, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3318, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
	{ PCI_VENDOR_ID_PROMISE, 0x3319, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
#if 0 /* broken currently */
	{ PCI_VENDOR_ID_PROMISE, 0x6622, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20621 },
#endif
	{ }	/* terminate list */
};


static struct pci_driver pdc_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= pdc_sata_pci_tbl,
	.probe			= pdc_sata_init_one,
	.remove			= ata_pci_remove_one,
};


static void pdc_20621_phy_reset (struct ata_port *ap)
{
	VPRINTK("ENTER\n");
        ap->cbl = ATA_CBL_SATA;
        ata_port_probe(ap);
        ata_bus_reset(ap);
}

static u32 pdc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;
	return readl((void *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void pdc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg,
			       u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;
	writel(val, (void *) ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void pdc_sata_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio)
{
	/* dummy */
}


static void pdc_sata_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma)
{
	/* dummy */
}

enum pdc_packet_bits {
	PDC_PKT_READ		= (1 << 2),
	PDC_PKT_NODATA		= (1 << 3),

	PDC_PKT_SIZEMASK	= (1 << 7) | (1 << 6) | (1 << 5),
	PDC_PKT_CLEAR_BSY	= (1 << 4),
	PDC_PKT_WAIT_DRDY	= (1 << 3) | (1 << 4),
	PDC_LAST_REG		= (1 << 3),

	PDC_REG_DEVCTL		= (1 << 3) | (1 << 2) | (1 << 1),
};

static inline void pdc_pkt_header(struct ata_taskfile *tf, dma_addr_t sg_table,
				  unsigned int devno, u8 *buf)
{
	u8 dev_reg;
	u32 *buf32 = (u32 *) buf;

	/* set control bits (byte 0), zero delay seq id (byte 3),
	 * and seq id (byte 2)
	 */
	switch (tf->protocol) {
	case ATA_PROT_DMA_READ:
		buf32[0] = cpu_to_le32(PDC_PKT_READ);
		break;

	case ATA_PROT_DMA_WRITE:
		buf32[0] = 0;
		break;

	case ATA_PROT_NODATA:
		buf32[0] = cpu_to_le32(PDC_PKT_NODATA);
		break;

	default:
		BUG();
		break;
	}

	buf32[1] = cpu_to_le32(sg_table);	/* S/G table addr */
	buf32[2] = 0;				/* no next-packet */

	if (devno == 0)
		dev_reg = ATA_DEVICE_OBS;
	else
		dev_reg = ATA_DEVICE_OBS | ATA_DEV1;

	/* select device */
	buf[12] = (1 << 5) | PDC_PKT_CLEAR_BSY | ATA_REG_DEVICE;
	buf[13] = dev_reg;

	/* device control register */
	buf[14] = (1 << 5) | PDC_REG_DEVCTL;
	buf[15] = tf->ctl;
}

static inline void pdc_pkt_footer(struct ata_taskfile *tf, u8 *buf,
				  unsigned int i)
{
	if (tf->flags & ATA_TFLAG_DEVICE) {
		buf[i++] = (1 << 5) | ATA_REG_DEVICE;
		buf[i++] = tf->device;
	}

	/* and finally the command itself; also includes end-of-pkt marker */
	buf[i++] = (1 << 5) | PDC_LAST_REG | ATA_REG_CMD;
	buf[i++] = tf->command;
}

static void pdc_prep_lba28(struct ata_taskfile *tf, dma_addr_t sg_table,
			   unsigned int devno, u8 *buf)
{
	unsigned int i;

	pdc_pkt_header(tf, sg_table, devno, buf);

	/* the "(1 << 5)" should be read "(count << 5)" */

	i = 16;

	/* ATA command block registers */
	buf[i++] = (1 << 5) | ATA_REG_FEATURE;
	buf[i++] = tf->feature;

	buf[i++] = (1 << 5) | ATA_REG_NSECT;
	buf[i++] = tf->nsect;

	buf[i++] = (1 << 5) | ATA_REG_LBAL;
	buf[i++] = tf->lbal;

	buf[i++] = (1 << 5) | ATA_REG_LBAM;
	buf[i++] = tf->lbam;

	buf[i++] = (1 << 5) | ATA_REG_LBAH;
	buf[i++] = tf->lbah;

	pdc_pkt_footer(tf, buf, i);
}

static void pdc_prep_lba48(struct ata_taskfile *tf, dma_addr_t sg_table,
			   unsigned int devno, u8 *buf)
{
	unsigned int i;

	pdc_pkt_header(tf, sg_table, devno, buf);

	/* the "(2 << 5)" should be read "(count << 5)" */

	i = 16;

	/* ATA command block registers */
	buf[i++] = (2 << 5) | ATA_REG_FEATURE;
	buf[i++] = tf->hob_feature;
	buf[i++] = tf->feature;

	buf[i++] = (2 << 5) | ATA_REG_NSECT;
	buf[i++] = tf->hob_nsect;
	buf[i++] = tf->nsect;

	buf[i++] = (2 << 5) | ATA_REG_LBAL;
	buf[i++] = tf->hob_lbal;
	buf[i++] = tf->lbal;

	buf[i++] = (2 << 5) | ATA_REG_LBAM;
	buf[i++] = tf->hob_lbam;
	buf[i++] = tf->lbam;

	buf[i++] = (2 << 5) | ATA_REG_LBAH;
	buf[i++] = tf->hob_lbah;
	buf[i++] = tf->lbah;

	pdc_pkt_footer(tf, buf, i);
}

static inline void __pdc_dma_complete (struct ata_port *ap,
                                       struct ata_queued_cmd *qc)
{
	void *dmactl = (void *) ap->ioaddr.cmd_addr + PDC_CTLSTAT;
	u32 val;

	/* clear DMA start/stop bit (bit 7) */
	val = readl(dmactl);
	writel(val & ~(1 << 7), dmactl);

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_altstatus(ap);              /* dummy read */
}

static inline void pdc_dma_complete (struct ata_port *ap,
                                     struct ata_queued_cmd *qc)
{
	__pdc_dma_complete(ap, qc);

	/* get drive status; clear intr; complete txn */
	ata_qc_complete(ata_qc_from_tag(ap, ap->active_tag),
			ata_wait_idle(ap), 0);
}

static void pdc_eng_timeout(struct ata_port *ap)
{
	u8 drv_stat;
	struct ata_queued_cmd *qc;

	DPRINTK("ENTER\n");

	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (!qc) {
		printk(KERN_ERR "ata%u: BUG: timeout without command\n",
		       ap->id);
		goto out;
	}

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA_READ:
	case ATA_PROT_DMA_WRITE:
		printk(KERN_ERR "ata%u: DMA timeout\n", ap->id);
		__pdc_dma_complete(ap, qc);
		ata_qc_complete(ata_qc_from_tag(ap, ap->active_tag),
			        ata_wait_idle(ap) | ATA_ERR, 0);
		break;

	case ATA_PROT_NODATA:
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

		printk(KERN_ERR "ata%u: command 0x%x timeout, stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat);

		ata_qc_complete(qc, drv_stat, 1);
		break;

	default:
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

		printk(KERN_ERR "ata%u: unknown timeout, cmd 0x%x stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat);

		ata_qc_complete(qc, drv_stat, 1);
		break;
	}

out:
	DPRINTK("EXIT\n");
}

static inline unsigned int pdc_host_intr( struct ata_port *ap,
                                          struct ata_queued_cmd *qc)
{
	u8 status;
	unsigned int handled = 0;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA_READ:
	case ATA_PROT_DMA_WRITE:
		pdc_dma_complete(ap, qc);
		handled = 1;
		break;

	case ATA_PROT_NODATA:   /* command completion, but no data xfer */
		status = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);
		DPRINTK("BUS_NODATA (drv_stat 0x%X)\n", status);
		ata_qc_complete(qc, status, 0);
		handled = 1;
		break;

        default:
                ap->stats.idle_irq++;
                break;
        }

        return handled;
}

static irqreturn_t pdc_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct ata_port *ap;
	u32 mask = 0;
	unsigned int i, tmp;
	unsigned int handled = 0, have_20621 = 0;
	void *mmio_base;

	VPRINTK("ENTER\n");

	if (!host_set || !host_set->mmio_base) {
		VPRINTK("QUICK EXIT\n");
		return IRQ_NONE;
	}

	mmio_base = host_set->mmio_base;

	for (i = 0; i < host_set->n_ports; i++) {
		ap = host_set->ports[i];
		if (ap && (ap->flags & PDC_FLAG_20621)) {
			have_20621 = 1;
			break;
		}
	}

	/* reading should also clear interrupts */
	if (have_20621) {
		mmio_base += PDC_CHIP0_OFS;
		mask = readl(mmio_base + PDC_20621_SEQMASK);
	} else {
		mask = readl(mmio_base + PDC_INT_SEQMASK);
	}

	if (mask == 0xffffffff) {
		VPRINTK("QUICK EXIT 2\n");
		return IRQ_NONE;
	}
	mask &= 0xf;		/* only 16 tags possible */
	if (!mask) {
		VPRINTK("QUICK EXIT 3\n");
		return IRQ_NONE;
	}

        spin_lock_irq(&host_set->lock);

        for (i = 0; i < host_set->n_ports; i++) {
		VPRINTK("port %u\n", i);
		ap = host_set->ports[i];
		tmp = mask & (1 << (i + 1));
		if (tmp && ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED))) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && ((qc->flags & ATA_QCFLAG_POLL) == 0))
				handled += pdc_host_intr(ap, qc);
		}
	}

        spin_unlock_irq(&host_set->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static void pdc_dma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_host_set *host_set = ap->host_set;
	unsigned int port_no = ap->port_no;
	void *mmio = host_set->mmio_base;
	void *dmactl = (void *) ap->ioaddr.cmd_addr + PDC_CTLSTAT;
	unsigned int rw = (qc->flags & ATA_QCFLAG_WRITE);
	u32 val;
	u8 seq = (u8) (port_no + 1);

	wmb();	/* flush writes made to PRD table in DMA memory */

	if (ap->flags & PDC_FLAG_20621)
		mmio += PDC_CHIP0_OFS;

	VPRINTK("ENTER, ap %p, mmio %p\n", ap, mmio);

	/* indicate where our S/G table is to chip */
	writel(ap->prd_dma, (void *) ap->ioaddr.cmd_addr + PDC_PRD_TBL);

	/* clear dma start bit (paranoia), clear intr seq id (paranoia),
	 * set DMA direction (bit 6 == from chip -> drive)
	 */
	val = readl(dmactl);
	VPRINTK("val == %x\n", val);
	val &= ~(1 << 7);	/* clear dma start/stop bit */
	if (rw)			/* set/clear dma direction bit */
		val |= (1 << 6);
	else
		val &= ~(1 << 6);
	if (qc->tf.ctl & ATA_NIEN) /* set/clear irq-mask bit */
		val |= (1 << 10);
	else
		val &= ~(1 << 10);
	writel(val, dmactl);
	val = readl(dmactl);
	VPRINTK("val == %x\n", val);

	/* FIXME: clear any intr status bits here? */

	ata_exec_command_mmio(ap, &qc->tf);

	VPRINTK("FIVE\n");
	if (ap->flags & PDC_FLAG_20621)
		writel(0x00000001, mmio + PDC_20621_SEQCTL + (seq * 4));
	else
		writel(0x00000001, mmio + (seq * 4));

	/* start host DMA transaction */
	writel(val | seq | (1 << 7), dmactl);
}

static void pdc_sata_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr		= base;
	port->data_addr		= base;
	port->error_addr	= base + 0x4;
	port->nsect_addr	= base + 0x8;
	port->lbal_addr		= base + 0xc;
	port->lbam_addr		= base + 0x10;
	port->lbah_addr		= base + 0x14;
	port->device_addr	= base + 0x18;
	port->cmdstat_addr	= base + 0x1c;
	port->ctl_addr		= base + 0x38;
}

static void pdc_20621_init(struct ata_probe_ent *pe)
{
}

static void pdc_host_init(unsigned int chip_id, struct ata_probe_ent *pe)
{
	void *mmio = pe->mmio_base;
	u32 tmp;

	if (chip_id == board_20621)
		return;

	/* change FIFO_SHD to 8 dwords. Promise driver does this...
	 * dunno why.
	 */
	tmp = readl(mmio + PDC_FLASH_CTL);
	if ((tmp & (1 << 16)) == 0)
		writel(tmp | (1 << 16), mmio + PDC_FLASH_CTL);

	/* clear plug/unplug flags for all ports */
	tmp = readl(mmio + PDC_SATA_PLUG_CSR);
	writel(tmp | 0xff, mmio + PDC_SATA_PLUG_CSR);

	/* mask plug/unplug ints */
	tmp = readl(mmio + PDC_SATA_PLUG_CSR);
	writel(tmp | 0xff0000, mmio + PDC_SATA_PLUG_CSR);

	/* reduce TBG clock to 133 Mhz. FIXME: why? */
	tmp = readl(mmio + PDC_TBG_MODE);
	tmp &= ~0x30000; /* clear bit 17, 16*/
	tmp |= 0x10000;  /* set bit 17:16 = 0:1 */
	writel(tmp, mmio + PDC_TBG_MODE);

	/* adjust slew rate control register. FIXME: why? */
	tmp = readl(mmio + PDC_SLEW_CTL);
	tmp &= 0xFFFFF03F; /* clear bit 11 ~ 6 */
	tmp  |= 0x00000900; /* set bit 11-9 = 100b , bit 8-6 = 100 */
	writel(tmp, mmio + PDC_SLEW_CTL);
}

static int pdc_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	unsigned long base;
	void *mmio_base;
	unsigned int board_idx = (unsigned int) ent->driver_data;
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

	mmio_base = ioremap(pci_resource_start(pdev, 3),
		            pci_resource_len(pdev, 3));
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	probe_ent->sht		= pdc_port_info[board_idx].sht;
	probe_ent->host_flags	= pdc_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= pdc_port_info[board_idx].pio_mask;
	probe_ent->udma_mask	= pdc_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= pdc_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;

	if (board_idx == board_20621)
		base += PDC_CHIP0_OFS;

	pdc_sata_setup_port(&probe_ent->port[0], base + 0x200);
	probe_ent->port[0].scr_addr = base + 0x400;

	pdc_sata_setup_port(&probe_ent->port[1], base + 0x280);
	probe_ent->port[1].scr_addr = base + 0x500;

	/* notice 4-port boards */
	switch (board_idx) {
	case board_20319:
	case board_20621:
       		probe_ent->n_ports = 4;

		pdc_sata_setup_port(&probe_ent->port[2], base + 0x300);
		probe_ent->port[2].scr_addr = base + 0x600;

		pdc_sata_setup_port(&probe_ent->port[3], base + 0x380);
		probe_ent->port[3].scr_addr = base + 0x700;
		break;
	case board_2037x:
       		probe_ent->n_ports = 2;
		break;
	default:
		BUG();
		break;
	}

	pci_set_master(pdev);

	/* initialize adapter */
	switch (board_idx) {
	case board_20621:
		pdc_20621_init(probe_ent);
		break;

	default:
		pdc_host_init(board_idx, probe_ent);
		break;
	}

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



static int __init pdc_sata_init(void)
{
	int rc;

	rc = pci_module_init(&pdc_sata_pci_driver);
	if (rc)
		return rc;

	return 0;
}


static void __exit pdc_sata_exit(void)
{
	pci_unregister_driver(&pdc_sata_pci_driver);
}


MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Promise SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pdc_sata_pci_tbl);

module_init(pdc_sata_init);
module_exit(pdc_sata_exit);

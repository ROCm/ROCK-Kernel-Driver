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
#include <asm/io.h>

#undef DIRECT_HDMA

#define DRV_NAME	"sata_promise"
#define DRV_VERSION	"0.86"


enum {
	PDC_PRD_TBL		= 0x44,	/* Direct command DMA table addr */

	PDC_PKT_SUBMIT		= 0x40, /* Command packet pointer addr */
	PDC_HDMA_PKT_SUBMIT	= 0x100, /* Host DMA packet pointer addr */
	PDC_INT_SEQMASK		= 0x40,	/* Mask of asserted SEQ INTs */
	PDC_TBG_MODE		= 0x41,	/* TBG mode */
	PDC_FLASH_CTL		= 0x44, /* Flash control register */
	PDC_CTLSTAT		= 0x60,	/* IDE control and status register */
	PDC_SATA_PLUG_CSR	= 0x6C, /* SATA Plug control/status reg */
	PDC_SLEW_CTL		= 0x470, /* slew rate control reg */
	PDC_HDMA_CTLSTAT	= 0x12C, /* Host DMA control / status */
	PDC_20621_SEQCTL	= 0x400,
	PDC_20621_SEQMASK	= 0x480,
	PDC_20621_GENERAL_CTL	= 0x484,
	PDC_20621_PAGE_SIZE	= (32 * 1024),

	/* chosen, not constant, values; we design our own DIMM mem map */
	PDC_20621_DIMM_WINDOW	= 0x0C,	/* page# for 32K DIMM window */
	PDC_20621_DIMM_BASE	= 0x00200000,
	PDC_20621_DIMM_DATA	= (64 * 1024),
	PDC_DIMM_DATA_STEP	= (256 * 1024),
	PDC_DIMM_WINDOW_STEP	= (8 * 1024),
	PDC_DIMM_HOST_PRD	= (6 * 1024),
	PDC_DIMM_HOST_PKT	= (128 * 0),
	PDC_DIMM_HPKT_PRD	= (128 * 1),
	PDC_DIMM_ATA_PKT	= (128 * 2),
	PDC_DIMM_APKT_PRD	= (128 * 3),
	PDC_DIMM_HEADER_SZ	= PDC_DIMM_APKT_PRD + 128,
	PDC_PAGE_WINDOW		= 0x40,
	PDC_PAGE_DATA		= PDC_PAGE_WINDOW +
				  (PDC_20621_DIMM_DATA / PDC_20621_PAGE_SIZE),
	PDC_PAGE_SET		= PDC_DIMM_DATA_STEP / PDC_20621_PAGE_SIZE,

	PDC_CHIP0_OFS		= 0xC0000, /* offset of chip #0 */

	board_2037x		= 0,	/* FastTrak S150 TX2plus */
	board_20319		= 1,	/* FastTrak S150 TX4 */
	board_20621		= 2,	/* FastTrak S150 SX4 */

	PDC_FLAG_20621		= (1 << 30), /* we have a 20621 */
	PDC_HDMA_RESET		= (1 << 11), /* HDMA reset */
};


struct pdc_port_priv {
	u8			dimm_buf[(ATA_PRD_SZ * ATA_MAX_PRD) + 512];
	u8			*pkt;
	dma_addr_t		pkt_dma;
};


static u32 pdc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void pdc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static void pdc_sata_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio);
static void pdc_sata_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma);
static int pdc_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void pdc_dma_start(struct ata_queued_cmd *qc);
static void pdc20621_dma_start(struct ata_queued_cmd *qc);
static irqreturn_t pdc_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static irqreturn_t pdc20621_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static void pdc_eng_timeout(struct ata_port *ap);
static void pdc_20621_phy_reset (struct ata_port *ap);
static int pdc_port_start(struct ata_port *ap);
static void pdc_port_stop(struct ata_port *ap);
static void pdc_fill_sg(struct ata_queued_cmd *qc);
static void pdc20621_fill_sg(struct ata_queued_cmd *qc);
static void pdc_tf_load_mmio(struct ata_port *ap, struct ata_taskfile *tf);
static void pdc_exec_command_mmio(struct ata_port *ap, struct ata_taskfile *tf);
static void pdc20621_host_stop(struct ata_host_set *host_set);
static inline void pdc_dma_complete (struct ata_port *ap,
                                     struct ata_queued_cmd *qc);


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
	.tf_load		= pdc_tf_load_mmio,
	.tf_read		= ata_tf_read_mmio,
	.check_status		= ata_check_status_mmio,
	.exec_command		= pdc_exec_command_mmio,
	.phy_reset		= sata_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */
	.bmdma_start            = pdc_dma_start,
	.fill_sg		= pdc_fill_sg,
	.eng_timeout		= pdc_eng_timeout,
	.irq_handler		= pdc_interrupt,
	.scr_read		= pdc_sata_scr_read,
	.scr_write		= pdc_sata_scr_write,
	.port_start		= pdc_port_start,
	.port_stop		= pdc_port_stop,
};

static struct ata_port_operations pdc_20621_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= pdc_sata_set_piomode,
	.set_udmamode		= pdc_sata_set_udmamode,
	.tf_load		= pdc_tf_load_mmio,
	.tf_read		= ata_tf_read_mmio,
	.check_status		= ata_check_status_mmio,
	.exec_command		= pdc_exec_command_mmio,
	.phy_reset		= pdc_20621_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */
	.bmdma_start            = pdc20621_dma_start,
	.fill_sg		= pdc20621_fill_sg,
	.eng_timeout		= pdc_eng_timeout,
	.irq_handler		= pdc20621_interrupt,
	.port_start		= pdc_port_start,
	.port_stop		= pdc_port_stop,
	.host_stop		= pdc20621_host_stop,
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
	{ PCI_VENDOR_ID_PROMISE, 0x3373, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3375, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3376, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3318, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
	{ PCI_VENDOR_ID_PROMISE, 0x3319, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
	{ PCI_VENDOR_ID_PROMISE, 0x6622, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20621 },
	{ }	/* terminate list */
};


static struct pci_driver pdc_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= pdc_sata_pci_tbl,
	.probe			= pdc_sata_init_one,
	.remove			= ata_pci_remove_one,
};


static void pdc20621_host_stop(struct ata_host_set *host_set)
{
	void *mmio = host_set->private_data;

	assert(mmio != NULL);
	iounmap(mmio);
}

static int pdc_port_start(struct ata_port *ap)
{
	struct pci_dev *pdev = ap->host_set->pdev;
	struct pdc_port_priv *pp;
	int rc;

	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp = kmalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_out;
	}

	pp->pkt = pci_alloc_consistent(pdev, 128, &pp->pkt_dma);
	if (!pp->pkt) {
		rc = -ENOMEM;
		goto err_out_kfree;
	}

	ap->private_data = pp;

	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}


static void pdc_port_stop(struct ata_port *ap)
{
	struct pci_dev *pdev = ap->host_set->pdev;
	struct pdc_port_priv *pp = ap->private_data;

	ap->private_data = NULL;
	pci_free_consistent(pdev, 128, pp->pkt, pp->pkt_dma);
	kfree(pp);
	ata_port_stop(ap);
}


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

static inline unsigned int pdc_pkt_header(struct ata_taskfile *tf,
					  dma_addr_t sg_table,
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

	return 16; 	/* offset of next byte */
}

static inline unsigned int pdc_pkt_footer(struct ata_taskfile *tf, u8 *buf,
				  unsigned int i)
{
	if (tf->flags & ATA_TFLAG_DEVICE) {
		buf[i++] = (1 << 5) | ATA_REG_DEVICE;
		buf[i++] = tf->device;
	}

	/* and finally the command itself; also includes end-of-pkt marker */
	buf[i++] = (1 << 5) | PDC_LAST_REG | ATA_REG_CMD;
	buf[i++] = tf->command;

	return i;
}

static inline unsigned int pdc_prep_lba28(struct ata_taskfile *tf, u8 *buf, unsigned int i)
{
	/* the "(1 << 5)" should be read "(count << 5)" */

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

	return i;
}

static inline unsigned int pdc_prep_lba48(struct ata_taskfile *tf, u8 *buf, unsigned int i)
{
	/* the "(2 << 5)" should be read "(count << 5)" */

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

	return i;
}

static inline void pdc20621_ata_sg(struct ata_taskfile *tf, u8 *buf,
				    	   unsigned int portno,
					   unsigned int total_len)
{
	u32 addr;
	unsigned int dw = PDC_DIMM_APKT_PRD >> 2;
	u32 *buf32 = (u32 *) buf;

	/* output ATA packet S/G table */
	addr = PDC_20621_DIMM_BASE + PDC_20621_DIMM_DATA +
	       (PDC_DIMM_DATA_STEP * portno);
	VPRINTK("ATA sg addr 0x%x, %d\n", addr, addr);
	buf32[dw] = cpu_to_le32(addr);
	buf32[dw + 1] = cpu_to_le32(total_len | ATA_PRD_EOT);

	VPRINTK("ATA PSG @ %x == (0x%x, 0x%x)\n",
		PDC_20621_DIMM_BASE +
		       (PDC_DIMM_WINDOW_STEP * portno) +
		       PDC_DIMM_APKT_PRD,
		buf32[dw], buf32[dw + 1]);
}

static inline void pdc20621_host_sg(struct ata_taskfile *tf, u8 *buf,
				    	    unsigned int portno,
					    unsigned int total_len)
{
	u32 addr;
	unsigned int dw = PDC_DIMM_HPKT_PRD >> 2;
	u32 *buf32 = (u32 *) buf;

	/* output Host DMA packet S/G table */
	addr = PDC_20621_DIMM_BASE + PDC_20621_DIMM_DATA +
	       (PDC_DIMM_DATA_STEP * portno);

	buf32[dw] = cpu_to_le32(addr);
	buf32[dw + 1] = cpu_to_le32(total_len | ATA_PRD_EOT);

	VPRINTK("HOST PSG @ %x == (0x%x, 0x%x)\n",
		PDC_20621_DIMM_BASE +
		       (PDC_DIMM_WINDOW_STEP * portno) +
		       PDC_DIMM_HPKT_PRD,
		buf32[dw], buf32[dw + 1]);
}

static inline unsigned int pdc20621_ata_pkt(struct ata_taskfile *tf,
					    unsigned int devno, u8 *buf,
					    unsigned int portno)
{
	unsigned int i, dw;
	u32 *buf32 = (u32 *) buf;
	u8 dev_reg;

	unsigned int dimm_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * portno) +
			       PDC_DIMM_APKT_PRD;
	VPRINTK("ENTER, dimm_sg == 0x%x, %d\n", dimm_sg, dimm_sg);

	i = PDC_DIMM_ATA_PKT;

	/*
	 * Set up ATA packet
	 */
	if (tf->protocol == ATA_PROT_DMA_READ)
		buf[i++] = PDC_PKT_READ;
	else if (tf->protocol == ATA_PROT_NODATA)
		buf[i++] = PDC_PKT_NODATA;
	else
		buf[i++] = 0;
	buf[i++] = 0;			/* reserved */
	buf[i++] = portno + 1;		/* seq. id */
	buf[i++] = 0xff;		/* delay seq. id */

	/* dimm dma S/G, and next-pkt */
	dw = i >> 2;
	buf32[dw] = cpu_to_le32(dimm_sg);
	buf32[dw + 1] = 0;
	i += 8;

	if (devno == 0)
		dev_reg = ATA_DEVICE_OBS;
	else
		dev_reg = ATA_DEVICE_OBS | ATA_DEV1;

	/* select device */
	buf[i++] = (1 << 5) | PDC_PKT_CLEAR_BSY | ATA_REG_DEVICE;
	buf[i++] = dev_reg;

	/* device control register */
	buf[i++] = (1 << 5) | PDC_REG_DEVCTL;
	buf[i++] = tf->ctl;

	return i;
}

static inline void pdc20621_host_pkt(struct ata_taskfile *tf, u8 *buf,
				     unsigned int portno)
{
	unsigned int dw;
	u32 tmp, *buf32 = (u32 *) buf;

	unsigned int host_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * portno) +
			       PDC_DIMM_HOST_PRD;
	unsigned int dimm_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * portno) +
			       PDC_DIMM_HPKT_PRD;
	VPRINTK("ENTER, dimm_sg == 0x%x, %d\n", dimm_sg, dimm_sg);
	VPRINTK("host_sg == 0x%x, %d\n", host_sg, host_sg);

	dw = PDC_DIMM_HOST_PKT >> 2;

	/*
	 * Set up Host DMA packet
	 */
	if (tf->protocol == ATA_PROT_DMA_READ)
		tmp = PDC_PKT_READ;
	else
		tmp = 0;
	tmp |= ((portno + 1 + 4) << 16);	/* seq. id */
	tmp |= (0xff << 24);			/* delay seq. id */
	buf32[dw + 0] = cpu_to_le32(tmp);
	buf32[dw + 1] = cpu_to_le32(host_sg);
	buf32[dw + 2] = cpu_to_le32(dimm_sg);
	buf32[dw + 3] = 0;

	VPRINTK("HOST PKT @ %x == (0x%x 0x%x 0x%x 0x%x)\n",
		PDC_20621_DIMM_BASE + (PDC_DIMM_WINDOW_STEP * portno) +
			PDC_DIMM_HOST_PKT,
		buf32[dw + 0],
		buf32[dw + 1],
		buf32[dw + 2],
		buf32[dw + 3]);
}

static void pdc20621_fill_sg(struct ata_queued_cmd *qc)
{
	struct scatterlist *sg = qc->sg;
	struct ata_port *ap = qc->ap;
	struct pdc_port_priv *pp = ap->private_data;
	void *mmio = ap->host_set->mmio_base;
	void *dimm_mmio = ap->host_set->private_data;
	unsigned int portno = ap->port_no;
	unsigned int i, last, idx, total_len = 0, sgt_len;
	u32 *buf = (u32 *) &pp->dimm_buf[PDC_DIMM_HEADER_SZ];

	VPRINTK("ata%u: ENTER\n", ap->id);

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	/*
	 * Build S/G table
	 */
	last = qc->n_elem;
	idx = 0;
	for (i = 0; i < last; i++) {
		buf[idx++] = cpu_to_le32(sg[i].dma_address);
		buf[idx++] = cpu_to_le32(sg[i].length);
		total_len += sg[i].length;
	}
	buf[idx - 1] |= cpu_to_le32(ATA_PRD_EOT);
	sgt_len = idx * 4;

	/*
	 * Build ATA, host DMA packets
	 */
	pdc20621_host_sg(&qc->tf, &pp->dimm_buf[0], portno, total_len);
	pdc20621_host_pkt(&qc->tf, &pp->dimm_buf[0], portno);

	pdc20621_ata_sg(&qc->tf, &pp->dimm_buf[0], portno, total_len);
	i = pdc20621_ata_pkt(&qc->tf, qc->dev->devno, &pp->dimm_buf[0], portno);

	if (qc->tf.flags & ATA_TFLAG_LBA48)
		i = pdc_prep_lba48(&qc->tf, &pp->dimm_buf[0], i);
	else
		i = pdc_prep_lba28(&qc->tf, &pp->dimm_buf[0], i);

	pdc_pkt_footer(&qc->tf, &pp->dimm_buf[0], i);

	/* copy three S/G tables and two packets to DIMM MMIO window */
	memcpy_toio(dimm_mmio + (portno * PDC_DIMM_WINDOW_STEP),
		    &pp->dimm_buf, PDC_DIMM_HEADER_SZ);
	memcpy_toio(dimm_mmio + (portno * PDC_DIMM_WINDOW_STEP) +
		    PDC_DIMM_HOST_PRD,
		    &pp->dimm_buf[PDC_DIMM_HEADER_SZ], sgt_len);

	/* force host FIFO dump */
	writel(0x00000001, mmio + PDC_20621_GENERAL_CTL);

	readl(dimm_mmio);	/* MMIO PCI posting flush */

	VPRINTK("ata pkt buf ofs %u, prd size %u, mmio copied\n", i, sgt_len);
}

#ifdef DIRECT_HDMA
static void pdc20621_push_hdma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_host_set *host_set = ap->host_set;
	unsigned int port_no = ap->port_no;
	void *mmio = host_set->mmio_base;
	unsigned int rw = (qc->flags & ATA_QCFLAG_WRITE);
	u32 tmp;

	unsigned int host_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * port_no) +
			       PDC_DIMM_HOST_PRD;
	unsigned int dimm_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * port_no) +
			       PDC_DIMM_HPKT_PRD;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	tmp = readl(mmio + PDC_HDMA_CTLSTAT) & 0xffffff00;
	tmp |= port_no + 1 + 4;		/* seq. ID */
	if (!rw)
		tmp |= (1 << 6);	/* hdma data direction */
	writel(tmp, mmio + PDC_HDMA_CTLSTAT); /* note: stops DMA, if active */
	readl(mmio + PDC_HDMA_CTLSTAT);	/* flush */

	writel(host_sg, mmio + 0x108);
	writel(dimm_sg, mmio + 0x10C);
	writel(0, mmio + 0x128);

	tmp |= (1 << 7);
	writel(tmp, mmio + PDC_HDMA_CTLSTAT);
	readl(mmio + PDC_HDMA_CTLSTAT);	/* flush */
}
#endif

#ifdef ATA_VERBOSE_DEBUG
static void pdc20621_dump_hdma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int port_no = ap->port_no;
	void *dimm_mmio = ap->host_set->private_data;

	dimm_mmio += (port_no * PDC_DIMM_WINDOW_STEP);
	dimm_mmio += PDC_DIMM_HOST_PKT;

	printk(KERN_ERR "HDMA[0] == 0x%08X\n", readl(dimm_mmio));
	printk(KERN_ERR "HDMA[1] == 0x%08X\n", readl(dimm_mmio + 4));
	printk(KERN_ERR "HDMA[2] == 0x%08X\n", readl(dimm_mmio + 8));
	printk(KERN_ERR "HDMA[3] == 0x%08X\n", readl(dimm_mmio + 12));
}
#else
static inline void pdc20621_dump_hdma(struct ata_queued_cmd *qc) { }
#endif /* ATA_VERBOSE_DEBUG */

static void pdc20621_dma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_host_set *host_set = ap->host_set;
	unsigned int port_no = ap->port_no;
	void *mmio = host_set->mmio_base;
	unsigned int rw = (qc->flags & ATA_QCFLAG_WRITE);
	u8 seq = (u8) (port_no + 1);
	unsigned int doing_hdma = 0, port_ofs;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	VPRINTK("ata%u: ENTER\n", ap->id);

	port_ofs = PDC_20621_DIMM_BASE + (PDC_DIMM_WINDOW_STEP * port_no);

	/* if writing, we (1) DMA to DIMM, then (2) do ATA command */
	if (rw) {
		doing_hdma = 1;
		seq += 4;
	}

	wmb();			/* flush PRD, pkt writes */

	writel(0x00000001, mmio + PDC_20621_SEQCTL + (seq * 4));
	readl(mmio + PDC_20621_SEQCTL + (seq * 4));	/* flush */

	if (doing_hdma) {
		pdc20621_dump_hdma(qc);
#ifdef DIRECT_HDMA
		pdc20621_push_hdma(qc);
#else
		writel(port_ofs + PDC_DIMM_HOST_PKT,
		       mmio + PDC_HDMA_PKT_SUBMIT);
		readl(mmio + PDC_HDMA_PKT_SUBMIT);	/* flush */
#endif
		VPRINTK("submitted ofs 0x%x (%u), seq %u\n",
		port_ofs + PDC_DIMM_HOST_PKT,
		port_ofs + PDC_DIMM_HOST_PKT,
		seq);
	} else {
		writel(port_ofs + PDC_DIMM_ATA_PKT,
		       (void *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
		readl((void *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
		VPRINTK("submitted ofs 0x%x (%u), seq %u\n",
			port_ofs + PDC_DIMM_ATA_PKT,
			port_ofs + PDC_DIMM_ATA_PKT,
			seq);
	}
}

static inline unsigned int pdc20621_host_intr( struct ata_port *ap,
                                          struct ata_queued_cmd *qc,
					  unsigned int doing_hdma,
					  void *mmio)
{
	unsigned int port_no = ap->port_no;
	unsigned int port_ofs =
		PDC_20621_DIMM_BASE + (PDC_DIMM_WINDOW_STEP * port_no);
	u8 status;
	unsigned int handled = 0;

	VPRINTK("ENTER\n");

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA_READ:
		/* step two - DMA from DIMM to host */
		if (doing_hdma) {
			VPRINTK("ata%u: read hdma, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));
			pdc_dma_complete(ap, qc);
		}

		/* step one - exec ATA command */
		else {
			u8 seq = (u8) (port_no + 1 + 4);
			VPRINTK("ata%u: read ata, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));

			/* submit hdma pkt */
			pdc20621_dump_hdma(qc);
			writel(0x00000001, mmio + PDC_20621_SEQCTL + (seq * 4));
			readl(mmio + PDC_20621_SEQCTL + (seq * 4));
#ifdef DIRECT_HDMA
			pdc20621_push_hdma(qc);
#else
			writel(port_ofs + PDC_DIMM_HOST_PKT,
			       mmio + PDC_HDMA_PKT_SUBMIT);
			readl(mmio + PDC_HDMA_PKT_SUBMIT);
#endif
		}
		handled = 1;
		break;

	case ATA_PROT_DMA_WRITE:
		/* step one - DMA from host to DIMM */
		if (doing_hdma) {
			u8 seq = (u8) (port_no + 1);
			VPRINTK("ata%u: write hdma, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));

			/* submit ata pkt */
			writel(0x00000001, mmio + PDC_20621_SEQCTL + (seq * 4));
			readl(mmio + PDC_20621_SEQCTL + (seq * 4));
			writel(port_ofs + PDC_DIMM_ATA_PKT,
			       (void *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
			readl((void *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
		}

		/* step two - execute ATA command */
		else {
			VPRINTK("ata%u: write ata, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));
			pdc_dma_complete(ap, qc);
		}
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

static irqreturn_t pdc20621_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct ata_port *ap;
	u32 mask = 0;
	unsigned int i, tmp, port_no;
	unsigned int handled = 0;
	void *mmio_base;

	VPRINTK("ENTER\n");

	if (!host_set || !host_set->mmio_base) {
		VPRINTK("QUICK EXIT\n");
		return IRQ_NONE;
	}

	mmio_base = host_set->mmio_base;

	/* reading should also clear interrupts */
	mmio_base += PDC_CHIP0_OFS;
	mask = readl(mmio_base + PDC_20621_SEQMASK);
	VPRINTK("mask == 0x%x\n", mask);

	if (mask == 0xffffffff) {
		VPRINTK("QUICK EXIT 2\n");
		return IRQ_NONE;
	}
	mask &= 0xffff;		/* only 16 tags possible */
	if (!mask) {
		VPRINTK("QUICK EXIT 3\n");
		return IRQ_NONE;
	}

        spin_lock_irq(&host_set->lock);

        for (i = 1; i < 9; i++) {
		port_no = i - 1;
		if (port_no > 3)
			port_no -= 4;
		if (port_no >= host_set->n_ports)
			ap = NULL;
		else
			ap = host_set->ports[port_no];
		tmp = mask & (1 << i);
		VPRINTK("seq %u, port_no %u, ap %p, tmp %x\n", i, port_no, ap, tmp);
		if (tmp && ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED))) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && ((qc->flags & ATA_QCFLAG_POLL) == 0))
				handled += pdc20621_host_intr(ap, qc, (i > 4),
							      mmio_base);
		}
	}

        spin_unlock_irq(&host_set->lock);

	VPRINTK("mask == 0x%x\n", mask);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static void pdc_fill_sg(struct ata_queued_cmd *qc)
{
	struct pdc_port_priv *pp = qc->ap->private_data;
	unsigned int i;

	VPRINTK("ENTER\n");

	ata_fill_sg(qc);

	i = pdc_pkt_header(&qc->tf, qc->ap->prd_dma,  qc->dev->devno, pp->pkt);

	if (qc->tf.flags & ATA_TFLAG_LBA48)
		i = pdc_prep_lba48(&qc->tf, pp->pkt, i);
	else
		i = pdc_prep_lba28(&qc->tf, pp->pkt, i);

	pdc_pkt_footer(&qc->tf, pp->pkt, i);
}

static inline void pdc_dma_complete (struct ata_port *ap,
                                     struct ata_queued_cmd *qc)
{
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
	unsigned int handled = 0;
	void *mmio_base;

	VPRINTK("ENTER\n");

	if (!host_set || !host_set->mmio_base) {
		VPRINTK("QUICK EXIT\n");
		return IRQ_NONE;
	}

	mmio_base = host_set->mmio_base;

	/* reading should also clear interrupts */
	mask = readl(mmio_base + PDC_INT_SEQMASK);

	if (mask == 0xffffffff) {
		VPRINTK("QUICK EXIT 2\n");
		return IRQ_NONE;
	}
	mask &= 0xffff;		/* only 16 tags possible */
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
	struct pdc_port_priv *pp = ap->private_data;
	unsigned int port_no = ap->port_no;
	u8 seq = (u8) (port_no + 1);

	VPRINTK("ENTER, ap %p\n", ap);

	writel(0x00000001, ap->host_set->mmio_base + (seq * 4));
	readl(ap->host_set->mmio_base + (seq * 4));	/* flush */

	pp->pkt[2] = seq;
	wmb();			/* flush PRD, pkt writes */
	writel(pp->pkt_dma, (void *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
	readl((void *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT); /* flush */
}

static void pdc_tf_load_mmio(struct ata_port *ap, struct ata_taskfile *tf)
{
	if ((tf->protocol != ATA_PROT_DMA_READ) &&
	    (tf->protocol != ATA_PROT_DMA_WRITE))
		ata_tf_load_mmio(ap, tf);
}


static void pdc_exec_command_mmio(struct ata_port *ap, struct ata_taskfile *tf)
{
	if ((tf->protocol != ATA_PROT_DMA_READ) &&
	    (tf->protocol != ATA_PROT_DMA_WRITE))
		ata_exec_command_mmio(ap, tf);
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
	u32 tmp;
	void *mmio = pe->mmio_base;

	mmio += PDC_CHIP0_OFS;

	/*
	 * Select page 0x40 for our 32k DIMM window
	 */
	tmp = readl(mmio + PDC_20621_DIMM_WINDOW) & 0xffff0000;
	tmp |= PDC_PAGE_WINDOW;	/* page 40h; arbitrarily selected */
	writel(tmp, mmio + PDC_20621_DIMM_WINDOW);

	/*
	 * Reset Host DMA
	 */
	tmp = readl(mmio + PDC_HDMA_CTLSTAT);
	tmp |= PDC_HDMA_RESET;
	writel(tmp, mmio + PDC_HDMA_CTLSTAT);
	readl(mmio + PDC_HDMA_CTLSTAT);		/* flush */

	udelay(10);

	tmp = readl(mmio + PDC_HDMA_CTLSTAT);
	tmp &= ~PDC_HDMA_RESET;
	writel(tmp, mmio + PDC_HDMA_CTLSTAT);
	readl(mmio + PDC_HDMA_CTLSTAT);		/* flush */
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
	void *mmio_base, *dimm_mmio = NULL;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	unsigned int have_20621 = (board_idx == board_20621);
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

	if (have_20621) {
		dimm_mmio = ioremap(pci_resource_start(pdev, 4),
				    pci_resource_len(pdev, 4));
		if (!dimm_mmio) {
			rc = -ENOMEM;
			goto err_out_iounmap;
		}
	}

	probe_ent->sht		= pdc_port_info[board_idx].sht;
	probe_ent->host_flags	= pdc_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= pdc_port_info[board_idx].pio_mask;
	probe_ent->udma_mask	= pdc_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= pdc_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;

	if (have_20621) {
		probe_ent->private_data = dimm_mmio;
		base += PDC_CHIP0_OFS;
	}

	pdc_sata_setup_port(&probe_ent->port[0], base + 0x200);
	pdc_sata_setup_port(&probe_ent->port[1], base + 0x280);

	if (!have_20621) {
		probe_ent->port[0].scr_addr = base + 0x400;
		probe_ent->port[1].scr_addr = base + 0x500;
	}

	/* notice 4-port boards */
	switch (board_idx) {
	case board_20319:
	case board_20621:
       		probe_ent->n_ports = 4;

		pdc_sata_setup_port(&probe_ent->port[2], base + 0x300);
		pdc_sata_setup_port(&probe_ent->port[3], base + 0x380);

		if (!have_20621) {
			probe_ent->port[2].scr_addr = base + 0x600;
			probe_ent->port[3].scr_addr = base + 0x700;
		}
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
	if (have_20621)
		pdc_20621_init(probe_ent);
	else
		pdc_host_init(board_idx, probe_ent);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_iounmap:
	iounmap(mmio_base);
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

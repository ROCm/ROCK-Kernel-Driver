
/*
 *  ata_adma.c - ADMA ATA support
 *
 *  Copyright 2004 Red Hat, Inc.
 *  Copyright 2004 Jeff Garzik
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
 *  Draft of the ADMA hardware specification:
 *  http://www.t13.org/project/d1510r1-Host-Adapter.pdf
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/io.h>

#define DRV_NAME	"ata_adma"
#define DRV_VERSION	"0.1"


enum board_ids_enum {
	board_adma,
};

enum {
	ADMA_PCI_BAR		= 4,
	ADMA_CPB_SZ		= 64,
	ADMA_SGTBL_LEN		= (4096 - ADMA_CPB_SZ) / 16,
	ADMA_SGTBL_SZ		= ADMA_SGTBL_LEN * 16,
	ADMA_PORT_PRIV_DMA_SZ	= ADMA_CPB_SZ + ADMA_SGTBL_SZ,

	APCI_TIM0		= 0x40,
	APCI_TIM1		= 0x42,
	APCI_UDMA_CTL		= 0x48,
	APCI_UDMA_TIMING	= 0x4A,
	APCI_IO_CFG		= 0x54,

	ADMA_CTL		= 0x0,
	ADMA_STAT		= 0x2,
	ADMA_CPB_COUNT		= 0x4,
	ADMA_NEXT_CPB		= 0xC,

	ADMA_AIEN		= (1 << 8),
	ADMA_GO			= (1 << 7),

	APRD_UDMA		= (1 << 4),
	APRD_WRITE		= (1 << 5),
	APRD_END		= (1 << 7),

	IRQ_PCI_ERR		= (1 << 0),
	IRQ_CPB_ERR		= (1 << 1),
	IRQ_DONE		= (1 << 7),
	IRQ_ERR_MASK		= IRQ_CPB_ERR | IRQ_PCI_ERR,

	CPB_APRD_VALID		= (1 << (2 + 16)),
	CPB_IEN			= (1 << (3 + 16)),
	CPB_LEN_SHIFT		= 24,
	CPB_ERR_MASK		= 0xf8, /* select err bits 7-3 */

	ADMA_USE_CLUSTERING	= 1,

	N_PORTS			= 2,
};

struct adma_prd {
	u32			addr;
	u32			len;
	u32			flags;
	u32			next_prd;
};

struct adma_host_priv {
	unsigned long		flags;
};

struct adma_port_priv {
	u32			*cpb;
	dma_addr_t		cpb_dma;
	struct adma_prd		*aprd;
	dma_addr_t		aprd_dma;
};

static int adma_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static int adma_qc_issue(struct ata_queued_cmd *qc);
static irqreturn_t adma_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static void adma_phy_reset(struct ata_port *ap);
static void adma_irq_clear(struct ata_port *ap);
static int adma_port_start(struct ata_port *ap);
static void adma_port_stop(struct ata_port *ap);
static void adma_host_stop(struct ata_host_set *host_set);
static void adma_qc_prep(struct ata_queued_cmd *qc);
static void adma_set_piomode(struct ata_port *ap, struct ata_device *adev);
static void adma_set_dmamode(struct ata_port *ap, struct ata_device *adev);

static Scsi_Host_Template adma_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= ADMA_SGTBL_LEN,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ADMA_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations adma_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= adma_set_piomode,
	.set_dmamode		= adma_set_dmamode,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,

	.phy_reset		= adma_phy_reset,

	.qc_prep		= adma_qc_prep,
	.qc_issue		= adma_qc_issue,

	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= adma_interrupt,
	.irq_clear		= adma_irq_clear,

	.port_start		= adma_port_start,
	.port_stop		= adma_port_stop,
	.host_stop		= adma_host_stop,
};

static struct ata_port_info adma_port_info[] = {
	/* board_adma */
	{
		.sht		= &adma_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST |
				  ATA_FLAG_NO_LEGACY | ATA_FLAG_MMIO,
		.pio_mask	= 0x03, /* pio3-4 */
		.udma_mask	= 0x3f, /* udma0-5 */
		.port_ops	= &adma_ops,
	},
};

static struct pci_device_id adma_pci_tbl[] = {
	{ }	/* terminate list */
};


static struct pci_driver adma_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= adma_pci_tbl,
	.probe			= adma_init_one,
	.remove			= ata_pci_remove_one,
};


static inline void __iomem *__adma_ctl_block(void __iomem *mmio,
					     unsigned int port_no)
{
	if (port_no == 0)
		mmio += 0x80;
	else
		mmio += 0xA0;

	return mmio;
}

static inline void __iomem *adma_ctl_block(struct ata_port *ap)
{
	return __adma_ctl_block(ap->host_set->mmio_base, ap->port_no);
}

static const struct adma_timing {
	unsigned int clk66, clk100;
	u16 cyc_tim;
} adma_udma_modes[] = {
	{ 0, 0, 0 },	/* udma0 */
	{ 0, 0, 1 },	/* udma1 */
	{ 0, 0, 2 },	/* udma2 */
	{ 1, 0, 1 },	/* udma3 */
	{ 1, 0, 2 },	/* udma4 */
	{ 1, 1, 1 },	/* udma5 */
};

static void adma_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int udma       = adev->dma_mode; /* FIXME: MWDMA too */
	struct pci_dev *dev	= to_pci_dev(ap->host_set->dev);
	unsigned int is_slave	= (adev->devno != 0);
	unsigned int shift	= ap->hard_port_no ? 2 : 0;
	const struct adma_timing *tim = &adma_udma_modes[udma];
	u8 tmp8, new8;
	u16 tmp16, new16;
	u32 tmp32, new32;

	if (is_slave)
		shift++;

	/*
	 * turn on UDMA for port X, device Y
	 */
	pci_read_config_byte(dev, APCI_UDMA_CTL, &tmp8);
	new8 = tmp8 | (1 << shift);
	if (tmp8 != new8)
		pci_write_config_byte(dev, APCI_UDMA_CTL, new8);

	/*
	 * set UDMA cycle time
	 */
	shift = ap->hard_port_no ? 8 : 0;
	if (is_slave)
		shift += 4;

	pci_read_config_word(dev, APCI_UDMA_TIMING, &tmp16);
	new16 = (tmp16 & ~(3 << shift)) | tim->cyc_tim;
	if (tmp16 != new16)
		pci_write_config_word(dev, APCI_UDMA_TIMING, new16);

	/*
	 * set 66/100Mhz base clocks
	 */
	pci_read_config_dword(dev, APCI_IO_CFG, &tmp32);
	new32 = tmp32;

	shift = ap->hard_port_no ? 2 : 0;	/* 66 Mhz */
	if (is_slave)
		shift++;
	if (tim->clk66)
		new32 |= (1 << shift);
	else
		new32 &= ~(1 << shift);

	shift = ap->hard_port_no ? 14 : 12;	/* 100 Mhz */
	if (is_slave)
		shift++;
	if (tim->clk100)
		new32 |= (1 << shift);
	else
		new32 &= ~(1 << shift);

	if (tmp32 != new32)
		pci_write_config_dword(dev, APCI_IO_CFG, new32);
}

static void adma_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host_set->dev);
	unsigned int is_slave	= (adev->devno != 0);
	u16 mask		= is_slave ? 0x0f : 0xf0;
	unsigned int reg	= ap->hard_port_no ? APCI_TIM1 : APCI_TIM0;
	void __iomem *mmio	= adma_ctl_block(ap);
	u16 tmp, new, timing1, timing2;

	timing1 = (1 << 15) | (1 << 13) | (1 << 8); /* pio3; decode enable */
	if (pio == 4)
		timing1 |= (1 << 9);		    /* -> pio4 */

	timing2 = (1 << 0) | (1 << 1); /* fast drv tim sel; IODRY sample pt. */
	if (adev->class == ATA_DEV_ATA)
		timing2 |= (1 << 2);	/* ATA or ATAPI device ? */
	if (is_slave)
		timing2 <<= 4;

	pci_read_config_word(dev, reg, &tmp);
	new = (tmp & mask) | timing1 | timing2;
	if (tmp != new)
		pci_write_config_word(dev, reg, new);

	tmp = readw(mmio + ADMA_CTL) & ~0x3;
	tmp |= (pio - 1) & 0x3;
	writew(tmp, mmio + ADMA_CTL);
}

static void adma_host_stop(struct ata_host_set *host_set)
{
	struct adma_host_priv *hpriv = host_set->private_data;
	kfree(hpriv);
}

static int adma_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct adma_port_priv *pp;
	int rc;
	void *mem;
	dma_addr_t mem_dma;

	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp = kmalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_out;
	}
	memset(pp, 0, sizeof(*pp));

	mem = dma_alloc_coherent(dev, ADMA_PORT_PRIV_DMA_SZ, &mem_dma, GFP_KERNEL);
	if (!mem) {
		rc = -ENOMEM;
		goto err_out_kfree;
	}
	memset(mem, 0, ADMA_PORT_PRIV_DMA_SZ);

	/*
	 * First item in chunk of DMA memory:
	 * 64-byte command parameter block (CPB)
	 */
	pp->cpb = mem;
	pp->cpb_dma = mem_dma;

	mem += ADMA_CPB_SZ;
	mem_dma += ADMA_CPB_SZ;

	/*
	 * Second item: block of ADMA_SGTBL_LEN s/g entries
	 */
	pp->aprd = mem;
	pp->aprd_dma = mem_dma;

	ap->private_data = pp;

	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}


static void adma_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = adma_ctl_block(ap);
	u16 tmp;

	tmp = readw(mmio + ADMA_CTL);
	writew((tmp & 0x3) | ADMA_AIEN, mmio + ADMA_CTL);

	ap->private_data = NULL;
	dma_free_coherent(dev, ADMA_PORT_PRIV_DMA_SZ, pp->cpb, pp->cpb_dma);
	kfree(pp);
	ata_port_stop(ap);
}

static void adma_cbl_detect(struct ata_port *ap)
{
	/* FIXME: todo */
}

static void adma_phy_reset(struct ata_port *ap)
{
	adma_cbl_detect(ap);
	ata_port_probe(ap);
	ata_bus_reset(ap);
}

static void adma_fill_sg(struct ata_queued_cmd *qc)
{
	struct adma_port_priv *pp = qc->ap->private_data;
	unsigned int i, idx;

	VPRINTK("ENTER\n");

	idx = 0;
	if (is_atapi_taskfile(&qc->tf)) {
		idx = 1;

		/* FIXME: point first s/g entry to ATAPI packet */
	}

	for (i = 0; i < qc->n_elem; i++, idx++) {
		u32 sg_len, addr, flags;

		addr = (u32) sg_dma_address(&qc->sg[i]);
		sg_len = sg_dma_len(&qc->sg[i]);

		flags = 0;
		if (qc->tf.flags & ATA_TFLAG_WRITE)
			flags |= APRD_WRITE;
		if (i == (qc->n_elem - 1))
			flags |= APRD_END;
		if (qc->tf.protocol == ATA_PROT_DMA ||
		    qc->tf.protocol == ATA_PROT_ATAPI_DMA) {
			flags |= APRD_UDMA;
			flags |= (qc->dev->dma_mode << 8);
			flags |= (2 << 12); /* udma bus burst size, 512b units*/
		} else {
			flags |= ((qc->dev->pio_mode - 1) << 8);
		}

		pp->aprd[idx].addr = cpu_to_le32(addr);
		pp->aprd[idx].len = cpu_to_le32(sg_len / 8); /* len in Qwords */
		pp->aprd[idx].flags = cpu_to_le32(flags);

		if (i == (qc->n_elem - 1))
			pp->aprd[idx].next_prd = 0;
		else {
			u32 tmp = (u32) pp->aprd_dma;
			tmp += ((idx + 1) * 16);
			pp->aprd[idx].next_prd = cpu_to_le32(tmp);
		}
	}
}

enum adma_regbits {
	CMDEND	= (1 << 15),		/* end of command list */
	WNB	= (1 << 14),		/* wait-not-BSY */
	IGN	= (1 << 13),		/* ignore this entry */
	CS1n	= (1 << (4 + 8)),	/* std. PATA signals follow... */
	DA2	= (1 << (2 + 8)),
	DA1	= (1 << (1 + 8)),
	DA0	= (1 << (0 + 8)),
};

static const u16 adma_regaddr[] = {
	CS1n,			/* ATA_REG_DATA */
	CS1n | DA0,		/* ATA_REG_ERR */
	CS1n | DA1,		/* ATA_REG_NSECT */
	CS1n | DA1 | DA0,	/* ATA_REG_LBAL */
	CS1n | DA2,		/* ATA_REG_LBAM */
	CS1n | DA2 | DA0,	/* ATA_REG_LBAH */
	CS1n | DA2 | DA1,	/* ATA_REG_DEVICE */
	CS1n | DA2 | DA1 | DA0,	/* ATA_REG_STATUS */
};

static unsigned int adma_tf_to_cpb(struct ata_taskfile *tf, u16 *cpb)
{
	unsigned int idx = 0;

	cpb[idx++] = cpu_to_le16(WNB | adma_regaddr[ATA_REG_ERR] | tf->feature);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_NSECT] | tf->nsect);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_LBAL] | tf->lbal);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_LBAM] | tf->lbam);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_LBAM] | tf->lbah);

	if ((tf->flags & ATA_TFLAG_LBA48) == 0) {
		cpb[idx++] = cpu_to_le16(IGN);
		cpb[idx++] = cpu_to_le16(IGN);
		cpb[idx++] = cpu_to_le16(IGN | CMDEND);
		return idx;
	}

	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_ERR] | tf->hob_feature);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_NSECT] | tf->hob_nsect);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_LBAL] | tf->hob_lbal);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_LBAM] | tf->hob_lbam);
	cpb[idx++] = cpu_to_le16(adma_regaddr[ATA_REG_LBAM] | tf->hob_lbah);
	cpb[idx++] = cpu_to_le16(IGN);
	cpb[idx++] = cpu_to_le16(IGN | CMDEND);

	return idx;
}

static void adma_qc_prep(struct ata_queued_cmd *qc)
{
	struct adma_port_priv *pp = qc->ap->private_data;
	u32 flags, *cpb = pp->cpb;
	u16 *cpb16;
	unsigned int cpb_used;

	cpb[0] = cpu_to_le32(1);
	cpb[1] = cpu_to_le32((u32) pp->cpb_dma);
	cpb[2] = cpu_to_le32((u32) pp->aprd_dma);
	cpb[3] = 0;

	cpb16 = (u16 *) &cpb[4];
	cpb_used = adma_tf_to_cpb(&qc->tf, cpb16);

	flags = CPB_APRD_VALID | CPB_IEN;
	flags |= (cpb_used / 4) << CPB_LEN_SHIFT;

	cpb[0] = cpu_to_le32(flags);

	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	adma_fill_sg(qc);
}

static inline void adma_complete (struct ata_port *ap,
				  struct ata_queued_cmd *qc, int have_err)
{
	/* get drive status; clear intr; complete txn */
	ata_qc_complete(ata_qc_from_tag(ap, ap->active_tag),
			have_err ? ATA_ERR : 0);
}

static inline int adma_host_intr(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	void __iomem *mmio = adma_ctl_block(ap);
	struct adma_port_priv *pp = ap->private_data;
	u8 status;
	int have_err;

	/* reading clears all flagged events */
	status = readb(mmio + ADMA_STAT);
	status &= ~(1 << 2); /* mask out reserved bit */
	if (!status)
		return 0;	/* no irq handled */

	if (status & IRQ_ERR_MASK)
		have_err = 1;
	else if (le32_to_cpu(pp->cpb[0]) & CPB_ERR_MASK)
		have_err = 1;
	else
		have_err = 0;

	adma_complete(ap, qc, have_err);

	return 1; /* irq handled */
}

static void adma_irq_clear(struct ata_port *ap)
{
	/* TODO */
}

static irqreturn_t adma_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct adma_host_priv *hpriv;
	unsigned int i, handled = 0;
	void *mmio;

	VPRINTK("ENTER\n");

	hpriv = host_set->private_data;
	mmio = host_set->mmio_base;

        spin_lock(&host_set->lock);

        for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap = host_set->ports[i];
		struct ata_queued_cmd *qc;
		VPRINTK("port %u\n", i);

		qc = ata_qc_from_tag(ap, ap->active_tag);
		if (qc)
			handled |= adma_host_intr(ap, qc);
	}

        spin_unlock(&host_set->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static int adma_qc_issue(struct ata_queued_cmd *qc)
{
	void __iomem *mmio = adma_ctl_block(qc->ap);
	struct adma_port_priv *pp = qc->ap->private_data;
	u16 tmp;

	writew(1, mmio + ADMA_CPB_COUNT);
	writel((u32) pp->cpb_dma, mmio + ADMA_NEXT_CPB);

	tmp = readw(mmio + ADMA_CTL);
	writew((tmp & 0x3) | ADMA_GO, mmio + ADMA_CTL);

	return 0;
}

static void adma_setup_port(struct ata_probe_ent *probe_ent, unsigned int port)
{
	void __iomem *mmio = probe_ent->mmio_base;
	struct ata_ioports *ioport = &probe_ent->port[port];

	if (port == 1)
		mmio += 0x40;

	ioport->cmd_addr	= (unsigned long) mmio;
	ioport->data_addr	= (unsigned long) mmio + (ATA_REG_DATA * 4);
	ioport->error_addr	=
	ioport->feature_addr	= (unsigned long) mmio + (ATA_REG_ERR * 4);
	ioport->nsect_addr	= (unsigned long) mmio + (ATA_REG_NSECT * 4);
	ioport->lbal_addr	= (unsigned long) mmio + (ATA_REG_LBAL * 4);
	ioport->lbam_addr	= (unsigned long) mmio + (ATA_REG_LBAM * 4);
	ioport->lbah_addr	= (unsigned long) mmio + (ATA_REG_LBAH * 4);
	ioport->device_addr	= (unsigned long) mmio + (ATA_REG_DEVICE * 4);
	ioport->status_addr	=
	ioport->command_addr	= (unsigned long) mmio + (ATA_REG_STATUS * 4);
	ioport->altstatus_addr	=
	ioport->ctl_addr	= (unsigned long) mmio + 0x38;
}

static int adma_host_init(struct ata_probe_ent *probe_ent)
{
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	unsigned int i;
	u16 tmp16, new16;
	u32 tmp32, new32;

	probe_ent->n_ports = N_PORTS;

	for (i = 0; i < probe_ent->n_ports; i++)
		adma_setup_port(probe_ent, i);

	/* enable I/O address range decoding, disable Dev1 timing register */
	pci_read_config_word(pdev, APCI_TIM0, &tmp16);
	new16 = (tmp16 & ~(1 << 14)) | (1 << 15);
	if (new16 != tmp16)
		pci_write_config_word(pdev, APCI_TIM0, new16);
	pci_read_config_word(pdev, APCI_TIM1, &tmp16);
	new16 = (tmp16 & ~(1 << 14)) | (1 << 15);
	if (new16 != tmp16)
		pci_write_config_word(pdev, APCI_TIM1, new16);

	/* make sure ATA signal pins are not driven low or tri-stated */
	pci_read_config_dword(pdev, APCI_IO_CFG, &tmp32);
	new32 = tmp32 & ~(0xfU << 16);
	if (new32 != tmp32)
		pci_write_config_dword(pdev, APCI_IO_CFG, new32);

	for (i = 0; i < probe_ent->n_ports; i++) {
		void __iomem *mmio = __adma_ctl_block(probe_ent->mmio_base, i);
		u16 tmp;

		/* enable interrupt, clear reset if not already clear */
		tmp = readw(mmio + ADMA_CTL);
		writew(tmp & 0x3, mmio + ADMA_CTL);
	}

	pci_set_master(pdev);

	return 0;
}

static int adma_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	struct adma_host_priv *hpriv;
	unsigned long base;
	void *mmio_base;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int rc;

	VPRINTK("ENTER\n");

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

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	/* FIXME: ADMA BAR is always 64-bit... does the PCI
	 * layer assign that BAR4, or do we need to '|' with BAR5?
	 */
	mmio_base = ioremap(pci_resource_start(pdev, ADMA_PCI_BAR),
		            pci_resource_len(pdev, ADMA_PCI_BAR));
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	hpriv = kmalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		rc = -ENOMEM;
		goto err_out_iounmap;
	}
	memset(hpriv, 0, sizeof(*hpriv));

	probe_ent->sht		= adma_port_info[board_idx].sht;
	probe_ent->host_flags	= adma_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= adma_port_info[board_idx].pio_mask;
	probe_ent->udma_mask	= adma_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= adma_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;
	probe_ent->private_data = hpriv;

	/* initialize adapter */
	rc = adma_host_init(probe_ent);
	if (rc)
		goto err_out_hpriv;

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_hpriv:
	kfree(hpriv);
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


static int __init adma_init(void)
{
	return pci_module_init(&adma_pci_driver);
}


static void __exit adma_exit(void)
{
	pci_unregister_driver(&adma_pci_driver);
}


MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("ADMA ATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, adma_pci_tbl);

module_init(adma_init);
module_exit(adma_exit);

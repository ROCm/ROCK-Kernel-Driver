/*

    ata_piix.c - Intel PATA/SATA controllers


	Copyright 2003 Red Hat Inc
	Copyright 2003 Jeff Garzik


	Copyright header from piix.c:

    Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
    Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
    Copyright (C) 2003 Red Hat Inc <alan@redhat.com>

    May be copied or modified under the terms of the GNU General Public License

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

#define DRV_NAME	"ata_piix"
#define DRV_VERSION	"0.95"

enum {
	PIIX_IOCFG		= 0x54, /* IDE I/O configuration register */
	ICH5_PCS		= 0x92,	/* port control and status */

	PIIX_FLAG_COMBINED	= (1 << 30), /* combined mode possible */

	PIIX_COMB_PRI		= (1 << 0), /* combined mode, PATA primary */
	PIIX_COMB_SEC		= (1 << 1), /* combined mode, PATA secondary */

	PIIX_80C_PRI		= (1 << 5) | (1 << 4),
	PIIX_80C_SEC		= (1 << 7) | (1 << 6),

	ich5_pata		= 0,
	ich5_sata		= 1,
	piix4_pata		= 2,
};

static int piix_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent);

static void piix_pata_phy_reset(struct ata_port *ap);
static void piix_sata_phy_reset(struct ata_port *ap);
static void piix_sata_port_disable(struct ata_port *ap);
static void piix_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio);
static void piix_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			       unsigned int udma);

static unsigned int in_module_init = 1;

static struct pci_device_id piix_pci_tbl[] = {
#ifdef ATA_ENABLE_PATA
	{ 0x8086, 0x7111, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix4_pata },
	{ 0x8086, 0x24db, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_pata },
	{ 0x8086, 0x25a2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_pata },
#endif

	{ 0x8086, 0x24d1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	{ 0x8086, 0x24df, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	{ 0x8086, 0x25a3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	{ 0x8086, 0x25b0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },

	{ }	/* terminate list */
};

static struct pci_driver piix_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= piix_pci_tbl,
	.probe			= piix_init_one,
	.remove			= ata_pci_remove_one,
};

static Scsi_Host_Template piix_sht = {
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

static struct ata_port_operations piix_pata_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= piix_set_piomode,
	.set_udmamode		= piix_set_udmamode,

	.tf_load		= ata_tf_load_pio,
	.tf_read		= ata_tf_read_pio,
	.check_status		= ata_check_status_pio,
	.exec_command		= ata_exec_command_pio,

	.phy_reset		= piix_pata_phy_reset,
	.phy_config		= pata_phy_config,

	.bmdma_start		= ata_bmdma_start_pio,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_operations piix_sata_ops = {
	.port_disable		= piix_sata_port_disable,
	.set_piomode		= piix_set_piomode,
	.set_udmamode		= piix_set_udmamode,

	.tf_load		= ata_tf_load_pio,
	.tf_read		= ata_tf_read_pio,
	.check_status		= ata_check_status_pio,
	.exec_command		= ata_exec_command_pio,

	.phy_reset		= piix_sata_phy_reset,
	.phy_config		= pata_phy_config,	/* not a typo */

	.bmdma_start		= ata_bmdma_start_pio,
	.fill_sg		= ata_fill_sg,
	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_info piix_port_info[] = {
	/* ich5_pata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask	= 0x03,	/* pio3-4 */
		.udma_mask	= ATA_UDMA_MASK_40C, /* FIXME: cbl det */
		.port_ops	= &piix_pata_ops,
	},

	/* ich5_sata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SATA | PIIX_FLAG_COMBINED |
				  ATA_FLAG_SRST,
		.pio_mask	= 0x03,	/* pio3-4 */
		.udma_mask	= 0x7f,	/* udma0-6 ; FIXME */
		.port_ops	= &piix_sata_ops,
	},

	/* piix4_pata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask	= 0x03, /* pio3-4 */
		.udma_mask	= ATA_UDMA_MASK_40C, /* FIXME: cbl det */
		.port_ops	= &piix_pata_ops,
	},
};

static struct pci_bits piix_enable_bits[] = {
	{ 0x41U, 1U, 0x80UL, 0x80UL },	/* port 0 */
	{ 0x43U, 1U, 0x80UL, 0x80UL },	/* port 1 */
};

MODULE_AUTHOR("Andre Hedrick, Alan Cox, Andrzej Krzysztofowicz, Jeff Garzik");
MODULE_DESCRIPTION("SCSI low-level driver for Intel PIIX/ICH ATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, piix_pci_tbl);

/**
 *	piix_pata_cbl_detect - Probe host controller cable detect info
 *	@ap: Port for which cable detect info is desired
 *
 *	Read 80c cable indicator from SATA PCI device's PCI config
 *	register.  This register is normally set by firmware (BIOS).
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void piix_pata_cbl_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = ap->host_set->pdev;
	u8 tmp, mask;

	/* no 80c support in host controller? */
	if ((ap->udma_mask & ~ATA_UDMA_MASK_40C) == 0)
		goto cbl40;

	/* check BIOS cable detect results */
	mask = ap->port_no == 0 ? PIIX_80C_PRI : PIIX_80C_SEC;
	pci_read_config_byte(pdev, PIIX_IOCFG, &tmp);
	if ((tmp & mask) == 0)
		goto cbl40;

	ap->cbl = ATA_CBL_PATA80;
	return;

cbl40:
	ap->cbl = ATA_CBL_PATA40;
	ap->udma_mask &= ATA_UDMA_MASK_40C;
}

/**
 *	piix_pata_phy_reset - Probe specified port on PATA host controller
 *	@ap: Port to probe
 *
 *	Probe PATA phy.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_pata_phy_reset(struct ata_port *ap)
{
	if (!pci_test_config_bits(ap->host_set->pdev,
				  &piix_enable_bits[ap->port_no])) {
		ata_port_disable(ap);
		printk(KERN_INFO "ata%u: port disabled. ignoring.\n", ap->id);
		return;
	}

	piix_pata_cbl_detect(ap);

	ata_port_probe(ap);

	ata_bus_reset(ap);
}

/**
 *	piix_pcs_probe - Probe SATA port configuration and status register
 *	@ap: Port to probe
 *	@have_port: (output) Non-zero if SATA port is enabled
 *	@have_device: (output) Non-zero if SATA phy indicates device present
 *
 *	Reads SATA PCI device's PCI config register Port Configuration
 *	and Status (PCS) to determine port and device availability.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void piix_pcs_probe (struct ata_port *ap, unsigned int *have_port,
			    unsigned int *have_device)
{
	struct pci_dev *pdev = ap->host_set->pdev;
	u16 pcs;

	pci_read_config_word(pdev, ICH5_PCS, &pcs);

	/* is SATA port enabled? */
	if (pcs & (1 << ap->port_no)) {
		*have_port = 1;

		if (pcs & (1 << (ap->port_no + 4)))
			*have_device = 1;
	}
}

/**
 *	piix_pcs_disable - Disable SATA port
 *	@ap: Port to disable
 *
 *	Disable SATA phy for specified port.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void piix_pcs_disable (struct ata_port *ap)
{
	struct pci_dev *pdev = ap->host_set->pdev;
	u16 pcs;

	pci_read_config_word(pdev, ICH5_PCS, &pcs);

	if (pcs & (1 << ap->port_no)) {
		pcs &= ~(1 << ap->port_no);
		pci_write_config_word(pdev, ICH5_PCS, pcs);
	}
}

/**
 *	piix_sata_phy_reset - Probe specified port on SATA host controller
 *	@ap: Port to probe
 *
 *	Probe SATA phy.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_sata_phy_reset(struct ata_port *ap)
{
	unsigned int have_port = 0, have_dev = 0;

	if (!pci_test_config_bits(ap->host_set->pdev,
				  &piix_enable_bits[ap->port_no])) {
		ata_port_disable(ap);
		printk(KERN_INFO "ata%u: port disabled. ignoring.\n", ap->id);
		return;
	}

	piix_pcs_probe(ap, &have_port, &have_dev);

	/* if port not enabled, exit */
	if (!have_port) {
		ata_port_disable(ap);
		printk(KERN_INFO "ata%u: SATA port disabled. ignoring.\n",
		       ap->id);
		return;
	}

	/* if port enabled but no device, disable port and exit */
	if (!have_dev) {
		piix_sata_port_disable(ap);
		printk(KERN_INFO "ata%u: SATA port has no device. disabling.\n",
		       ap->id);
		return;
	}

	ap->cbl = ATA_CBL_SATA;

	ata_port_probe(ap);

	ata_bus_reset(ap);
}

/**
 *	piix_sata_port_disable - Disable SATA port
 *	@ap: Port to disable.
 *
 *	Disable SATA port.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_sata_port_disable(struct ata_port *ap)
{
	ata_port_disable(ap);
	piix_pcs_disable(ap);
}

/**
 *	piix_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *	@pio: PIO mode, 0 - 4
 *
 *	Set PIO mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio)
{
	struct pci_dev *dev	= ap->host_set->pdev;
	unsigned int is_slave	= (adev->flags & ATA_DFLAG_MASTER) ? 0 : 1;
	unsigned int master_port= ap->port_no ? 0x42 : 0x40;
	unsigned int slave_port	= 0x44;
	u16 master_data;
	u8 slave_data;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		master_data |= 0x4000;
		/* enable PPE, IE and TIME */
		master_data |= 0x0070;
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= (ap->port_no ? 0x0f : 0xf0);
		slave_data |=
			(timings[pio][0] << 2) |
			(timings[pio][1] << (ap->port_no ? 4 : 0));
	} else {
		master_data &= 0xccf8;
		/* enable PPE, IE and TIME */
		master_data |= 0x0007;
		master_data |=
			(timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
}

/**
 *	piix_set_udmamode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *	@udma: udma mode, 0 - 6
 *
 *	Set UDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma)
{
	struct pci_dev *dev	= ap->host_set->pdev;
	u8 maslave		= ap->port_no ? 0x42 : 0x40;
	u8 speed		= udma;
	unsigned int drive_dn	= (ap->port_no ? 2 : 0) + adev->devno;
	int a_speed		= 3 << (drive_dn * 4);
	int u_flag		= 1 << drive_dn;
	int v_flag		= 0x01 << drive_dn;
	int w_flag		= 0x10 << drive_dn;
	int u_speed		= 0;
	int			sitre;
	u16			reg4042, reg44, reg48, reg4a, reg54;
	u8			reg55;

	pci_read_config_word(dev, maslave, &reg4042);
	DPRINTK("reg4042 = 0x%04x\n", reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);
	pci_read_config_word(dev, 0x54, &reg54);
	pci_read_config_byte(dev, 0x55, &reg55);

	switch(speed) {
		case XFER_UDMA_4:
		case XFER_UDMA_2:	u_speed = 2 << (drive_dn * 4); break;
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:	u_speed = 1 << (drive_dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive_dn * 4); break;
		default:
			BUG();
			return;
	}

	if (!(reg48 & u_flag))
		pci_write_config_word(dev, 0x48, reg48|u_flag);
	if (speed == XFER_UDMA_5) {
		pci_write_config_byte(dev, 0x55, (u8) reg55|w_flag);
	} else {
		pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
	}
	if (!(reg4a & u_speed)) {
		pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		pci_write_config_word(dev, 0x4a, reg4a|u_speed);
	}
	if (speed > XFER_UDMA_2) {
		if (!(reg54 & v_flag)) {
			pci_write_config_word(dev, 0x54, reg54|v_flag);
		}
	} else {
		pci_write_config_word(dev, 0x54, reg54 & ~v_flag);
	}
}

/**
 *	piix_probe_combined - Determine if PATA and SATA are combined
 *	@pdev: PCI device to examine
 *	@mask: (output) zero, %PIIX_COMB_PRI or %PIIX_COMB_SEC
 *
 *	Determine if BIOS has secretly stuffed a PATA port into our
 *	otherwise-beautiful SATA PCI device.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 */
static void piix_probe_combined (struct pci_dev *pdev, unsigned int *mask)
{
	u8 tmp;

	pci_read_config_byte(pdev, 0x90, &tmp); /* combined mode reg */
	tmp &= 0x6; 	/* interesting bits 2:1, PATA primary/secondary */

	/* backwards from what one might expect */
	if (tmp == 0x4)	/* bits 10x */
		*mask |= PIIX_COMB_SEC;
	if (tmp == 0x6)	/* bits 11x */
		*mask |= PIIX_COMB_PRI;
}

/**
 *	piix_init_one - Register PIIX ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in piix_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.  We probe for combined mode (sigh),
 *	and then hand over control to libata, for it to do the rest.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int piix_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info *port_info[2];
	unsigned int combined = 0, n_ports = 1;
	unsigned int pata_comb = 0, sata_comb = 0;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	/* no hotplugging support (FIXME) */
	if (!in_module_init)
		return -ENODEV;

	port_info[0] = &piix_port_info[ent->driver_data];
	port_info[1] = NULL;
	if (port_info[0]->host_flags & PIIX_FLAG_COMBINED)
		piix_probe_combined(pdev, &combined);

	if (combined & PIIX_COMB_PRI)
		sata_comb = 1;
	else if (combined & PIIX_COMB_SEC)
		pata_comb = 1;

	if (pata_comb || sata_comb) {
		port_info[sata_comb] = &piix_port_info[ent->driver_data];
		port_info[sata_comb]->host_flags |= ATA_FLAG_SLAVE_POSS; /* sigh */
		port_info[pata_comb] = &piix_port_info[ich5_pata]; /*ich5-specific*/
		n_ports++;

		printk(KERN_WARNING DRV_NAME ": combined mode detected\n");
	}

	return ata_pci_init_one(pdev, port_info, n_ports);
}

/**
 *	piix_init -
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int __init piix_init(void)
{
	int rc;

	DPRINTK("pci_module_init\n");
	rc = pci_module_init(&piix_pci_driver);
	if (rc)
		return rc;

	in_module_init = 0;

	DPRINTK("done\n");
	return 0;
}

/**
 *	piix_exit -
 *
 *	LOCKING:
 *
 */

static void __exit piix_exit(void)
{
	pci_unregister_driver(&piix_pci_driver);
}

module_init(piix_init);
module_exit(piix_exit);


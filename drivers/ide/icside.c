/*
 * linux/drivers/ide/icside.c
 *
 * Copyright (c) 1996-2002 Russell King.
 *
 * Changelog:
 *  08-Jun-1996	RMK	Created
 *  12-Sep-1997	RMK	Added interrupt enable/disable
 *  17-Apr-1999	RMK	Added support for V6 EASI
 *  22-May-1999	RMK	Added support for V6 DMA
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/io.h>
#include <asm/page.h>

/*
 * Maximum number of interfaces per card
 */
#define MAX_IFS	2

#define ICS_IDENT_OFFSET		0x8a0

#define ICS_ARCIN_V5_INTRSTAT		0x000
#define ICS_ARCIN_V5_INTROFFSET		0x001
#define ICS_ARCIN_V5_IDEOFFSET		0xa00
#define ICS_ARCIN_V5_IDEALTOFFSET	0xae0
#define ICS_ARCIN_V5_IDESTEPPING	4

#define ICS_ARCIN_V6_IDEOFFSET_1	0x800
#define ICS_ARCIN_V6_INTROFFSET_1	0x880
#define ICS_ARCIN_V6_INTRSTAT_1		0x8a4
#define ICS_ARCIN_V6_IDEALTOFFSET_1	0x8e0
#define ICS_ARCIN_V6_IDEOFFSET_2	0xc00
#define ICS_ARCIN_V6_INTROFFSET_2	0xc80
#define ICS_ARCIN_V6_INTRSTAT_2		0xca4
#define ICS_ARCIN_V6_IDEALTOFFSET_2	0xce0
#define ICS_ARCIN_V6_IDESTEPPING	4

struct cardinfo {
	unsigned int dataoffset;
	unsigned int ctrloffset;
	unsigned int stepping;
};

static struct cardinfo icside_cardinfo_v5 = {
	ICS_ARCIN_V5_IDEOFFSET,
	ICS_ARCIN_V5_IDEALTOFFSET,
	ICS_ARCIN_V5_IDESTEPPING
};

static struct cardinfo icside_cardinfo_v6_1 = {
	ICS_ARCIN_V6_IDEOFFSET_1,
	ICS_ARCIN_V6_IDEALTOFFSET_1,
	ICS_ARCIN_V6_IDESTEPPING
};

static struct cardinfo icside_cardinfo_v6_2 = {
	ICS_ARCIN_V6_IDEOFFSET_2,
	ICS_ARCIN_V6_IDEALTOFFSET_2,
	ICS_ARCIN_V6_IDESTEPPING
};

struct icside_state {
	unsigned int channel;
	unsigned int enabled;
	unsigned int irq_port;
};

static const card_ids icside_cids[] = {
	{ MANU_ICS,  PROD_ICS_IDE  },
	{ MANU_ICS2, PROD_ICS2_IDE },
	{ 0xffff, 0xffff }
};

typedef enum {
	ics_if_unknown,
	ics_if_arcin_v5,
	ics_if_arcin_v6
} iftype_t;

/* ---------------- Version 5 PCB Support Functions --------------------- */
/* Prototype: icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	unsigned int memc_port = (unsigned int)ec->irq_data;
	outb (0, memc_port + ICS_ARCIN_V5_INTROFFSET);
}

/* Prototype: icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	unsigned int memc_port = (unsigned int)ec->irq_data;
	inb (memc_port + ICS_ARCIN_V5_INTROFFSET);
}

static const expansioncard_ops_t icside_ops_arcin_v5 = {
	icside_irqenable_arcin_v5,
	icside_irqdisable_arcin_v5,
	NULL,
	NULL,
	NULL,
	NULL
};


/* ---------------- Version 6 PCB Support Functions --------------------- */
/* Prototype: icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	struct icside_state *state = ec->irq_data;
	unsigned int base = state->irq_port;

	state->enabled = 1;

	switch (state->channel) {
	case 0:
		outb(0, base + ICS_ARCIN_V6_INTROFFSET_1);
		inb(base + ICS_ARCIN_V6_INTROFFSET_2);
		break;
	case 1:
		outb(0, base + ICS_ARCIN_V6_INTROFFSET_2);
		inb(base + ICS_ARCIN_V6_INTROFFSET_1);
		break;
	}
}

/* Prototype: icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	struct icside_state *state = ec->irq_data;

	state->enabled = 0;

	inb (state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
	inb (state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
}

/* Prototype: icside_irqprobe(struct expansion_card *ec)
 * Purpose  : detect an active interrupt from card
 */
static int icside_irqpending_arcin_v6(struct expansion_card *ec)
{
	struct icside_state *state = ec->irq_data;

	return inb(state->irq_port + ICS_ARCIN_V6_INTRSTAT_1) & 1 ||
	       inb(state->irq_port + ICS_ARCIN_V6_INTRSTAT_2) & 1;
}

static const expansioncard_ops_t icside_ops_arcin_v6 = {
	icside_irqenable_arcin_v6,
	icside_irqdisable_arcin_v6,
	icside_irqpending_arcin_v6,
	NULL,
	NULL,
	NULL
};

/* Prototype: icside_identifyif (struct expansion_card *ec)
 * Purpose  : identify IDE interface type
 * Notes    : checks the description string
 */
static iftype_t __init icside_identifyif (struct expansion_card *ec)
{
	unsigned int addr;
	iftype_t iftype;
	int id = 0;

	iftype = ics_if_unknown;

	addr = ecard_address (ec, ECARD_IOC, ECARD_FAST) + ICS_IDENT_OFFSET;

	id = inb (addr) & 1;
	id |= (inb (addr + 1) & 1) << 1;
	id |= (inb (addr + 2) & 1) << 2;
	id |= (inb (addr + 3) & 1) << 3;

	switch (id) {
	case 0: /* A3IN */
		printk("icside: A3IN unsupported\n");
		break;

	case 1: /* A3USER */
		printk("icside: A3USER unsupported\n");
		break;

	case 3:	/* ARCIN V6 */
		printk(KERN_DEBUG "icside: detected ARCIN V6 in slot %d\n", ec->slot_no);
		iftype = ics_if_arcin_v6;
		break;

	case 15:/* ARCIN V5 (no id) */
		printk(KERN_DEBUG "icside: detected ARCIN V5 in slot %d\n", ec->slot_no);
		iftype = ics_if_arcin_v5;
		break;

	default:/* we don't know - complain very loudly */
		printk("icside: ***********************************\n");
		printk("icside: *** UNKNOWN ICS INTERFACE id=%d ***\n", id);
		printk("icside: ***********************************\n");
		printk("icside: please report this to linux@arm.linux.org.uk\n");
		printk("icside: defaulting to ARCIN V5\n");
		iftype = ics_if_arcin_v5;
		break;
	}

	return iftype;
}

/*
 * Handle routing of interrupts.  This is called before
 * we write the command to the drive.
 */
static void icside_maskproc(struct ata_device *drive)
{
	const int mask = 0;
	struct ata_channel *ch = drive->channel;
	struct icside_state *state = ch->hw.priv;
	unsigned long flags;

	local_irq_save(flags);

	state->channel = ch->unit;

	if (state->enabled && !mask) {
		switch (ch->unit) {
		case 0:
			outb(0, state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
			inb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
			break;
		case 1:
			outb(0, state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
			inb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
			break;
		}
	} else {
		inb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
		inb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
	}

	local_irq_restore(flags);
}

#ifdef CONFIG_BLK_DEV_IDEDMA_ICS
/*
 * SG-DMA support.
 *
 * Similar to the BM-DMA, but we use the RiscPCs IOMD DMA controllers.
 * There is only one DMA controller per card, which means that only
 * one drive can be accessed at one time.  NOTE! We do not enforce that
 * here, but we rely on the main IDE driver spotting that both
 * interfaces use the same IRQ, which should guarantee this.
 */
#define NR_ENTRIES 256
#define TABLE_SIZE (NR_ENTRIES * 8)

static int ide_build_sglist(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	struct scatterlist *sg = ch->sg_table;
	int nents;

	if ((rq->flags & REQ_SPECIAL) && (drive->type == ATA_DISK)) {
		struct ata_taskfile *args = rq->special;

		if (args->command_type == IDE_DRIVE_TASK_RAW_WRITE)
			ch->sg_dma_direction = PCI_DMA_TODEVICE;
		else
			ch->sg_dma_direction = PCI_DMA_FROMDEVICE;

		memset(sg, 0, sizeof(*sg));
		sg->page   = virt_to_page(rq->buffer);
		sg->offset = ((unsigned long)rq->buffer) & ~PAGE_MASK;
		sg->length = rq->nr_sectors * SECTOR_SIZE;
		nents = 1;
	} else {
		nents = blk_rq_map_sg(&drive->queue, rq, sg);

		if (rq->q && nents > rq->nr_phys_segments)
			printk("icside: received %d segments, build %d\n",
				rq->nr_phys_segments, nents);

		if (rq_data_dir(rq) == READ)
			ch->sg_dma_direction = PCI_DMA_FROMDEVICE;
		else
			ch->sg_dma_direction = PCI_DMA_TODEVICE;
	}

	return pci_map_sg(NULL, sg, nents, ch->sg_dma_direction);
}

/* Teardown mappings after DMA has completed.  */
static void icside_destroy_dmatable(struct ata_device *drive)
{
	struct scatterlist *sg = drive->channel->sg_table;
	int nents = drive->channel->sg_nents;

	pci_unmap_sg(NULL, sg, nents, drive->channel->sg_dma_direction);
}

/*
 * Configure the IOMD to give the appropriate timings for the transfer
 * mode being requested.  We take the advice of the ATA standards, and
 * calculate the cycle time based on the transfer mode, and the EIDE
 * MW DMA specs that the drive provides in the IDENTIFY command.
 *
 * We have the following IOMD DMA modes to choose from:
 *
 *	Type	Active		Recovery	Cycle
 *	A	250 (250)	312 (550)	562 (800)
 *	B	187		250		437
 *	C	125 (125)	125 (375)	250 (500)
 *	D	62		125		187
 *
 * (figures in brackets are actual measured timings)
 *
 * However, we also need to take care of the read/write active and
 * recovery timings:
 *
 *			Read	Write
 *  	Mode	Active	-- Recovery --	Cycle	IOMD type
 *	MW0	215	50	215	480	A
 *	MW1	80	50	50	150	C
 *	MW2	70	25	25	120	C
 */
static int
icside_config_if(struct ata_device *drive, int xfer_mode)
{
	int on = 0, cycle_time = 0, use_dma_info = 0;

	switch (xfer_mode) {
	case XFER_MW_DMA_2: cycle_time = 250; use_dma_info = 1;	break;
	case XFER_MW_DMA_1: cycle_time = 250; use_dma_info = 1;	break;
	case XFER_MW_DMA_0: cycle_time = 480;			break;
	}

	/*
	 * If we're going to be doing MW_DMA_1 or MW_DMA_2, we should
	 * take care to note the values in the ID...
	 */
	if (use_dma_info && drive->id->eide_dma_time > cycle_time)
		cycle_time = drive->id->eide_dma_time;

	drive->drive_data = cycle_time;

	if (cycle_time && ide_config_drive_speed(drive, xfer_mode) == 0)
		on = 1;
	else
		drive->drive_data = 480;

	printk("%s: %02x selected (peak %dMB/s)\n", drive->name,
		xfer_mode, 2000 / drive->drive_data);

	drive->current_speed = xfer_mode;

	return on;
}

static int icside_set_speed(struct ata_device *drive, byte speed)
{
	return icside_config_if(drive, speed);
}

static void icside_dma_enable(struct ata_device *drive, int on, int verbose)
{
	if (!on) {
		if (verbose)
			printk("%s: DMA disabled\n", drive->name);
#ifdef CONFIG_BLK_DEV_IDE_TCQ
		udma_tcq_enable(drive, 0);
#endif
	}

	/*
	 * We don't need any bouncing.  Yes, this looks the
	 * wrong way around, but it is correct.
	 */
	blk_queue_bounce_limit(&drive->queue, BLK_BOUNCE_ANY);
	drive->using_dma = on;

#ifdef CONFIG_CLK_DEV_IDE_TCQ_DEFAULT
	if (on)
		udma_tcq_enable(drive, 1);
#endif
}

static int icside_dma_check(struct ata_device *drive, int map)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *ch = drive->channel;
	int xfer_mode = XFER_PIO_2;
	int on;

	if (!id || !(id->capability & 1) || !ch->autodma)
		goto out;

	/*
	 * Enable DMA on any drive that has multiword DMA
	 */
	if (id->field_valid & 2) {
		if (id->dma_mword & 4) {
			xfer_mode = XFER_MW_DMA_2;
		} else if (id->dma_mword & 2) {
			xfer_mode = XFER_MW_DMA_1;
		} else if (id->dma_mword & 1) {
			xfer_mode = XFER_MW_DMA_0;
		}
	}

out:
	on = icside_config_if(drive, xfer_mode);

	icside_dma_enable(drive, on, 0);

	return 0;
}

static int icside_dma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;

	disable_dma(ch->hw.dma);
	icside_destroy_dmatable(drive);

	return get_dma_residue(ch->hw.dma) != 0;
}

static void icside_dma_start(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;

	/* We can not enable DMA on both channels simultaneously. */
	BUG_ON(dma_channel_active(ch->hw.dma));
	enable_dma(ch->hw.dma);
}

/*
 * dma_intr() is the handler for disk read/write DMA interrupts
 */
static ide_startstop_t icside_dmaintr(struct ata_device *drive, struct request *rq)
{
	int dma_stat;

	dma_stat = icside_dma_stop(drive);
	if (ata_status(drive, DRIVE_READY, drive->bad_wstat | DRQ_STAT)) {
		if (!dma_stat) {
			__ide_end_request(drive, rq, 1, rq->nr_sectors);
			return ATA_OP_FINISHED;
		}
		printk("%s: dma_intr: bad DMA status (dma_stat=%x)\n",
		       drive->name, dma_stat);
	}
	return ata_error(drive, rq, __FUNCTION__);
}

static int
icside_dma_common(struct ata_device *drive, struct request *rq,
		  unsigned int dma_mode)
{
	struct ata_channel *ch = drive->channel;
	unsigned int count;

	/*
	 * We can not enable DMA on both channels.
	 */
	BUG_ON(dma_channel_active(ch->hw.dma));

	count = ch->sg_nents = ide_build_sglist(drive, rq);
	if (!count)
		return 1;

	/*
	 * Route the DMA signals to to this channel.
	 */
	outb(ch->select_data, ch->config_data);

	/*
	 * Select the correct timing for this drive.
	 */
	set_dma_speed(ch->hw.dma, drive->drive_data);

	/*
	 * Tell the DMA engine about the SG table and
	 * data direction.
	 */
	set_dma_sg(ch->hw.dma, ch->sg_table, count);
	set_dma_mode(ch->hw.dma, dma_mode);

	return 0;
}

static int icside_dma_init(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	u8 int cmd;

	if (icside_dma_common(drive, rq, DMA_MODE_WRITE))
		return ATA_OP_FINISHED;

	if (drive->type != ATA_DISK)
		return ATA_OP_CONTINUES;

	ata_set_handler(drive, icside_dmaintr, WAIT_CMD, NULL);

	if ((rq->flags & REQ_SPECIAL) && drive->addressing == 1) {
		struct ata_taskfile *args = rq->special;
		cmd = args->cmd;
	} else if (drive->addressing) {
		cmd = rq_data_dir(rq) == WRITE ? WIN_WRITEDMA_EXT : WIN_READDMA_EXT;
	} else {
		cmd = rq_data_dir(rq) == WRITE ? WIN_WRITEDMA : WIN_READDMA;
	}
	OUT_BYTE(cmd, IDE_COMMAND_REG);

	enable_dma(ch->hw.dma);

	return ATA_OP_CONTINUES;
}

static int icside_irq_status(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	struct icside_state *state = ch->hw.priv;

	return inb(state->irq_port +
		   (ch->unit ?
			ICS_ARCIN_V6_INTRSTAT_2 :
			ICS_ARCIN_V6_INTRSTAT_1)) & 1;
}

static void icside_dma_timeout(struct ata_device *drive)
{
	printk(KERN_ERR "ATA: %s: UDMA timeout occured:", drive->name);
	ata_status(drive, 0, 0);
	ata_dump(drive, NULL, "UDMA timeout");
}

static void icside_irq_lost(struct ata_device *drive)
{
	printk(KERN_ERR "ATA: %s: IRQ lost\n", drive->name);
}

static int icside_setup_dma(struct ata_channel *ch)
{
	int autodma = 0;

#ifdef CONFIG_IDEDMA_ICS_AUTO
	autodma = 1;
#endif

	printk("    %s: SG-DMA", ch->name);

	ch->sg_table = kmalloc(sizeof(struct scatterlist) * NR_ENTRIES,
				 GFP_KERNEL);
	if (!ch->sg_table)
		goto failed;

	ch->dmatable_cpu    = NULL;
	ch->dmatable_dma    = 0;
	ch->speedproc       = icside_set_speed;
	ch->udma_setup	    = icside_dma_check;
	ch->udma_enable     = icside_dma_enable;
	ch->udma_start      = icside_dma_start;
	ch->udma_stop       = icside_dma_stop;
	ch->udma_init	    = icside_dma_init;
	ch->udma_irq_status = icside_irq_status;
	ch->udma_timeout    = icside_dma_timeout;
	ch->udma_irq_lost   = icside_irq_lost;
	ch->autodma         = autodma;

	printk(" capable%s\n", autodma ? ", auto-enable" : "");

	return 1;

failed:
	printk(" disabled, unable to allocate DMA table\n");
	return 0;
}

void ide_release_dma(struct ata_channel *ch)
{
	if (ch->sg_table) {
		kfree(ch->sg_table);
		ch->sg_table = NULL;
	}
}
#endif

static struct ata_channel *icside_find_hwif(unsigned long dataport)
{
	struct ata_channel *ch;
	int index;

	for (index = 0; index < MAX_HWIFS; ++index) {
		ch = &ide_hwifs[index];
		if (ch->io_ports[IDE_DATA_OFFSET] == (ide_ioreg_t)dataport)
			goto found;
	}

	for (index = 0; index < MAX_HWIFS; ++index) {
		ch = &ide_hwifs[index];
		if (!ch->io_ports[IDE_DATA_OFFSET])
			goto found;
	}

	return NULL;
found:
	return ch;
}

static struct ata_channel *
icside_setup(unsigned long base, struct cardinfo *info, int irq)
{
	unsigned long port = base + info->dataoffset;
	struct ata_channel *ch;

	ch = icside_find_hwif(base);
	if (ch) {
		int i;

		memset(&ch->hw, 0, sizeof(hw_regs_t));

		for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
			ch->hw.io_ports[i] = (ide_ioreg_t)port;
			ch->io_ports[i] = (ide_ioreg_t)port;
			port += 1 << info->stepping;
		}
		ch->hw.io_ports[IDE_CONTROL_OFFSET] = base + info->ctrloffset;
		ch->io_ports[IDE_CONTROL_OFFSET] = base + info->ctrloffset;
		ch->hw.irq  = irq;
		ch->irq     = irq;
		ch->hw.dma  = NO_DMA;
		ch->noprobe = 0;
		ch->chipset = ide_acorn;
	}

	return ch;
}

static int __init icside_register_v5(struct expansion_card *ec)
{
	unsigned long slot_port;
	struct ata_channel *ch;

	slot_port = ecard_address(ec, ECARD_MEMC, 0);

	ec->irqaddr  = (unsigned char *)ioaddr(slot_port + ICS_ARCIN_V5_INTRSTAT);
	ec->irqmask  = 1;
	ec->irq_data = (void *)slot_port;
	ec->ops      = (expansioncard_ops_t *)&icside_ops_arcin_v5;

	/*
	 * Be on the safe side - disable interrupts
	 */
	inb(slot_port + ICS_ARCIN_V5_INTROFFSET);

	ch = icside_setup(slot_port, &icside_cardinfo_v5, ec->irq);

	return ch ? 0 : -1;
}

static int __init icside_register_v6(struct expansion_card *ec)
{
	unsigned long slot_port, port;
	struct icside_state *state;
	struct ata_channel *ch0, *ch1;
	unsigned int sel = 0;

	slot_port = ecard_address(ec, ECARD_IOC, ECARD_FAST);
	port      = ecard_address(ec, ECARD_EASI, ECARD_FAST);

	if (port == 0)
		port = slot_port;
	else
		sel = 1 << 5;

	outb(sel, slot_port);

	/*
	 * Be on the safe side - disable interrupts
	 */
	inb(port + ICS_ARCIN_V6_INTROFFSET_1);
	inb(port + ICS_ARCIN_V6_INTROFFSET_2);

	/*
	 * Find and register the interfaces.
	 */
	ch0 = icside_setup(port, &icside_cardinfo_v6_1, ec->irq);
	ch1 = icside_setup(port, &icside_cardinfo_v6_2, ec->irq);

	if (!ch0 || !ch1)
		return -ENODEV;

	state = kmalloc(sizeof(struct icside_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->channel    = 0;
	state->enabled    = 0;
	state->irq_port   = port;

	ec->irq_data      = state;
	ec->ops           = (expansioncard_ops_t *)&icside_ops_arcin_v6;

	ch0->maskproc     = icside_maskproc;
	ch0->unit         = 0;
	ch0->hw.priv      = state;
	ch0->config_data  = slot_port;
	ch0->select_data  = sel;
	ch0->hw.dma       = ec->dma;

	ch1->maskproc     = icside_maskproc;
	ch1->unit         = 1;
	ch1->hw.priv      = state;
	ch1->config_data  = slot_port;
	ch1->select_data  = sel | 1;
	ch1->hw.dma       = ec->dma;

#ifdef CONFIG_BLK_DEV_IDEDMA_ICS
	if (ec->dma != NO_DMA && !request_dma(ec->dma, ch0->name)) {
		icside_setup_dma(ch0);
		icside_setup_dma(ch1);
	}
#endif
	return 0;
}

int __init icside_init(void)
{
	ecard_startfind ();

	do {
		struct expansion_card *ec;
		int result;

		ec = ecard_find(0, icside_cids);
		if (ec == NULL)
			break;

		ecard_claim(ec);

		switch (icside_identifyif(ec)) {
		case ics_if_arcin_v5:
			result = icside_register_v5(ec);
			break;

		case ics_if_arcin_v6:
			result = icside_register_v6(ec);
			break;

		default:
			result = -1;
			break;
		}

		if (result)
			ecard_release(ec);
	} while (1);

	return 0;
}

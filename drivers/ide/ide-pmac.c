/*
 * linux/drivers/ide/ide-pmac.c		Version ?.??	Mar. 18, 2000
 *
 * Support for IDE interfaces on PowerMacs.
 * These IDE interfaces are memory-mapped and have a DBDMA channel
 * for doing DMA.
 *
 *  Copyright (C) 1998 Paul Mackerras.
 *
 *  Bits from Benjamin Herrenschmidt
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * Some code taken from drivers/ide/ide-dma.c:
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/ide.h>
#include <asm/mediabay.h>
#include <asm/feature.h>
#ifdef CONFIG_PMAC_PBOOK
#include <linux/adb.h>
#include <linux/pmu.h>
#include <asm/irq.h>
#endif
#include "ide_modes.h"

extern char *ide_dmafunc_verbose(ide_dma_action_t dmafunc);

#undef IDE_PMAC_DEBUG

#define IDE_SYSCLK_NS		30
#define IDE_SYSCLK_ULTRA_PS	0x1d4c /* (15 * 1000 / 2)*/

struct pmac_ide_hwif {
	ide_ioreg_t			regbase;
	int				irq;
	int				kind;
	int				aapl_bus_id;
	struct device_node*		node;
	u32				timings[2];
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
	volatile struct dbdma_regs*	dma_regs;
	struct dbdma_cmd*		dma_table;
#endif
	
} pmac_ide[MAX_HWIFS];

static int pmac_ide_count;

enum {
	controller_ohare,	/* OHare based */
	controller_heathrow,	/* Heathrow/Paddington */
	controller_kl_ata3,	/* KeyLargo ATA-3 */
	controller_kl_ata4	/* KeyLargo ATA-4 */
};


#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

typedef struct {
	int	accessTime;
	int	cycleTime;
} pmac_ide_timing;

/* Multiword DMA timings */
static pmac_ide_timing mdma_timings[] =
{
    { 215,    480 },	/* Mode 0 */
    {  80,    150 },	/*      1 */
    {  70,    120 }	/*      2 */
};

/* Ultra DMA timings (for use when I know how to calculate them */
static pmac_ide_timing udma_timings[] =
{
    {   0,    114 },	/* Mode 0 */
    {   0,     75 },	/*      1 */
    {   0,     55 },	/*      2 */
    {   100,   45 },	/*      3 */
    {   100,   25 }	/*      4 */
};

/* allow up to 256 DBDMA commands per xfer */
#define MAX_DCMDS		256

/* Wait 1.5s for disk to answer on IDE bus after
 * enable operation.
 * NOTE: There is at least one case I know of a disk that needs about 10sec
 *       before anwering on the bus. I beleive we could add a kernel command
 *       line arg to override this delay for such cases.
 */
#define IDE_WAKEUP_DELAY_MS	1500

static void pmac_ide_setup_dma(struct device_node *np, int ix);
static int pmac_ide_dmaproc(ide_dma_action_t func, ide_drive_t *drive);
static int pmac_ide_build_dmatable(ide_drive_t *drive, int ix, int wr);
static int pmac_ide_tune_chipset(ide_drive_t *drive, byte speed);
static void pmac_ide_tuneproc(ide_drive_t *drive, byte pio);
static void pmac_ide_selectproc(ide_drive_t *drive);

#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

#ifdef CONFIG_PMAC_PBOOK
static int idepmac_notify_sleep(struct pmu_sleep_notifier *self, int when);
struct pmu_sleep_notifier idepmac_sleep_notifier = {
	idepmac_notify_sleep, SLEEP_LEVEL_BLOCK,
};
#endif /* CONFIG_PMAC_PBOOK */

static int
pmac_ide_find(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	ide_ioreg_t base;
	int i;
	
	for (i=0; i<pmac_ide_count; i++) {
		base = pmac_ide[i].regbase;
		if (base && base == hwif->io_ports[0])
			return i;
	}
	return -1;
}

/*
 * N.B. this can't be an initfunc, because the media-bay task can
 * call ide_[un]register at any time.
 */
void pmac_ide_init_hwif_ports(hw_regs_t *hw,
			      ide_ioreg_t data_port, ide_ioreg_t ctrl_port,
			      int *irq)
{
	int i, ix;

	if (data_port == 0)
		return;

	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (data_port == pmac_ide[ix].regbase)
			break;

	if (ix >= MAX_HWIFS) {
		/* Probably a PCI interface... */
		for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; ++i)
			hw->io_ports[i] = data_port + i - IDE_DATA_OFFSET;
		/* XXX is this right? */
		hw->io_ports[IDE_CONTROL_OFFSET] = 0;
		if (irq != 0)
			*irq = 0;
		return;
	}

	for (i = 0; i < 8; ++i)
		hw->io_ports[i] = data_port + i * 0x10;
	hw->io_ports[8] = data_port + 0x160;

	if (irq != NULL)
		*irq = pmac_ide[ix].irq;

	ide_hwifs[ix].tuneproc = pmac_ide_tuneproc;
	ide_hwifs[ix].selectproc = pmac_ide_selectproc;
	if (pmac_ide[ix].dma_regs && pmac_ide[ix].dma_table) {
		ide_hwifs[ix].dmaproc = &pmac_ide_dmaproc;
#ifdef CONFIG_PMAC_IDEDMA_AUTO
		ide_hwifs[ix].autodma = 1;
#endif
//		ide_hwifs[ix].speedproc = &pmac_ide_tune_chipset;
	}
}

#if 0
/* This one could be later extended to handle CMD IDE and be used by some kind
 * of /proc interface. I want to be able to get the devicetree path of a block
 * device for yaboot configuration
 */
struct device_node*
pmac_ide_get_devnode(ide_drive_t *drive)
{
	int i = pmac_ide_find(drive);
	if (i < 0)
		return NULL;
	return pmac_ide[i].node;
}
#endif

/* Setup timings for the selected drive (master/slave). I still need to verify if this
 * is enough, I beleive selectproc will be called whenever an IDE command is started,
 * but... */
static void
pmac_ide_selectproc(ide_drive_t *drive)
{
	int i = pmac_ide_find(drive);
	if (i < 0)
		return;
			
	if (drive->select.all & 0x10)
		out_le32((unsigned *)(IDE_DATA_REG + 0x200 + _IO_BASE), pmac_ide[i].timings[1]);
	else
		out_le32((unsigned *)(IDE_DATA_REG + 0x200 + _IO_BASE), pmac_ide[i].timings[0]);
}

/* Number of IDE_SYSCLK_NS ticks, argument is in nanoseconds */
#define SYSCLK_TICKS(t)		(((t) + IDE_SYSCLK_NS - 1) / IDE_SYSCLK_NS)
#define SYSCLK_TICKS_UDMA(t)	(((t) + IDE_SYSCLK_ULTRA_PS - 1) / IDE_SYSCLK_ULTRA_PS)

/* Calculate PIO timings */
static void
pmac_ide_tuneproc(ide_drive_t *drive, byte pio)
{
	ide_pio_data_t d;
	int i;
	u32 *timings;
	int accessTicks, recTicks;
	
	i = pmac_ide_find(drive);
	if (i < 0)
		return;
		
	pio = ide_get_best_pio_mode(drive, pio, 4, &d);
	accessTicks = SYSCLK_TICKS(ide_pio_timings[pio].active_time);
	if (drive->select.all & 0x10)
		timings = &pmac_ide[i].timings[1];
	else
		timings = &pmac_ide[i].timings[0];
	
	if (pmac_ide[i].kind == controller_kl_ata4) {
		/* The "ata-4" IDE controller of Core99 machines */
		accessTicks = SYSCLK_TICKS_UDMA(ide_pio_timings[pio].active_time * 1000);
		recTicks = SYSCLK_TICKS_UDMA(d.cycle_time * 1000) - accessTicks;

		*timings = ((*timings) & 0x1FFFFFC00) | accessTicks | (recTicks << 5);
	} else {
		/* The old "ata-3" IDE controller */
		accessTicks = SYSCLK_TICKS(ide_pio_timings[pio].active_time);
		if (accessTicks < 4)
			accessTicks = 4;
		recTicks = SYSCLK_TICKS(d.cycle_time) - accessTicks - 4;
		if (recTicks < 1)
			recTicks = 1;
	
		*timings = ((*timings) & 0xFFFFFF800) | accessTicks | (recTicks << 5);
	}

#ifdef IDE_PMAC_DEBUG
	printk("ide_pmac: Set PIO timing for mode %d, reg: 0x%08x\n",
		pio,  *timings);
#endif	
		
	if (drive->select.all == IN_BYTE(IDE_SELECT_REG))
		pmac_ide_selectproc(drive);
}

ide_ioreg_t
pmac_ide_get_base(int index)
{
	return pmac_ide[index].regbase;
}

static int ide_majors[] = { 3, 22, 33, 34, 56, 57 };

kdev_t __init
pmac_find_ide_boot(char *bootdevice, int n)
{
	int i;
	
	/*
	 * Look through the list of IDE interfaces for this one.
	 */
	for (i = 0; i < pmac_ide_count; ++i) {
		char *name;
		if (!pmac_ide[i].node || !pmac_ide[i].node->full_name)
			continue;
		name = pmac_ide[i].node->full_name;
		if (memcmp(name, bootdevice, n) == 0 && name[n] == 0) {
			/* XXX should cope with the 2nd drive as well... */
			return MKDEV(ide_majors[i], 0);
		}
	}

	return 0;
}

void __init
pmac_ide_probe(void)
{
	struct device_node *np;
	int i;
	struct device_node *atas;
	struct device_node *p, **pp, *removables, **rp;
	unsigned long base;
	int irq, big_delay;
	ide_hwif_t *hwif;

	if (_machine != _MACH_Pmac)
		return;
	pp = &atas;
	rp = &removables;
	p = find_devices("ATA");
	if (p == NULL)
		p = find_devices("IDE");
	if (p == NULL)
		p = find_type_devices("ide");
	if (p == NULL)
		p = find_type_devices("ata");
	/* Move removable devices such as the media-bay CDROM
	   on the PB3400 to the end of the list. */
	for (; p != NULL; p = p->next) {
		if (p->parent && p->parent->type
		    && strcasecmp(p->parent->type, "media-bay") == 0) {
			*rp = p;
			rp = &p->next;
		} else {
			*pp = p;
			pp = &p->next;
		}
	}
	*rp = NULL;
	*pp = removables;
	big_delay = 0;

	for (i = 0, np = atas; i < MAX_HWIFS && np != NULL; np = np->next) {
		struct device_node *tp;
		int *bidp;

		/*
		 * If this node is not under a mac-io or dbdma node,
		 * leave it to the generic PCI driver.
		 */
		for (tp = np->parent; tp != 0; tp = tp->parent)
			if (tp->type && (strcmp(tp->type, "mac-io") == 0
					 || strcmp(tp->type, "dbdma") == 0))
				break;
		if (tp == 0)
			continue;

		if (np->n_addrs == 0) {
			printk(KERN_WARNING "ide: no address for device %s\n",
			       np->full_name);
			continue;
		}

		/*
		 * If this slot is taken (e.g. by ide-pci.c) try the next one.
		 */
		while (i < MAX_HWIFS
		       && ide_hwifs[i].io_ports[IDE_DATA_OFFSET] != 0)
			++i;
		if (i >= MAX_HWIFS)
			break;

		base = (unsigned long) ioremap(np->addrs[0].address, 0x200) - _IO_BASE;

		/* XXX This is bogus. Should be fixed in the registry by checking
		   the kind of host interrupt controller, a bit like gatwick
		   fixes in irq.c
		 */
		if (np->n_intrs == 0) {
			printk("ide: no intrs for device %s, using 13\n",
			       np->full_name);
			irq = 13;
		} else {
			irq = np->intrs[0].line;
		}
		pmac_ide[i].regbase = base;
		pmac_ide[i].irq = irq;
		pmac_ide[i].node = np;
		if (device_is_compatible(np, "keylargo-ata")) {
			if (strcmp(np->name, "ata-4") == 0)
				pmac_ide[i].kind = controller_kl_ata4;
			else
				pmac_ide[i].kind = controller_kl_ata3;
		} else if (device_is_compatible(np, "heathrow-ata"))
			pmac_ide[i].kind = controller_heathrow;
		else
			pmac_ide[i].kind = controller_ohare;

		bidp = (int *)get_property(np, "AAPL,bus-id", NULL);
		pmac_ide[i].aapl_bus_id =  bidp ? *bidp : 0;

		if (np->parent && np->parent->name
		    && strcasecmp(np->parent->name, "media-bay") == 0) {
			media_bay_set_ide_infos(np->parent,base,irq,i);
		} else if (pmac_ide[i].kind == controller_ohare) {
			/* The code below is having trouble on some ohare machines
			 * (timing related ?). Until I can put my hand on one of these
			 * units, I keep the old way
			 */
			 feature_set(np, FEATURE_IDE0_enable);
		} else {
 			/* This is necessary to enable IDE when net-booting */
			printk("pmac_ide: enabling IDE bus ID %d\n",
				pmac_ide[i].aapl_bus_id);
			switch(pmac_ide[i].aapl_bus_id) {
			    case 0:
				feature_set(np, FEATURE_IDE0_reset);
				mdelay(10);
 				feature_set(np, FEATURE_IDE0_enable);
				mdelay(10);
				feature_clear(np, FEATURE_IDE0_reset);
				break;
			    case 1:
				feature_set(np, FEATURE_IDE1_reset);
				mdelay(10);
 				feature_set(np, FEATURE_IDE1_enable);
				mdelay(10);
				feature_clear(np, FEATURE_IDE1_reset);
				break;
			    case 2:
			    	/* This one exists only for KL, I don't know
				   about any enable bit */
				feature_set(np, FEATURE_IDE2_reset);
				mdelay(10);
				feature_clear(np, FEATURE_IDE2_reset);
				break;
			}
			big_delay = 1;
		}

		hwif = &ide_hwifs[i];
		pmac_ide_init_hwif_ports(&hwif->hw, base, 0, &hwif->irq);
		memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
		hwif->chipset = ide_pmac;
		hwif->noprobe = (!hwif->io_ports[IDE_DATA_OFFSET]) ||
			(check_media_bay_by_base(base, MB_CD) == -EINVAL);

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
		if (np->n_addrs >= 2) {
			/* has a DBDMA controller channel */
			pmac_ide_setup_dma(np, i);
		}
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

		++i;
	}
	pmac_ide_count = i;
	if (big_delay)
		mdelay(IDE_WAKEUP_DELAY_MS);

#ifdef CONFIG_PMAC_PBOOK
	pmu_register_sleep_notifier(&idepmac_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

static void __init 
pmac_ide_setup_dma(struct device_node *np, int ix)
{
	pmac_ide[ix].dma_regs =
		(volatile struct dbdma_regs*)ioremap(np->addrs[1].address, 0x200);

	/*
	 * Allocate space for the DBDMA commands.
	 * The +2 is +1 for the stop command and +1 to allow for
	 * aligning the start address to a multiple of 16 bytes.
	 */
	pmac_ide[ix].dma_table = (struct dbdma_cmd*)
	       kmalloc((MAX_DCMDS + 2) * sizeof(struct dbdma_cmd), GFP_KERNEL);
	if (pmac_ide[ix].dma_table == 0) {
		printk(KERN_ERR "%s: unable to allocate DMA command list\n",
		       ide_hwifs[ix].name);
		return;
	}

	ide_hwifs[ix].dmaproc = &pmac_ide_dmaproc;
#ifdef CONFIG_PMAC_IDEDMA_AUTO
	ide_hwifs[ix].autodma = 1;
#endif
}

/*
 * pmac_ide_build_dmatable builds the DBDMA command list
 * for a transfer and sets the DBDMA channel to point to it.
 */
static int
pmac_ide_build_dmatable(ide_drive_t *drive, int ix, int wr)
{
	struct dbdma_cmd *table, *tstart;
	int count = 0;
	struct request *rq = HWGROUP(drive)->rq;
	struct buffer_head *bh = rq->bh;
	unsigned int size, addr;
	volatile struct dbdma_regs *dma = pmac_ide[ix].dma_regs;

	table = tstart = (struct dbdma_cmd *) DBDMA_ALIGN(pmac_ide[ix].dma_table);
	out_le32(&dma->control, (RUN|PAUSE|FLUSH|WAKE|DEAD) << 16);
	while (in_le32(&dma->status) & RUN)
		udelay(1);

	do {
		/*
		 * Determine addr and size of next buffer area.  We assume that
		 * individual virtual buffers are always composed linearly in
		 * physical memory.  For example, we assume that any 8kB buffer
		 * is always composed of two adjacent physical 4kB pages rather
		 * than two possibly non-adjacent physical 4kB pages.
		 */
		if (bh == NULL) {  /* paging requests have (rq->bh == NULL) */
			addr = virt_to_bus(rq->buffer);
			size = rq->nr_sectors << 9;
		} else {
			/* group sequential buffers into one large buffer */
			addr = virt_to_bus(bh->b_data);
			size = bh->b_size;
			while ((bh = bh->b_reqnext) != NULL) {
				if ((addr + size) != virt_to_bus(bh->b_data))
					break;
				size += bh->b_size;
			}
		}

		/*
		 * Fill in the next DBDMA command block.
		 * Note that one DBDMA command can transfer
		 * at most 65535 bytes.
		 */
		while (size) {
			unsigned int tc = (size < 0xfe00)? size: 0xfe00;

			if (++count >= MAX_DCMDS) {
				printk("%s: DMA table too small\n",
				       drive->name);
				return 0; /* revert to PIO for this request */
			}
			st_le16(&table->command, wr? OUTPUT_MORE: INPUT_MORE);
			st_le16(&table->req_count, tc);
			st_le32(&table->phy_addr, addr);
			table->cmd_dep = 0;
			table->xfer_status = 0;
			table->res_count = 0;
			addr += tc;
			size -= tc;
			++table;
		}
	} while (bh != NULL);

	/* convert the last command to an input/output last command */
	if (count)
		st_le16(&table[-1].command, wr? OUTPUT_LAST: INPUT_LAST);
	else
		printk(KERN_DEBUG "%s: empty DMA table?\n", drive->name);

	/* add the stop command to the end of the list */
	memset(table, 0, sizeof(struct dbdma_cmd));
	out_le16(&table->command, DBDMA_STOP);

	out_le32(&dma->cmdptr, virt_to_bus(tstart));
	return 1;
}


/* This is fun.  -DaveM */
#define IDE_SETXFER		0x03
#define IDE_SETFEATURE		0xef
#define IDE_DMA2_ENABLE		0x22
#define IDE_DMA1_ENABLE		0x21
#define IDE_DMA0_ENABLE		0x20
#define IDE_UDMA4_ENABLE	0x44
#define IDE_UDMA3_ENABLE	0x43
#define IDE_UDMA2_ENABLE	0x42
#define IDE_UDMA1_ENABLE	0x41
#define IDE_UDMA0_ENABLE	0x40

static __inline__ unsigned char
dma_bits_to_command(unsigned char bits)
{
	if(bits & 0x04)
		return IDE_DMA2_ENABLE;
	if(bits & 0x02)
		return IDE_DMA1_ENABLE;
	return IDE_DMA0_ENABLE;
}

static __inline__ unsigned char
udma_bits_to_command(unsigned char bits)
{
	if(bits & 0x10)
		return IDE_UDMA4_ENABLE;
	if(bits & 0x08)
		return IDE_UDMA3_ENABLE;
	if(bits & 0x04)
		return IDE_UDMA2_ENABLE;
	if(bits & 0x02)
		return IDE_UDMA1_ENABLE;
	if(bits & 0x01)
		return IDE_UDMA0_ENABLE;
	return 0;
}

static __inline__ int
wait_for_ready(ide_drive_t *drive)
{
	/* Timeout bumped for some powerbooks */
	int timeout = 2000;
	byte stat;

	while(--timeout) {
		stat = GET_STAT();
		if(!(stat & BUSY_STAT)) {
			if (drive->ready_stat == 0)
				break;
			else if((stat & drive->ready_stat) || (stat & ERR_STAT))
				break;
		}
		mdelay(1);
	}
	if((stat & ERR_STAT) || timeout <= 0) {
		if (stat & ERR_STAT) {
			printk("ide_pmace: wait_for_ready, error status: %x\n", stat);
		}
		return 1;
	}
	return 0;
}

static int
pmac_ide_do_setfeature(ide_drive_t *drive, byte command)
{
	unsigned long flags;
	byte old_select;
	int result = 1;

	save_flags(flags);
	cli();
	old_select = IN_BYTE(IDE_SELECT_REG);
	OUT_BYTE(drive->select.all, IDE_SELECT_REG);
	udelay(10);
	OUT_BYTE(IDE_SETXFER, IDE_FEATURE_REG);
	OUT_BYTE(command, IDE_NSECTOR_REG);
	if(wait_for_ready(drive)) {
		printk("pmac_ide_do_setfeature disk not ready before SET_FEATURE!\n");
		goto out;
	}
	OUT_BYTE(IDE_SETFEATURE, IDE_COMMAND_REG);
	result = wait_for_ready(drive);
	if (result)
		printk("pmac_ide_do_setfeature disk not ready after SET_FEATURE !\n");
out:
	OUT_BYTE(old_select, IDE_SELECT_REG);
	restore_flags(flags);
	
	return result;
}

/* Calculate MultiWord DMA timings */
static int
pmac_ide_mdma_enable(ide_drive_t *drive, int idx)
{
	byte bits = drive->id->dma_mword & 0x07;
	byte feature = dma_bits_to_command(bits);
	u32 *timings;
	int cycleTime, accessTime;
	int accessTicks, recTicks;
	struct hd_driveid *id = drive->id;
	
	/* Set feature on drive */
    	printk("%s: Enabling MultiWord DMA %d\n", drive->name, feature & 0xf);
	if (pmac_ide_do_setfeature(drive, feature)) {
	    	printk("%s: Failed !\n", drive->name);
	    	return 0;
	}

	if (!drive->init_speed)
		drive->init_speed = feature;
	
	/* which drive is it ? */
	if (drive->select.all & 0x10)
		timings = &pmac_ide[idx].timings[1];
	else
		timings = &pmac_ide[idx].timings[0];

	/* Calculate accesstime and cycle time */
	cycleTime = mdma_timings[feature & 0xf].cycleTime;
	accessTime = mdma_timings[feature & 0xf].accessTime;
	if ((id->field_valid & 2) && (id->eide_dma_time))
		cycleTime = id->eide_dma_time;
	if ((pmac_ide[idx].kind == controller_ohare) && (cycleTime < 150))
		cycleTime = 150;

	/* For ata-4 controller, we don't know the calculation */
	if (pmac_ide[idx].kind == controller_kl_ata4) {
		accessTicks = SYSCLK_TICKS_UDMA(accessTime * 1000);
		recTicks = SYSCLK_TICKS_UDMA(cycleTime * 1000) - accessTicks;
		*timings = ((*timings) & 0xffe003ff) |
			(accessTicks | (recTicks << 5)) << 10;
	} else {
		int halfTick = 0;
		int origAccessTime = accessTime;
		int origCycleTime = cycleTime;
		
		accessTicks = SYSCLK_TICKS(accessTime);
		if (accessTicks < 1)
			accessTicks = 1;
		accessTime = accessTicks * IDE_SYSCLK_NS;
		recTicks = SYSCLK_TICKS(cycleTime - accessTime) - 1;
		if (recTicks < 1)
			recTicks = 1;
		cycleTime = (recTicks + 1 + accessTicks) * IDE_SYSCLK_NS;

		/* KeyLargo ata-3 don't support the half-tick stuff */
		if ((pmac_ide[idx].kind != controller_kl_ata3) &&
			(accessTicks > 1) &&
			((accessTime - IDE_SYSCLK_NS/2) >= origAccessTime) &&
			((cycleTime - IDE_SYSCLK_NS) >= origCycleTime)) {
            			halfTick    = 1;
				accessTicks--;
		}
		*timings = ((*timings) & 0x7FF) |
			(accessTicks | (recTicks << 5) | (halfTick << 10)) << 11;
	}
#ifdef IDE_PMAC_DEBUG
	printk("ide_pmac: Set MDMA timing for mode %d, reg: 0x%08x\n",
		feature & 0xf, *timings);
#endif
	drive->current_speed = feature;	
	return 1;
}

/* Calculate Ultra DMA timings */
static int
pmac_ide_udma_enable(ide_drive_t *drive, int idx)
{
	byte bits = drive->id->dma_ultra & 0x1f;
	byte feature = udma_bits_to_command(bits);
	int cycleTime, accessTime;
	int rdyToPauseTicks, cycleTicks;
	u32 *timings;
	
	/* Set feature on drive */
    	printk("%s: Enabling Ultra DMA %d\n", drive->name, feature & 0xf);
	if (pmac_ide_do_setfeature(drive, feature)) {
		printk("%s: Failed !\n", drive->name);
		return 0;
	}

	if (!drive->init_speed)
		drive->init_speed = feature;

	/* which drive is it ? */
	if (drive->select.all & 0x10)
		timings = &pmac_ide[idx].timings[1];
	else
		timings = &pmac_ide[idx].timings[0];

	cycleTime = udma_timings[feature & 0xf].cycleTime;
	accessTime = udma_timings[feature & 0xf].accessTime;

	rdyToPauseTicks = SYSCLK_TICKS_UDMA(accessTime * 1000);
	cycleTicks = SYSCLK_TICKS_UDMA(cycleTime * 1000);

	*timings = ((*timings) & 0xe00fffff) |
			((cycleTicks << 1) | (rdyToPauseTicks << 5) | 1) << 20;

	drive->current_speed = feature;	
	return 1;
}

static int
pmac_ide_dma_onoff(ide_drive_t *drive, int enable)
{
	int ata4, udma, idx;
	struct hd_driveid *id = drive->id;

	drive->using_dma = 0;
	
	idx = pmac_ide_find(drive);
	if (idx < 0)
		return 0;
		
	if (drive->media == ide_floppy)
		enable = 0;
	if (((id->capability & 1) == 0) && !check_drive_lists(drive, GOOD_DMA_DRIVE))
		enable = 0;
	if (check_drive_lists(drive, BAD_DMA_DRIVE))
		enable = 0;

	udma = 0;
	ata4 = (pmac_ide[idx].kind == controller_kl_ata4);
			
	if(enable) {
		if (ata4 && (drive->media == ide_disk) &&
		    (id->field_valid & 0x0004) && (id->dma_ultra & 0x17)) {
			/* UltraDMA modes. */
			drive->using_dma = pmac_ide_udma_enable(drive, idx);
		}
		if (!drive->using_dma && (id->dma_mword & 0x0007)) {
			/* Normal MultiWord DMA modes. */
			drive->using_dma = pmac_ide_mdma_enable(drive, idx);
		}
		/* Without this, strange things will happen on Keylargo-based
		 * machines
		 */
		OUT_BYTE(0, IDE_CONTROL_REG);
		if (drive->select.all == IN_BYTE(IDE_SELECT_REG))
			pmac_ide_selectproc(drive);
	}
	return 0;
}

int pmac_ide_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int ix, dstat;
	volatile struct dbdma_regs *dma;

	/* Can we stuff a pointer to our intf structure in config_data
	 * or select_data in hwif ?
	 */
	ix = pmac_ide_find(drive);
	if (ix < 0)
		return 0;		
	dma = pmac_ide[ix].dma_regs;

	switch (func) {
	case ide_dma_on:
	case ide_dma_off:
	case ide_dma_off_quietly:
		pmac_ide_dma_onoff(drive, (func == ide_dma_on));
		break;
	case ide_dma_check:
		printk("IDE-DMA check !\n");
		if (hwif->autodma)
			pmac_ide_dma_onoff(drive, 1);
		break;
	case ide_dma_read:
	case ide_dma_write:
		if (!pmac_ide_build_dmatable(drive, ix, func==ide_dma_write))
			return 1;
		drive->waiting_for_dma = 1;
		if (drive->media != ide_disk)
			return 0;
		ide_set_handler(drive, &ide_dma_intr, WAIT_CMD, NULL);
		OUT_BYTE(func==ide_dma_write? WIN_WRITEDMA: WIN_READDMA,
			 IDE_COMMAND_REG);
	case ide_dma_begin:
		out_le32(&dma->control, (RUN << 16) | RUN);
		break;
	case ide_dma_end:
		drive->waiting_for_dma = 0;
		dstat = in_le32(&dma->status);
		out_le32(&dma->control, ((RUN|WAKE|DEAD) << 16));
		/* verify good dma status */
		return (dstat & (RUN|DEAD|ACTIVE)) != RUN;
	case ide_dma_test_irq:
		return (in_le32(&dma->status) & (RUN|ACTIVE)) == RUN;

		/* Let's implement tose just in case someone wants them */
	case ide_dma_bad_drive:
	case ide_dma_good_drive:
		return check_drive_lists(drive, (func == ide_dma_good_drive));
	case ide_dma_verbose:
		return report_drive_dmaing(drive);
	case ide_dma_retune:
	case ide_dma_lostirq:
	case ide_dma_timeout:
		printk("ide_pmac_dmaproc: chipset supported %s func only: %d\n", ide_dmafunc_verbose(func),  func);
		return 1;
	default:
		printk("ide_pmac_dmaproc: unsupported %s func: %d\n", ide_dmafunc_verbose(func), func);
		return 1;
	}
	return 0;
}
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

#ifdef CONFIG_PMAC_PBOOK
static void idepmac_sleep_disk(int i, unsigned long base)
{
	struct device_node* np = pmac_ide[i].node;
	int j;

	/* FIXME: We only handle the master IDE */
	if (ide_hwifs[i].drives[0].media == ide_disk) {
		/* Spin down the drive */
		outb(0xa0, base+0x60);
		outb(0x0, base+0x30);
		outb(0x0, base+0x20);
		outb(0x0, base+0x40);
		outb(0x0, base+0x50);
		outb(0xe0, base+0x70);
		outb(0x2, base+0x160);   
		for (j = 0; j < 10; j++) {
			int status;
			mdelay(100);
			status = inb(base+0x70);
			if (!(status & BUSY_STAT) && (status & DRQ_STAT))
				break;
		}
	}
	feature_set(np, FEATURE_IDE0_reset);
	feature_clear(np, FEATURE_IDE0_enable);
	switch(pmac_ide[i].aapl_bus_id) {
	    case 0:
		feature_set(np, FEATURE_IDE0_reset);
		feature_clear(np, FEATURE_IDE0_enable);
		break;
	    case 1:
		feature_set(np, FEATURE_IDE1_reset);
		feature_clear(np, FEATURE_IDE1_enable);
		break;
	    case 2:
		feature_set(np, FEATURE_IDE2_reset);
		break;
	}
	pmac_ide[i].timings[0] = 0;
	pmac_ide[i].timings[1] = 0;
}

static void idepmac_wake_disk(int i, unsigned long base)
{
	struct device_node* np = pmac_ide[i].node;
	int j;

	/* Revive IDE disk and controller */
	switch(pmac_ide[i].aapl_bus_id) {
	    case 0:
		feature_set(np, FEATURE_IDE0_reset);
		mdelay(10);
 		feature_set(np, FEATURE_IDE0_enable);
		mdelay(10);
		feature_clear(np, FEATURE_IDE0_reset);
		break;
	    case 1:
		feature_set(np, FEATURE_IDE1_reset);
		mdelay(10);
 		feature_set(np, FEATURE_IDE1_enable);
		mdelay(10);
		feature_clear(np, FEATURE_IDE1_reset);
		break;
	    case 2:
	    	/* This one exists only for KL, I don't know
		   about any enable bit */
		feature_set(np, FEATURE_IDE2_reset);
		mdelay(10);
		feature_clear(np, FEATURE_IDE2_reset);
		break;
	}
	mdelay(IDE_WAKEUP_DELAY_MS);

	/* Reset timings */
	pmac_ide_selectproc(&ide_hwifs[i].drives[0]);
	mdelay(10);

	/* Wait up to 10 seconds (enough for recent drives) */
	for (j = 0; j < 100; j++) {
		int status;
		mdelay(100);
		status = inb(base + 0x70);
		if (!(status & BUSY_STAT))
			break;
	}
}

/* Here we handle media bay devices */
static void
idepmac_wake_bay(int i, unsigned long base)
{
	int timeout;

	/* Reset timings */
	pmac_ide_selectproc(&ide_hwifs[i].drives[0]);
	mdelay(10);

	timeout = 10000;
	while ((inb(base + 0x70) & BUSY_STAT) && timeout) {
		mdelay(1);
		--timeout;
	}
}

/* Note: We support only master drives for now. This will have to be
 * improved if we want to handle sleep on the iMacDV where the CD-ROM
 * is a slave
 */
static int idepmac_notify_sleep(struct pmu_sleep_notifier *self, int when)
{
	int i, ret;
	unsigned long base;

	switch (when) {
	case PBOOK_SLEEP_REQUEST:
		break;
	case PBOOK_SLEEP_REJECT:
		break;
	case PBOOK_SLEEP_NOW:
		for (i = 0; i < pmac_ide_count; ++i) {
			if ((base = pmac_ide[i].regbase) == 0)
				continue;
			/* Disable irq during sleep */
			disable_irq(pmac_ide[i].irq);
			ret = check_media_bay_by_base(base, MB_CD);
			if ((ret == -ENODEV) && ide_hwifs[i].drives[0].present)
				/* not media bay - put the disk to sleep */
				idepmac_sleep_disk(i, base);
		}
		break;
	case PBOOK_WAKE:
		for (i = 0; i < pmac_ide_count; ++i) {
			ide_hwif_t *hwif;
			if ((base = pmac_ide[i].regbase) == 0)
				continue;
			hwif = &ide_hwifs[i];
		        /* We don't handle media bay devices this way */
			ret = check_media_bay_by_base(base, MB_CD);
			if ((ret == -ENODEV) && ide_hwifs[i].drives[0].present)
				idepmac_wake_disk(i, base);
			else if (ret == 0)
				idepmac_wake_bay(i, base);
			enable_irq(pmac_ide[i].irq);

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
			if (hwif->drives[0].present && hwif->drives[0].using_dma)
				pmac_ide_dma_onoff(&hwif->drives[0], 1);
#endif				
		}
		break;
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */

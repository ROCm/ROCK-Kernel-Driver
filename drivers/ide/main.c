/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 1994-1998,2002  Linus Torvalds and authors:
 *
 *	Mark Lord	<mlord@pobox.com>
 *      Gadi Oxman	<gadio@netvision.net.il>
 *      Andre Hedrick	<andre@linux-ide.org>
 *	Jens Axboe	<axboe@suse.de>
 *      Marcin Dalecki	<martin@dalecki.de>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 */

/*
 * Handle overall infrastructure of the driver
 */

#define	VERSION	"7.0.0"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#ifndef MODULE
# include <linux/init.h>
#endif
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/device.h>
#include <linux/kmod.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#include "ata-timing.h"
#include "pcihost.h"
#include "ioctl.h"


MODULE_DESCRIPTION("ATA/ATAPI driver infrastructure");
MODULE_PARM(options,"s");
MODULE_LICENSE("GPL");

/*
 * Those will be moved into separate header files eventually.
 */
#ifdef CONFIG_ETRAX_IDE
extern void init_e100_ide(void);
#endif
#ifdef CONFIG_BLK_DEV_CMD640
extern void ide_probe_for_cmd640x(void);
#endif
#ifdef CONFIG_BLK_DEV_PDC4030
extern int ide_probe_for_pdc4030(void);
#endif
#ifdef CONFIG_BLK_DEV_IDE_PMAC
extern void pmac_ide_probe(void);
#endif
#ifdef CONFIG_BLK_DEV_IDE_ICSIDE
extern void icside_init(void);
#endif
#ifdef CONFIG_BLK_DEV_IDE_RAPIDE
extern void rapide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_GAYLE
extern void gayle_init(void);
#endif
#ifdef CONFIG_BLK_DEV_FALCON_IDE
extern void falconide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_MAC_IDE
extern void macide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_Q40IDE
extern void q40ide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_BUDDHA
extern void buddha_init(void);
#endif
#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP)
extern void pnpide_init(int);
#endif

/* default maximum number of failures */
#define IDE_DEFAULT_MAX_FAILURES	1

int system_bus_speed;		/* holds what we think is VESA/PCI bus speed */

static int initializing;	/* set while initializing built-in drivers */
static int idebus_parameter;	/* the "idebus=" parameter */

/*
 * Protects access to global structures etc.
 */
spinlock_t ide_lock __cacheline_aligned = SPIN_LOCK_UNLOCKED;

#ifdef CONFIG_PCI
static int ide_scan_direction;	/* THIS was formerly 2.2.x pci=reverse */
#endif

#ifdef ATA_ARCH_LOCK
/*
 * This is used by the Atari code to obtain access to the IDE interrupt,
 * which is shared between several drivers.
 */
int ide_irq_lock;
#endif

int noautodma = 0;

/* Single linked list of sub device type drivers */
static struct ata_operations *ata_drivers; /* = NULL */
static spinlock_t ata_drivers_lock = SPIN_LOCK_UNLOCKED;

/*
 * This is declared extern in ide.h, for access by other IDE modules:
 */
struct ata_channel ide_hwifs[MAX_HWIFS];	/* master data repository */

/*
 * FIXME: This function should be unrolled in the palces where it get's used,
 * since in reality it's simple architecture specific initialization.
 *
 * Setup hw_regs_t structure described by parameters.  You may set up the hw
 * structure yourself OR use this routine to do it for you.
 */
void ide_setup_ports(hw_regs_t *hw, ide_ioreg_t base, int *offsets,
		ide_ioreg_t ctrl, ide_ioreg_t intr,
		int (*ack_intr)(struct ata_channel *),
		int irq)
{
	int i;

	for (i = 0; i < IDE_NR_PORTS; i++) {
		if (offsets[i] != -1)
			hw->io_ports[i] = base + offsets[i];
		else
			hw->io_ports[i] = 0;
	}
	if (offsets[IDE_CONTROL_OFFSET] == -1)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl;

	/* FIMXE: check if we can remove this ifdef */
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (offsets[IDE_IRQ_OFFSET] == -1)
		hw->io_ports[IDE_IRQ_OFFSET] = intr;
#endif

	hw->irq = irq;
	hw->dma = NO_DMA;
	hw->ack_intr = ack_intr;
}

static void init_hwif_data(struct ata_channel *ch, unsigned int index)
{
	static const unsigned int majors[] = {
		IDE0_MAJOR, IDE1_MAJOR, IDE2_MAJOR, IDE3_MAJOR, IDE4_MAJOR,
		IDE5_MAJOR, IDE6_MAJOR, IDE7_MAJOR, IDE8_MAJOR, IDE9_MAJOR
	};

	unsigned int unit;
	hw_regs_t hw;

	/* bulk initialize channel & drive info with zeros */
	memset(ch, 0, sizeof(struct ata_channel));
	memset(&hw, 0, sizeof(hw_regs_t));

	/* fill in any non-zero initial values */
	ch->index = index;
	ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, &ch->irq);

	memcpy(&ch->hw, &hw, sizeof(hw));
	memcpy(ch->io_ports, hw.io_ports, sizeof(hw.io_ports));

	ch->noprobe	= !ch->io_ports[IDE_DATA_OFFSET];
#ifdef CONFIG_BLK_DEV_HD
	if (ch->io_ports[IDE_DATA_OFFSET] == HD_DATA)
		ch->noprobe = 1; /* may be overridden by ide_setup() */
#endif

	ch->major = majors[index];
	sprintf(ch->name, "ide%d", index);
	ch->bus_state = BUSSTATE_ON;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &ch->drives[unit];

		drive->type		= ATA_DISK;
		drive->select.all	= (unit << 4) | 0xa0;
		drive->channel		= ch;
		drive->ready_stat	= READY_STAT;
		drive->bad_wstat	= BAD_W_STAT;
		sprintf(drive->name, "hd%c", 'a' + (index * MAX_DRIVES) + unit);
		drive->max_failures	= IDE_DEFAULT_MAX_FAILURES;

		init_waitqueue_head(&drive->wqueue);
	}
}

extern struct block_device_operations ide_fops[];

/*
 * Returns the (struct ata_device *) for a given device number.  Return
 * NULL if the given device number does not match any present drives.
 */
struct ata_device *get_info_ptr(kdev_t i_rdev)
{
	unsigned int major = major(i_rdev);
	int h;

	for (h = 0; h < MAX_HWIFS; ++h) {
		struct ata_channel *ch = &ide_hwifs[h];
		if (ch->present && major == ch->major) {
			int unit = DEVICE_NR(i_rdev);
			if (unit < MAX_DRIVES) {
				struct ata_device *drive = &ch->drives[unit];
				if (drive->present)
					return drive;
			}
			break;
		}
	}
	return NULL;
}

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
int ata_revalidate(kdev_t i_rdev)
{
	kdev_t device = mk_kdev(major(i_rdev), minor(i_rdev) & ~PARTN_MASK);
	struct ata_device *drive;
	int res;

	if ((drive = get_info_ptr(device)) == NULL)
		return -ENODEV;

	MOD_INC_USE_COUNT;

	res = dev_lock_part(device);
	if (res < 0) {
		MOD_DEC_USE_COUNT;
		return res;
	}

	res = wipe_partitions(device);
	if (!res) {
		if (ata_ops(drive) && ata_ops(drive)->revalidate) {
			ata_get(ata_ops(drive));

			/* This is expected to be a no-op for tapes and SCSI
			 * based access.
			 */
			ata_ops(drive)->revalidate(drive);
			ata_put(ata_ops(drive));
		} else
			grok_partitions(device, ata_capacity(drive));
	}

	dev_unlock_part(device);
	MOD_DEC_USE_COUNT;
	return res;
}

/*
 * FIXME: this is most propably just totally unnecessary.
 *
 * Look again for all drives in the system on all interfaces.
 */
static void revalidate_drives(void)
{
	int i;

	for (i = 0; i < MAX_HWIFS; ++i) {
		int unit;
		struct ata_channel *ch = &ide_hwifs[i];

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			struct ata_device *drive = &ch->drives[unit];

			if (drive->revalidate) {
				drive->revalidate = 0;
				if (!initializing)
					ata_revalidate(mk_kdev(ch->major, unit<<PARTN_BITS));
			}
		}
	}
}

void ide_driver_module(void)
{
	int i;

	/* Don't reinit the probe if there is already one channel detected. */
	for (i = 0; i < MAX_HWIFS; ++i) {
		if (ide_hwifs[i].present)
			goto revalidate;
	}

	ideprobe_init();

revalidate:
	revalidate_drives();
}

/*
 * Release the data associated with a channel.
 */
void ide_unregister(struct ata_channel *ch)
{
	struct gendisk *gd;
	struct ata_device *d;
	spinlock_t *lock;
	int unit;
	int i;
	unsigned long flags;
	unsigned int p, minor;
	struct ata_channel old;
	int n_irq;
	int n_ch;

	spin_lock_irqsave(&ide_lock, flags);

	if (!ch->present)
		goto abort;

	put_device(&ch->dev);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device * drive = &ch->drives[unit];

		if (!drive->present)
			continue;

		if (drive->busy || drive->usage)
			goto abort;

		if (ata_ops(drive)) {
			if (ata_ops(drive)->cleanup) {
				if (ata_ops(drive)->cleanup(drive))
					goto abort;
			} else
				ide_unregister_subdriver(drive);
		}
	}
	ch->present = 0;

	/*
	 * All clear?  Then blow away the buffer cache
	 */
	spin_unlock_irqrestore(&ide_lock, flags);

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device * drive = &ch->drives[unit];

		if (!drive->present)
			continue;

		minor = drive->select.b.unit << PARTN_BITS;
		for (p = 0; p < (1<<PARTN_BITS); ++p) {
			if (drive->part[p].nr_sects > 0) {
				kdev_t devp = mk_kdev(ch->major, minor+p);
				invalidate_device(devp, 0);
			}
		}
	}

	spin_lock_irqsave(&ide_lock, flags);

	/*
	 * Note that we only release the standard ports, and do not even try to
	 * handle any extra ports allocated for weird IDE interface chipsets.
	 */

	if (ch->straight8) {
		release_region(ch->io_ports[IDE_DATA_OFFSET], 8);
	} else {
		for (i = 0; i < 8; i++)
			if (ch->io_ports[i])
				release_region(ch->io_ports[i], 1);
	}
	if (ch->io_ports[IDE_CONTROL_OFFSET])
		release_region(ch->io_ports[IDE_CONTROL_OFFSET], 1);
/* FIXME: check if we can remove this ifdef */
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (ch->io_ports[IDE_IRQ_OFFSET])
		release_region(ch->io_ports[IDE_IRQ_OFFSET], 1);
#endif

	/*
	 * Remove us from the lock group.
	 */

	lock = ch->lock;
	d = ch->drive;
	for (i = 0; i < MAX_DRIVES; ++i) {
		struct ata_device *drive = &ch->drives[i];

		if (drive->de) {
			devfs_unregister(drive->de);
			drive->de = NULL;
		}
		if (!drive->present)
			continue;

		/* FIXME: possibly unneccessary */
		if (ch->drive == drive)
			ch->drive = NULL;

		if (drive->id != NULL) {
			kfree(drive->id);
			drive->id = NULL;
		}
		drive->present = 0;
		blk_cleanup_queue(&drive->queue);
	}
	if (d->present)
		ch->drive = d;


	/*
	 * Free the irq if we were the only channel using it.
	 *
	 * Free the lock group if we were the only member.
	 */
	n_irq = n_ch = 0;
	for (i = 0; i < MAX_HWIFS; ++i) {
		struct ata_channel *tmp = &ide_hwifs[i];

		if (!tmp->present)
			continue;

		if (tmp->irq == ch->irq)
			++n_irq;
		if (tmp->lock == ch->lock)
			++n_ch;
	}
	if (n_irq == 1)
		free_irq(ch->irq, ch);
	if (n_ch == 1) {
		kfree(ch->lock);
		kfree(ch->active);
		ch->lock = NULL;
		ch->active = NULL;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	ide_release_dma(ch);
#endif

	/*
	 * Remove us from the kernel's knowledge.
	 */
	unregister_blkdev(ch->major, ch->name);
	blk_dev[ch->major].data = NULL;
	blk_dev[ch->major].queue = NULL;
	blk_clear(ch->major);
	gd = ch->gd;
	if (gd) {
		del_gendisk(gd);
		kfree(gd->sizes);
		kfree(gd->part);
		if (gd->de_arr)
			kfree (gd->de_arr);
		if (gd->flags)
			kfree (gd->flags);
		kfree(gd);
		ch->gd = NULL;
	}

	/*
	 * Reinitialize the channel handler, but preserve any special methods for
	 * it.
	 */

	old = *ch;
	init_hwif_data(ch, ch->index);
	ch->lock = old.lock;
	ch->tuneproc = old.tuneproc;
	ch->speedproc = old.speedproc;
	ch->selectproc = old.selectproc;
	ch->resetproc = old.resetproc;
	ch->intrproc = old.intrproc;
	ch->maskproc = old.maskproc;
	ch->quirkproc = old.quirkproc;
	ch->ata_read = old.ata_read;
	ch->ata_write = old.ata_write;
	ch->atapi_read = old.atapi_read;
	ch->atapi_write = old.atapi_write;
	ch->udma_setup = old.udma_setup;
	ch->udma_enable = old.udma_enable;
	ch->udma_start = old.udma_start;
	ch->udma_stop = old.udma_stop;
	ch->udma_init = old.udma_init;
	ch->udma_irq_status = old.udma_irq_status;
	ch->udma_timeout = old.udma_timeout;
	ch->udma_irq_lost = old.udma_irq_lost;
	ch->busproc = old.busproc;
	ch->bus_state = old.bus_state;
	ch->dma_base = old.dma_base;
	ch->dma_extra = old.dma_extra;
	ch->config_data = old.config_data;
	ch->select_data = old.select_data;
	ch->proc = old.proc;
	/* FIXME: most propably this is always right:! */
#ifndef CONFIG_BLK_DEV_IDECS
	ch->irq = old.irq;
#endif
	ch->major = old.major;
	ch->chipset = old.chipset;
	ch->autodma = old.autodma;
	ch->udma_four = old.udma_four;
#ifdef CONFIG_PCI
	ch->pci_dev = old.pci_dev;
#endif
	ch->straight8 = old.straight8;

abort:
	spin_unlock_irqrestore(&ide_lock, flags);
}

static int subdriver_match(struct ata_channel *channel, struct ata_operations *ops)
{
	int count, unit;

	if (!channel->present)
		return 0;

	count = 0;
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &channel->drives[unit];
		if (drive->present && !drive->driver) {
			(*ops->attach)(drive);
			if (drive->driver != NULL)
				++count;
		}
	}
	return count;
}

/*
 * Register an IDE interface, specifing exactly the registers etc
 * Set initializing=1 iff calling before probes have taken place.
 */
int ide_register_hw(hw_regs_t *hw)
{
	int h;
	int retry = 1;
	struct ata_channel *ch;

	do {
		for (h = 0; h < MAX_HWIFS; ++h) {
			ch = &ide_hwifs[h];
			if (ch->hw.io_ports[IDE_DATA_OFFSET] == hw->io_ports[IDE_DATA_OFFSET])
				goto found;
		}
		for (h = 0; h < MAX_HWIFS; ++h) {
			ch = &ide_hwifs[h];
			if ((!ch->present && (ch->unit == ATA_PRIMARY) && !initializing) ||
			    (!ch->hw.io_ports[IDE_DATA_OFFSET] && initializing))
				goto found;
		}
		for (h = 0; h < MAX_HWIFS; ++h)
			ide_unregister(&ide_hwifs[h]);
	} while (retry--);

	return -1;

found:
	ide_unregister(ch);
	if (ch->present)
		return -1;
	memcpy(&ch->hw, hw, sizeof(*hw));
	memcpy(ch->io_ports, ch->hw.io_ports, sizeof(ch->hw.io_ports));
	ch->irq = hw->irq;
	ch->noprobe = 0;
	ch->chipset = hw->chipset;

	if (!initializing) {
		ideprobe_init();
		revalidate_drives();
		/* FIXME: Do we really have to call it second time here?! */
		ide_driver_module();
	}

	/* Look up whatever there is a subdriver, which will serve this
	 * device and execute the attach method it is providing.
	 */
	{
		struct ata_operations *tmp;
		unsigned long flags;

		spin_lock_irqsave(&ata_drivers_lock, flags);
		for(tmp = ata_drivers; tmp; tmp = tmp->next) {
			if (subdriver_match(ch, tmp) > 0)
				break;
		}
		spin_unlock_irqrestore(&ata_drivers_lock, flags);
	}

	return (initializing || ch->present) ? h : -1;
}

/****************************************************************************
 * FIXME: rewrite the following crap:
 */

/*
 * Parsing for ide_setup():
 *
 * 1. the first char of s must be '='.
 * 2. if the remainder matches one of the supplied keywords,
 *     the index (1 based) of the keyword is negated and returned.
 * 3. if the remainder is a series of no more than max_vals numbers
 *     separated by commas, the numbers are saved in vals[] and a
 *     count of how many were saved is returned.  Base10 is assumed,
 *     and base16 is allowed when prefixed with "0x".  The number of 
 *     values read will be placed in vals[0], and the values read will
 *     placed in vals[1] to vals[max_vals].
 * 4. otherwise, zero is returned.
 */
static int __init match_parm (char *s, const char *keywords[], int vals[], int max_vals)
{
	int i;

	if (*s++ == '=') {
		/*
		 * Try matching against the supplied keywords,
		 * and return -(index+1) if we match one
		 */
		if (keywords != NULL) {
			for (i = 0; *keywords != NULL; ++i) {
				if (!strcmp(s, *keywords++))
					return -(i+1);
			}
		}
		/*
		 * Look for a series of no more than "max_vals"
		 * numeric values separated by commas, in base10,
		 * or base16 when prefixed with "0x".
		 * Return a count of how many were found.
		 */
        	get_options(s, max_vals+1, vals);
	        return vals[0];
	}
	return 0;
}

/*
 * This sets reasonable default values into all fields of all instances of the
 * channles and drives, but only on the first call.  Subsequent calls have no
 * effect (they don't wipe out anything).
 *
 * This routine is normally called at driver initialization time, but may also
 * be called MUCH earlier during kernel "command-line" parameter processing.
 * As such, we cannot depend on any other parts of the kernel (such as memory
 * allocation) to be functioning yet.
 *
 * This is too bad, as otherwise we could dynamically allocate the ata_device
 * structs as needed, rather than always consuming memory for the max possible
 * number (MAX_HWIFS * MAX_DRIVES) of them.
 */
#define MAGIC_COOKIE 0x12345678
static void __init init_global_data(void)
{
	unsigned int h;
	static unsigned long magic_cookie = MAGIC_COOKIE;

	if (magic_cookie != MAGIC_COOKIE)
		return;		/* already initialized */
	magic_cookie = 0;

	/* Initialize all interface structures */
	for (h = 0; h < MAX_HWIFS; ++h)
		init_hwif_data(&ide_hwifs[h], h);

	/* Add default hw interfaces */
	ide_init_default_hwifs();
}

/*
 * This gets called VERY EARLY during initialization, to handle kernel "command
 * line" strings beginning with "hdx=" or "ide".It gets called even before the
 * actual module gets initialized.
 *
 * Please look at Documentation/ide.txt to see the complete list of supported
 * options.
 */
int __init ide_setup(char *s)
{
	int i, vals[4];
	struct ata_channel *ch;
	struct ata_device *drive;
	unsigned int hw, unit;
	const char max_drive = 'a' + ((MAX_HWIFS * MAX_DRIVES) - 1);
	const char max_ch  = '0' + (MAX_HWIFS - 1);

	if (!strncmp(s, "hd=", 3))	/* hd= is for hd.c driver and not us */
		return 0;

	if (strncmp(s,"ide",3) &&
	    strncmp(s,"hd",2))		/* hdx= & hdxlun= */
		return 0;

	printk(KERN_INFO  "ide_setup: %s", s);
	init_global_data();

#ifdef CONFIG_BLK_DEV_IDEDOUBLER
	if (!strcmp(s, "ide=doubler")) {
		extern int ide_doubler;

		printk(KERN_INFO" : Enabled support for IDE doublers\n");
		ide_doubler = 1;

		return 1;
	}
#endif

	if (!strcmp(s, "ide=nodma")) {
		printk(KERN_INFO "ATA: Prevented DMA\n");
		noautodma = 1;

		return 1;
	}

#ifdef CONFIG_PCI
	if (!strcmp(s, "ide=reverse")) {
		ide_scan_direction = 1;
		printk(" : Enabled support for IDE inverse scan order.\n");

		return 1;
	}
#endif

	/*
	 * Look for drive options:  "hdx="
	 */
	if (!strncmp(s, "hd", 2) && s[2] >= 'a' && s[2] <= max_drive) {
		const char *hd_words[] = {"none", "noprobe", "nowerr", "cdrom",
				"serialize", "autotune", "noautotune",
				"slow", "flash", "remap", "noremap", "scsi", NULL};
		unit = s[2] - 'a';
		hw   = unit / MAX_DRIVES;
		unit = unit % MAX_DRIVES;
		ch = &ide_hwifs[hw];
		drive = &ch->drives[unit];
		if (!strncmp(s+3, "=ide-", 5)) {
			strncpy(drive->driver_req, s + 4, 9);
			goto done;
		}
		/*
		 * Look for last lun option:  "hdxlun="
		 */
		if (!strncmp(s+3, "lun=", 4)) {
	                if (*get_options(s+7, 2, vals) || vals[0]!=1)
				goto bad_option;
			if (vals[1] >= 0 && vals[1] <= 7) {
				drive->last_lun = vals[1];
				drive->forced_lun = 1;
			} else
				printk(" -- BAD LAST LUN! Expected value from 0 to 7");
			goto done;
		}
		switch (match_parm(s+3, hd_words, vals, 3)) {
			case -1: /* "none" */
				drive->nobios = 1;  /* drop into "noprobe" */
			case -2: /* "noprobe" */
				drive->noprobe = 1;
				goto done;
			case -3: /* "nowerr" */
				drive->bad_wstat = BAD_R_STAT;
				ch->noprobe = 0;
				goto done;
			case -4: /* "cdrom" */
				drive->present = 1;
				drive->type = ATA_ROM;
				ch->noprobe = 0;
				goto done;
			case -5: /* "serialize" */
				printk(" -- USE \"ide%d=serialize\" INSTEAD", hw);
				goto do_serialize;
			case -6: /* "autotune" */
				drive->autotune = 1;
				goto done;
			case -7: /* "noautotune" */
				drive->autotune = 2;
				goto done;
			case -8: /* "slow" */
				ch->slow = 1;
				goto done;
			case -9: /* "flash" */
				drive->ata_flash = 1;
				goto done;
			case -10: /* "remap" */
				drive->remap_0_to_1 = 1;
				goto done;
			case -11: /* "noremap" */
				drive->remap_0_to_1 = 2;
				goto done;
			case -12: /* "scsi" */
#if defined(CONFIG_BLK_DEV_IDESCSI) && defined(CONFIG_SCSI)
				drive->scsi = 1;
				goto done;
#else
				drive->scsi = 0;
				goto bad_option;
#endif
			case 3: /* cyl,head,sect */
				drive->type	= ATA_DISK;
				drive->cyl	= drive->bios_cyl  = vals[1];
				drive->head	= drive->bios_head = vals[2];
				drive->sect	= drive->bios_sect = vals[3];
				drive->present	= 1;
				drive->forced_geom = 1;
				ch->noprobe = 0;
				goto done;
			default:
				goto bad_option;
		}
	}

	/*
	 * Look for bus speed option:  "idebus="
	 */
	if (!strncmp(s, "idebus=", 7)) {
		if (*get_options(s+7, 2, vals) || vals[0] != 1)
			goto bad_option;
		idebus_parameter = vals[1];
		goto done;
	}

	/*
	 * Look for interface options:  "idex="
	 */
	if (!strncmp(s, "ide", 3) && s[3] >= '0' && s[3] <= max_ch) {
		/*
		 * Be VERY CAREFUL changing this: note hardcoded indexes below
		 */
		const char *ide_options[] = {
			"noprobe", "serialize", "autotune", "noautotune", "reset", "dma", "ata66", NULL };
		const char *ide_words[] = {
			"qd65xx", "ht6560b", "cmd640_vlb", "dtc2278", "umc8672", "ali14xx", "dc4030", NULL };
		hw = s[3] - '0';
		ch = &ide_hwifs[hw];


		switch (match_parm(s+4, ide_options, vals, 1)) {
			case -7: /* ata66 */
#ifdef CONFIG_PCI
				ch->udma_four = 1;
				goto done;
#else
				ch->udma_four = 0;
				goto bad_channel;
#endif
			case -6: /* dma */
				ch->autodma = 1;
				goto done;
			case -5: /* reset */
				ch->reset = 1;
				goto done;
			case -4: /* noautotune */
				ch->drives[0].autotune = 2;
				ch->drives[1].autotune = 2;
				goto done;
			case -3: /* autotune */
				ch->drives[0].autotune = 1;
				ch->drives[1].autotune = 1;
				goto done;
			case -2: /* "serialize" */
			do_serialize:
				{
					struct ata_channel *mate;

					mate = &ide_hwifs[hw ^ 1];
					ch->serialized = 1;
					mate->serialized = 1;
				}
				goto done;

			case -1: /* "noprobe" */
				ch->noprobe = 1;
				goto done;
		}

		i = match_parm(&s[4], ide_words, vals, 3);

		/*
		 * Cryptic check to ensure chipset not already set for a channel:
		 */
		if (i) {			/* is parameter a chipset name? */
			if (ide_hwifs[hw].chipset != ide_unknown)
				goto bad_option;	/* chipset already specified */
			if (i != -7 && hw != 0)
				goto bad_channel;		/* chipset drivers are for "ide0=" only */
			if (i != -7 && ide_hwifs[1].chipset != ide_unknown)
				goto bad_option;	/* chipset for 2nd port already specified */
			printk("\n");
		}

		switch (i) {
#ifdef CONFIG_BLK_DEV_PDC4030
			case -7:  /* "dc4030" */
			{
				extern void init_pdc4030(void);
				init_pdc4030();
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_ALI14XX
			case -6:  /* "ali14xx" */
			{
				extern void init_ali14xx (void);
				init_ali14xx();
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_UMC8672
			case -5:  /* "umc8672" */
			{
				extern void init_umc8672 (void);
				init_umc8672();
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_DTC2278
			case -4:  /* "dtc2278" */
			{
				extern void init_dtc2278 (void);
				init_dtc2278();
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_CMD640
			case -3:  /* "cmd640_vlb" */
			{
				extern int cmd640_vlb; /* flag for cmd640.c */
				cmd640_vlb = 1;
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_HT6560B
			case -2:  /* "ht6560b" */
			{
				extern void init_ht6560b (void);
				init_ht6560b();
				goto done;
			}
#endif
#if CONFIG_BLK_DEV_QD65XX
			case -1:  /* "qd65xx" */
			{
				extern void init_qd65xx (void);
				init_qd65xx();
				goto done;
			}
#endif
			case 1:	/* base */
				vals[2] = vals[1] + 0x206; /* default ctl */
			case 2: /* base,ctl */
				vals[3] = 0;	/* default irq = probe for it */
			case 3: /* base,ctl,irq */
				ch->hw.irq = vals[3];
				ide_init_hwif_ports(&ch->hw, (ide_ioreg_t) vals[1], (ide_ioreg_t) vals[2], &ch->irq);
				memcpy(ch->io_ports, ch->hw.io_ports, sizeof(ch->io_ports));
				ch->irq = vals[3];
				ch->noprobe  = 0;
				ch->chipset  = ide_generic;
				goto done;

			case 0:
				goto bad_option;
			default:
				printk(" -- SUPPORT NOT CONFIGURED IN THIS KERNEL\n");
				return 1;
		}
	}

bad_option:
	printk(" -- BAD OPTION\n");
	return 1;

bad_channel:
	printk("-- NOT SUPPORTED ON ide%d", hw);

done:
	printk("\n");

	return 1;
}

/****************************************************************************/

/*
 * This is in fact registering a device not a driver.
 */
int ide_register_subdriver(struct ata_device *drive, struct ata_operations *driver)
{
	unsigned long flags;

	/* FIXME: The locking here doesn't make the slightest sense! */
	spin_lock_irqsave(&ide_lock, flags);

	if (!drive->present || drive->driver != NULL || drive->busy || drive->usage) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return 1;
	}

	/* FIXME: This will be pushed to the drivers! Thus allowing us to
	 * save one parameter here and to separate this out.
	 */
	drive->driver = driver;

	spin_unlock_irqrestore(&ide_lock, flags);
	/* Default autotune or requested autotune */
	if (drive->autotune != 2) {
		struct ata_channel *ch = drive->channel;
		if (ch->udma_setup) {

			/*
			 * Force DMAing for the beginning of the check.  Some
			 * chipsets appear to do interesting things, if not
			 * checked and cleared.
			 *
			 *   PARANOIA!!!
			 */

			udma_enable(drive, 0, 0);
			ch->udma_setup(drive, ch->modes_map);
#ifdef CONFIG_BLK_DEV_IDE_TCQ_DEFAULT
			udma_tcq_enable(drive, 1);
#endif
		}

		/* Only CD-ROMs and tape drives support DSC overlap.  But only
		 * if they are alone on a channel. */
		if (drive->type == ATA_ROM || drive->type == ATA_TAPE) {
			int single = 0;
			int unit;

			for (unit = 0; unit < MAX_DRIVES; ++unit)
				if (drive->channel->drives[unit].present)
					++single;

			drive->dsc_overlap = (single == 1);
		} else
			drive->dsc_overlap = 0;

	}
	drive->revalidate = 1;
	drive->suspend_reset = 0;

	return 0;
}

/*
 * This is in fact the default cleanup routine.
 *
 * FIXME: Check whatever we maybe don't call it twice!.
 */
int ide_unregister_subdriver(struct ata_device *drive)
{
	unsigned long flags;

	__save_flags(flags); // FIXME: is this safe?
	__cli();

#if 0
	if (__MOD_IN_USE(ata_ops(drive)->owner)) {
		__restore_flags(flags); // FIXME: is this safe?
		return 1;
	}
#endif

	if (drive->usage || drive->busy || !ata_ops(drive)) {
		__restore_flags(flags);	// FIXME: is this safe?
		return 1;
	}

#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP) && defined(MODULE)
	pnpide_init(0);
#endif
	drive->driver = NULL;

	__restore_flags(flags); // FIXME: is this safe?

	return 0;
}


/*
 * Register an ATA subdriver for a particular device type.
 */
int register_ata_driver(struct ata_operations *driver)
{
	unsigned long flags;
	int index;
	int count = 0;

	spin_lock_irqsave(&ata_drivers_lock, flags);
	driver->next = ata_drivers;
	ata_drivers = driver;
	spin_unlock_irqrestore(&ata_drivers_lock, flags);

	for (index = 0; index < MAX_HWIFS; ++index)
		count += subdriver_match(&ide_hwifs[index], driver);

	return count;
}

EXPORT_SYMBOL(register_ata_driver);

/*
 * Unregister an ATA subdriver for a particular device type.
 */
void unregister_ata_driver(struct ata_operations *driver)
{
	struct ata_operations **tmp;
	unsigned long flags;
	int index;
	int unit;

	spin_lock_irqsave(&ata_drivers_lock, flags);
	for (tmp = &ata_drivers; *tmp != NULL; tmp = &(*tmp)->next) {
		if (*tmp == driver) {
			*tmp = driver->next;
			break;
		}
	}
	spin_unlock_irqrestore(&ata_drivers_lock, flags);

	for (index = 0; index < MAX_HWIFS; ++index) {
		struct ata_channel *ch = &ide_hwifs[index];
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			struct ata_device *drive = &ch->drives[unit];

			if (drive->driver == driver)
				(*ata_ops(drive)->cleanup)(drive);
		}
	}
}

EXPORT_SYMBOL(unregister_ata_driver);

EXPORT_SYMBOL(ide_hwifs);
EXPORT_SYMBOL(ide_lock);

devfs_handle_t ide_devfs_handle;

EXPORT_SYMBOL(ide_register_subdriver);
EXPORT_SYMBOL(ide_unregister_subdriver);
EXPORT_SYMBOL(ata_revalidate);
EXPORT_SYMBOL(ide_register_hw);
EXPORT_SYMBOL(ide_unregister);
EXPORT_SYMBOL(get_info_ptr);

/*
 * Handle power handling related events ths system informs us about.
 */
static int ata_sys_notify(struct notifier_block *this, unsigned long event, void *x)
{
	int i;

	switch (event) {
		case SYS_HALT:
		case SYS_POWER_OFF:
		case SYS_RESTART:
			break;
		default:
			return NOTIFY_DONE;
	}

	printk(KERN_INFO "flushing ATA/ATAPI devices: ");

	for (i = 0; i < MAX_HWIFS; i++) {
		int unit;
		struct ata_channel *ch = &ide_hwifs[i];

		if (!ch->present)
			continue;

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			struct ata_device *drive = &ch->drives[unit];

			if (!drive->present)
				continue;

			/* set the drive to standby */
			printk("%s ", drive->name);
			if (ata_ops(drive)) {
				if (event != SYS_RESTART) {
					if (ata_ops(drive)->standby && ata_ops(drive)->standby(drive))
						continue;
				}

				if (ata_ops(drive)->cleanup)
					ata_ops(drive)->cleanup(drive);
			}
		}
	}
	printk("\n");
	return NOTIFY_DONE;
}

static struct notifier_block ata_notifier = {
	ata_sys_notify,
	NULL,
	5
};

/*
 * This is the global initialization entry point.
 */
static int __init ata_module_init(void)
{
	printk(KERN_INFO "ATA/ATAPI device driver v" VERSION "\n");

	ide_devfs_handle = devfs_mk_dir(NULL, "ide", NULL);

	/*
	 * Because most of the ATA adapters represent the timings in unit of
	 * bus clocks, and there is no known reliable way to detect the bus
	 * clock frequency, we assume 50 MHz for non-PCI (VLB, EISA) and 33 MHz
	 * for PCI based systems. Since assuming only hurts performance and not
	 * stability, this is OK. The user can change this on the command line
	 * by using the "idebus=XX" parameter. While the system_bus_speed
	 * variable is in kHz units, we accept both MHz and kHz entry on the
	 * command line for backward compatibility.
	 */

	system_bus_speed = 50000;

	if (pci_present())
	    system_bus_speed = 33333;

	if (idebus_parameter >= 20 && idebus_parameter <= 80) {

		system_bus_speed = idebus_parameter * 1000;

		switch (system_bus_speed) {
			case 33000: system_bus_speed = 33333; break;
			case 37000: system_bus_speed = 37500; break;
			case 41000: system_bus_speed = 41666; break;
			case 66000: system_bus_speed = 66666; break;
		}
	}

	if (idebus_parameter >= 20000 && idebus_parameter <= 80000)
	    system_bus_speed = idebus_parameter;

	printk(KERN_INFO "ATA: %s bus speed %d.%dMHz\n",
		pci_present() ? "PCI" : "System", system_bus_speed / 1000, system_bus_speed / 100 % 10);

	init_global_data();

	initializing = 1;

#ifdef CONFIG_PCI
	/*
	 * Register the host chip drivers.
	 */
# ifdef CONFIG_BLK_DEV_PIIX
	init_piix();
# endif
# ifdef CONFIG_BLK_DEV_VIA82CXXX
	init_via82cxxx();
# endif
# ifdef CONFIG_BLK_DEV_PDC202XX
	init_pdc202xx();
# endif
# ifdef CONFIG_BLK_DEV_RZ1000
	init_rz1000();
# endif
# ifdef CONFIG_BLK_DEV_SIS5513
	init_sis5513();
# endif
# ifdef CONFIG_BLK_DEV_CMD64X
	init_cmd64x();
# endif
# ifdef CONFIG_BLK_DEV_OPTI621
	init_opti621();
# endif
# ifdef CONFIG_BLK_DEV_TRM290
	init_trm290();
# endif
# ifdef CONFIG_BLK_DEV_NS87415
	init_ns87415();
# endif
# ifdef CONFIG_BLK_DEV_AEC62XX
	init_aec62xx();
# endif
# ifdef CONFIG_BLK_DEV_SL82C105
	init_sl82c105();
# endif
# ifdef CONFIG_BLK_DEV_HPT34X
	init_hpt34x();
# endif
# ifdef CONFIG_BLK_DEV_HPT366
	init_hpt366();
# endif
# ifdef CONFIG_BLK_DEV_ALI15X3
	init_ali15x3();
# endif
# ifdef CONFIG_BLK_DEV_CY82C693
	init_cy82c693();
# endif
# ifdef CONFIG_BLK_DEV_CS5530
	init_cs5530();
# endif
# ifdef CONFIG_BLK_DEV_AMD74XX
	init_amd74xx();
# endif
# ifdef CONFIG_BLK_DEV_SVWKS
	init_svwks();
# endif
# ifdef CONFIG_BLK_DEV_IT8172
	init_it8172();
# endif

	init_ata_pci_misc();

	/*
	 * Detect and initialize "known" IDE host chip types.
	 */
	if (pci_present()) {
# ifdef CONFIG_PCI
		ide_scan_pcibus(ide_scan_direction);
# else
#  ifdef CONFIG_BLK_DEV_RZ1000
		ide_probe_for_rz100x();
#  endif
# endif
	}
#endif

#ifdef CONFIG_ETRAX_IDE
	init_e100_ide();
#endif
#ifdef CONFIG_BLK_DEV_CMD640
	ide_probe_for_cmd640x();
#endif
#ifdef CONFIG_BLK_DEV_PDC4030
	ide_probe_for_pdc4030();
#endif
#ifdef CONFIG_BLK_DEV_IDE_PMAC
	pmac_ide_probe();
#endif
#ifdef CONFIG_BLK_DEV_IDE_ICSIDE
	icside_init();
#endif
#ifdef CONFIG_BLK_DEV_IDE_RAPIDE
	rapide_init();
#endif
#ifdef CONFIG_BLK_DEV_GAYLE
	gayle_init();
#endif
#ifdef CONFIG_BLK_DEV_FALCON_IDE
	falconide_init();
#endif
#ifdef CONFIG_BLK_DEV_MAC_IDE
	macide_init();
#endif
#ifdef CONFIG_BLK_DEV_Q40IDE
	q40ide_init();
#endif
#ifdef CONFIG_BLK_DEV_BUDDHA
	buddha_init();
#endif
#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP)
	pnpide_init(1);
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
# if defined(__mc68000__) || defined(CONFIG_APUS)
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET]) {
		ide_get_lock(&ide_irq_lock, NULL, NULL);/* for atari only */
		disable_irq(ide_hwifs[0].irq);	/* disable_irq_nosync ?? */
//		disable_irq_nosync(ide_hwifs[0].irq);
	}
# endif

	ideprobe_init();

# if defined(__mc68000__) || defined(CONFIG_APUS)
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET]) {
		enable_irq(ide_hwifs[0].irq);
		ide_release_lock(&ide_irq_lock);/* for atari only */
	}
# endif
#endif

	/*
	 * Initialize all device type driver modules.
	 */
#ifdef CONFIG_BLK_DEV_IDEDISK
	idedisk_init();
#endif
#ifdef CONFIG_BLK_DEV_IDECD
	ide_cdrom_init();
#endif
#ifdef CONFIG_BLK_DEV_IDETAPE
	idetape_init();
#endif
#ifdef CONFIG_BLK_DEV_IDEFLOPPY
	idefloppy_init();
#endif

	initializing = 0;

	register_reboot_notifier(&ata_notifier);

	return 0;
}

static char *options = NULL;

static int __init init_ata(void)
{

	if (options != NULL && *options) {
		char *next = options;

		while ((options = next) != NULL) {
			if ((next = strchr(options,' ')) != NULL)
				*next++ = 0;
			if (!ide_setup(options))
				printk(KERN_ERR "Unknown option '%s'\n", options);
		}
	}
	return ata_module_init();
}

static void __exit cleanup_ata(void)
{
	int h;

	unregister_reboot_notifier(&ata_notifier);
	for (h = 0; h < MAX_HWIFS; ++h) {
		ide_unregister(&ide_hwifs[h]);
	}

	devfs_unregister(ide_devfs_handle);
}

module_init(init_ata);
module_exit(cleanup_ata);

#ifndef MODULE

/* command line option parser */
__setup("", ide_setup);

#endif

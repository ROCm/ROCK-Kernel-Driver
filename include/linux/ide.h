#ifndef _IDE_H
#define _IDE_H

/*
 *  Copyright (C) 1994-2002  Linus Torvalds & authors
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/hdsmart.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/bio.h>
#include <asm/byteorder.h>
#include <asm/hdreg.h>

/*
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to four IDE interfaces, on one or more IRQs (usually 14, 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * Primary i/f:    ide0: major=3;  (hda) minor=0; (hdb) minor=64
 * Secondary i/f:  ide1: major=22; (hdc) minor=0; (hdd) minor=64
 * Tertiary i/f:   ide2: major=33; (hde) minor=0; (hdf) minor=64
 * Quaternary i/f: ide3: major=34; (hdg) minor=0; (hdh) minor=64
 */

/******************************************************************************
 * IDE driver configuration options (play with these as desired):
 */
#define INITIAL_MULT_COUNT	0	/* off=0; on=2,4,8,16,32, etc.. */

#ifndef SUPPORT_SLOW_DATA_PORTS			/* 1 to support slow data ports */
# define SUPPORT_SLOW_DATA_PORTS	1	/* 0 to reduce kernel size */
#endif

#ifndef FANCY_STATUS_DUMPS		/* 1 for human-readable drive errors */
# define FANCY_STATUS_DUMPS	1	/* 0 to reduce kernel size */
#endif
#ifndef DISABLE_IRQ_NOSYNC
# define DISABLE_IRQ_NOSYNC	0
#endif

/*
 *  "No user-serviceable parts" beyond this point
 *****************************************************************************/

typedef unsigned char	byte;	/* used everywhere */

/*
 * Probably not wise to fiddle with these
 */
#define ERROR_MAX	8	/* Max read/write errors per sector */
#define ERROR_RESET	3	/* Reset controller every 4th retry */

/*
 * State flags.
 */
#define DMA_PIO_RETRY	1	/* retrying in PIO */

/*
 * Definitions for accessing IDE controller registers.
 */

enum {
	IDE_DATA_OFFSET		= 0,
	IDE_ERROR_OFFSET	= 1,
	IDE_FEATURE_OFFSET	= 1,
	IDE_NSECTOR_OFFSET	= 2,
	IDE_SECTOR_OFFSET	= 3,
	IDE_LCYL_OFFSET		= 4,
	IDE_HCYL_OFFSET		= 5,
	IDE_SELECT_OFFSET	= 6,
	IDE_STATUS_OFFSET	= 7,
	IDE_COMMAND_OFFSET	= 7,
	IDE_CONTROL_OFFSET	= 8,
	IDE_ALTSTATUS_OFFSET	= 8,
	IDE_IRQ_OFFSET		= 9,
	IDE_NR_PORTS		= 10
};


#define IDE_DATA_REG		(drive->channel->io_ports[IDE_DATA_OFFSET])
#define IDE_ERROR_REG		(drive->channel->io_ports[IDE_ERROR_OFFSET])
#define IDE_NSECTOR_REG		(drive->channel->io_ports[IDE_NSECTOR_OFFSET])
#define IDE_SECTOR_REG		(drive->channel->io_ports[IDE_SECTOR_OFFSET])
#define IDE_LCYL_REG		(drive->channel->io_ports[IDE_LCYL_OFFSET])
#define IDE_HCYL_REG		(drive->channel->io_ports[IDE_HCYL_OFFSET])
#define IDE_SELECT_REG		(drive->channel->io_ports[IDE_SELECT_OFFSET])
#define IDE_COMMAND_REG		(drive->channel->io_ports[IDE_STATUS_OFFSET])
#define IDE_IRQ_REG		(drive->channel->io_ports[IDE_IRQ_OFFSET])

#define IDE_FEATURE_REG		IDE_ERROR_REG
#define IDE_IREASON_REG		IDE_NSECTOR_REG
#define IDE_BCOUNTL_REG		IDE_LCYL_REG
#define IDE_BCOUNTH_REG		IDE_HCYL_REG

#define GET_ERR()		IN_BYTE(IDE_ERROR_REG)
#define GET_ALTSTAT()		IN_BYTE(drive->channel->io_ports[IDE_CONTROL_OFFSET])
#define GET_FEAT()		IN_BYTE(IDE_NSECTOR_REG)

#define BAD_R_STAT		(BUSY_STAT   | ERR_STAT)
#define BAD_W_STAT		(BAD_R_STAT  | WRERR_STAT)
#define BAD_STAT		(BAD_R_STAT  | DRQ_STAT)

#define DRIVE_READY		(READY_STAT  | SEEK_STAT)
#define DATA_READY		(DRQ_STAT)

/*
 * Our Physical Region Descriptor (PRD) table should be large enough
 * to handle the biggest I/O request we are likely to see.  Since requests
 * can have no more than 256 sectors, and since the typical blocksize is
 * two or more sectors, we could get by with a limit of 128 entries here for
 * the usual worst case.  Most requests seem to include some contiguous blocks,
 * further reducing the number of table entries required.
 *
 * As it turns out though, we must allocate a full 4KB page for this,
 * so the two PRD tables (ide0 & ide1) will each get half of that,
 * allowing each to have about 256 entries (8 bytes each) from this.
 */
#define PRD_BYTES	8
#define PRD_ENTRIES	(PAGE_SIZE / (2 * PRD_BYTES))

/*
 * Some more useful definitions
 */
#define IDE_MAJOR_NAME	"hd"	/* the same for all i/f; see also genhd.c */
#define MAJOR_NAME	IDE_MAJOR_NAME
#define PARTN_BITS	6	/* number of minor dev bits for partitions */
#define PARTN_MASK	((1<<PARTN_BITS)-1)	/* a useful bit mask */
#define MAX_DRIVES	2	/* per interface; 2 assumed by lots of code */
#define SECTOR_SIZE	512
#define SECTOR_WORDS	(SECTOR_SIZE / 4)	/* number of 32bit words per sector */

/*
 * Timeouts for various operations:
 */
#define WAIT_DRQ	(5*HZ/100)	/* 50msec - spec allows up to 20ms */
#define WAIT_READY	(5*HZ)		/* 5sec   - some laptops are very slow */
#define WAIT_PIDENTIFY	(10*HZ)		/* 10sec  - should be less than 3ms (?), if all ATAPI CD is closed at boot */
#define WAIT_WORSTCASE	(30*HZ)		/* 30sec  - worst case when spinning up */
#define WAIT_CMD	(10*HZ)		/* 10sec  - maximum wait for an IRQ to happen */
#define WAIT_MIN_SLEEP	(2*HZ/100)	/* 20msec - minimum sleep time */

/*
 * Check for an interrupt and acknowledge the interrupt status
 */
struct ata_channel;
typedef int (ide_ack_intr_t)(struct ata_channel *);

#ifndef NO_DMA
# define NO_DMA  255
#endif

/*
 * This is used to keep track of the specific hardware chipset used by each IDE
 * interface, if known. Please note that we don't discriminate between
 * different PCI host chips here.
 */
typedef enum {
	ide_unknown,
	ide_generic,
	ide_pci,
        ide_cmd640,
	ide_dtc2278,
	ide_ali14xx,
	ide_qd65xx,
	ide_umc8672,
	ide_ht6560b,
	ide_pdc4030,
	ide_rz1000,
	ide_trm290,
	ide_cmd646,
	ide_cy82c693,
	ide_pmac,
	ide_etrax100,
	ide_acorn
} hwif_chipset_t;


#define IDE_CHIPSET_PCI_MASK    \
    ((1<<ide_pci)|(1<<ide_cmd646)|(1<<ide_ali14xx))
#define IDE_CHIPSET_IS_PCI(c)   ((IDE_CHIPSET_PCI_MASK >> (c)) & 1)

/*
 * Structure to hold all information about the location of this port
 */
typedef struct hw_regs_s {
	ide_ioreg_t	io_ports[IDE_NR_PORTS];		    /* task file registers */
	int		irq;				    /* our irq number */
	int		dma;				    /* our dma entry */
	int		(*ack_intr)(struct ata_channel *);  /* acknowledge interrupt */
	hwif_chipset_t  chipset;
} hw_regs_t;

/*
 * Set up hw_regs_t structure before calling ide_register_hw (optional)
 */
extern void ide_setup_ports(hw_regs_t *hw,
	ide_ioreg_t base, int *offsets,
	ide_ioreg_t ctrl, ide_ioreg_t intr,
	int (*ack_intr)(struct ata_channel *),
	int irq);

#include <asm/ide.h>

/* Currently only m68k, apus and m8xx need it */
#ifdef ATA_ARCH_ACK_INTR
extern int ide_irq_lock;
# define ide_ack_intr(hwif) (hwif->hw.ack_intr ? hwif->hw.ack_intr(hwif) : 1)
#else
# define ide_ack_intr(hwif) (1)
#endif

/* Currently only Atari needs it */
#ifndef ATA_ARCH_LOCK
# define ide_release_lock(lock)		do {} while (0)
# define ide_get_lock(lock, hdlr, data)	do {} while (0)
#endif

/*
 * If the arch-dependant ide.h did not declare/define any OUT_BYTE or IN_BYTE
 * functions, we make some defaults here. The only architecture currently
 * needing this is Cris.
 */

#ifndef HAVE_ARCH_IN_OUT
# define OUT_BYTE(b,p)		outb((b),(p))
# define OUT_WORD(w,p)		outw((w),(p))
# define IN_BYTE(p)		(u8)inb(p)
# define IN_WORD(p)		(u16)inw(p)
#endif

/*
 * Device types - the nomenclature is analogous to SCSI code.
 */

#define ATA_DISK        0x20
#define ATA_TAPE        0x01
#define ATA_ROM         0x05	/* CD-ROM */
#define ATA_MOD		0x07    /* optical */
#define ATA_FLOPPY	0x00
#define ATA_SCSI	0x21
#define ATA_NO_LUN      0x7f

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned head		: 4;	/* always zeros here */
		unsigned unit		: 1;	/* drive select number: 0/1 */
		unsigned bit5		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit7		: 1;	/* always 1 */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned bit7		: 1;
		unsigned lba		: 1;
		unsigned bit5		: 1;
		unsigned unit		: 1;
		unsigned head		: 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} select_t;

/*
 * ATA/ATAPI device structure :
 */
struct ata_device {
	struct ata_channel *	channel;
	char			name[6];	/* device name */

	unsigned int usage;		/* current "open()" count for drive */
	char type; /* distingiush different devices: disk, cdrom, tape, floppy, ... */

	request_queue_t	queue;		/* per device request queue */
	struct request *rq;		/* current request */

	unsigned long sleep;		/* sleep until this time */

	u8	 retry_pio;		/* retrying dma capable host in pio */
	u8	 state;			/* retry state */

	unsigned using_dma	: 1;	/* disk is using dma for read/write */
	unsigned using_tcq	: 1;	/* disk is using queueing */
	unsigned dsc_overlap	: 1;	/* flag: DSC overlap */

	unsigned busy		: 1;	/* currently doing revalidate_disk() */
	unsigned blocked        : 1;	/* 1=powermanagment told us not to do anything, so sleep nicely */

	unsigned present	: 1;	/* drive is physically present */
	unsigned noprobe	: 1;	/* from:  hdx=noprobe */
	unsigned removable	: 1;	/* 1 if need to do check_media_change */
	unsigned forced_geom	: 1;	/* 1 if hdx=c,h,s was given at boot */
	unsigned nobios		: 1;	/* flag: do not probe bios for drive */
	unsigned revalidate	: 1;	/* request revalidation */
	unsigned atapi_overlap	: 1;	/* flag: ATAPI overlap (not supported) */
	unsigned doorlocking	: 1;	/* flag: for removable only: door lock/unlock works */
	unsigned autotune	: 2;	/* 1=autotune, 2=noautotune, 0=default */
	unsigned remap_0_to_1	: 2;	/* 0=remap if ezdrive, 1=remap, 2=noremap */
	unsigned ata_flash	: 1;	/* 1=present, 0=default */
	unsigned	addressing;	/* : 2; 0=28-bit, 1=48-bit, 2=64-bit */
	u8		scsi;		/* 0=default, 1=skip current ide-subdriver for ide-scsi emulation */

	select_t	select;		/* basic drive/head select reg value */
	u8		status;		/* last retrived status value for device */

	u8		ready_stat;	/* min status value for drive ready */
	u8		mult_count;	/* current multiple sector setting */
	u8		bad_wstat;	/* used for ignoring WRERR_STAT */
	u8		nowerr;		/* used for ignoring WRERR_STAT */
	u8		sect0;		/* offset of first sector for DM6:DDO */
	u8		head;		/* "real" number of heads */
	u8		sect;		/* "real" sectors per track */
	u8		bios_head;	/* BIOS/fdisk/LILO number of heads */
	u8		bios_sect;	/* BIOS/fdisk/LILO sectors per track */
	unsigned int	bios_cyl;	/* BIOS/fdisk/LILO number of cyls */
	unsigned int	cyl;		/* "real" number of cyls */
	u64		capacity;	/* total number of sectors */
	unsigned int	drive_data;	/* for use by tuneproc/selectproc as needed */

	wait_queue_head_t wqueue;	/* used to wait for drive in open() */

	struct hd_driveid *id;		/* drive model identification info */
	struct hd_struct  *part;	/* drive partition table */

	struct ata_operations *driver;

	void		*driver_data;	/* extra driver data */
	devfs_handle_t	de;		/* directory for device */

	char		driver_req[10];	/* requests specific driver */

	int		last_lun;	/* last logical unit */
	int		forced_lun;	/* if hdxlun was given at boot */
	int		lun;		/* logical unit */

	int		crc_count;	/* crc counter to reduce drive speed */
	int		quirk_list;	/* drive is considered quirky if set for a specific host */
	u8		current_speed;	/* current transfer rate set */
	u8		dn;		/* now wide spread use */
	u8		wcache;		/* status of write cache */
	u8		acoustic;	/* acoustic management */
	unsigned int	queue_depth;	/* max queue depth */
	unsigned int	failures;	/* current failure count */
	unsigned int	max_failures;	/* maximum allowed failure count */
	struct device	dev;		/* global device tree handle */

	/*
	 * tcq statistics
	 */
	unsigned long	immed_rel;
	unsigned long	immed_comp;
	int		max_last_depth;
	int		max_depth;
};

/*
 * Status returned by various functions.
 */
typedef enum {
	ATA_OP_FINISHED,	/* no drive operation was started */
	ATA_OP_CONTINUES,	/* a drive operation was started, and a handler was set */
	ATA_OP_RELEASED,	/* started and released bus */
	ATA_OP_READY		/* indicate status poll finished fine */
} ide_startstop_t;

/*
 *  Interrupt and timeout handler type.
 */
typedef ide_startstop_t (ata_handler_t)(struct ata_device *, struct request *);
typedef ide_startstop_t (ata_expiry_t)(struct ata_device *, struct request *, unsigned long *);

enum {
	ATA_PRIMARY	= 0,
	ATA_SECONDARY	= 1
};

enum {
	IDE_BUSY,	/* awaiting an interrupt */
	IDE_SLEEP,
	IDE_PIO,	/* PIO in progress */
	IDE_DMA		/* DMA in progress */
};

struct ata_channel {
	struct device	dev;		/* device handle */
	int		unit;		/* channel number */

	/* This lock is used to serialize requests on the same device queue or
	 * between differen queues sharing the same irq line.
	 */
	spinlock_t *lock;
	unsigned long *active;		/* active processing request */
	ide_startstop_t (*handler)(struct ata_device *, struct request *);	/* irq handler, if active */

	/* FIXME: Only still used in PDC4030.  Localize this code there by
	 * replacing with busy waits.
	 */
	struct timer_list timer;				/* failsafe timer */
	ide_startstop_t (*expiry)(struct ata_device *, struct request *, unsigned long *);	/* irq handler, if active */
	unsigned long poll_timeout;				/* timeout value during polled operations */

	struct ata_device *drive;				/* last serviced drive */


	ide_ioreg_t io_ports[IDE_NR_PORTS];	/* task file registers */
	hw_regs_t hw;				/* hardware info */
#ifdef CONFIG_PCI
	struct pci_dev *pci_dev;		/* for pci chipsets */
#endif
	struct ata_device drives[MAX_DRIVES];	/* drive info */
	struct gendisk *gd;			/* gendisk structure */

	/*
	 * Routines to tune PIO and DMA mode for drives.
	 *
	 * A value of 255 indicates that the function should choose the optimal
	 * mode itself.
	 */

	/* setup disk on a channel for a particular PIO transfer mode */
	void (*tuneproc) (struct ata_device *, u8 pio);

	/* setup the chipset timing for a particular transfer mode */
	int (*speedproc) (struct ata_device *, u8 pio);

	/* tweaks hardware to select drive */
	void (*selectproc) (struct ata_device *);

	/* routine to reset controller after a disk reset */
	void (*resetproc) (struct ata_device *);

	/* special interrupt handling for shared pci interrupts */
	void (*intrproc) (struct ata_device *);

	/* special host masking for drive selection */
	void (*maskproc) (struct ata_device *);

	/* check host's drive quirk list */
	int (*quirkproc) (struct ata_device *);

	/* driver soft-power interface */
	int (*busproc)(struct ata_device *, int);

	/* CPU-polled transfer routines */
	void (*ata_read)(struct ata_device *, void *, unsigned int);
	void (*ata_write)(struct ata_device *, void *, unsigned int);
	void (*atapi_read)(struct ata_device *, void *, unsigned int);
	void (*atapi_write)(struct ata_device *, void *, unsigned int);

	int (*udma_setup)(struct ata_device *, int);

	void (*udma_enable)(struct ata_device *, int, int);
	void (*udma_start) (struct ata_device *, struct request *);
	int (*udma_stop) (struct ata_device *);
	int (*udma_init) (struct ata_device *, struct request *);
	int (*udma_irq_status) (struct ata_device *);
	void (*udma_timeout) (struct ata_device *);
	void (*udma_irq_lost) (struct ata_device *);

	unsigned int	*dmatable_cpu;	/* dma physical region descriptor table (cpu view) */
	dma_addr_t	dmatable_dma;	/* dma physical region descriptor table (dma view) */
	struct scatterlist *sg_table;	/* Scatter-gather list used to build the above */
	int sg_nents;			/* Current number of entries in it */
	int sg_dma_direction;		/* dma transfer direction */
	unsigned long	dma_base;	/* base addr for dma ports */
	unsigned	dma_extra;	/* extra addr for dma ports */
	unsigned long	config_data;	/* for use by chipset-specific code */
	unsigned long	select_data;	/* for use by chipset-specific code */
	struct proc_dir_entry *proc;	/* /proc/ide/ directory entry */
	int		irq;		/* our irq number */
	int		major;		/* our major number */
	char		name[8];	/* name of interface */
	int		index;		/* 0 for ide0; 1 for ide1; ... */
	hwif_chipset_t	chipset;	/* sub-module for tuning.. */
	unsigned noprobe	: 1;	/* don't probe for this interface */
	unsigned present	: 1;	/* there is a device on this interface */
	unsigned serialized	: 1;	/* serialized operation between channels */
	unsigned sharing_irq	: 1;	/* 1 = sharing irq with another hwif */
	unsigned reset		: 1;	/* reset after probe */
	unsigned autodma	: 1;	/* automatically try to enable DMA at boot */
	unsigned udma_four	: 1;	/* 1=ATA-66 capable, 0=default */
	unsigned highmem	: 1;	/* can do full 32-bit dma */
	unsigned straight8	: 1;	/* Alan's straight 8 check */
	unsigned no_io_32bit	: 1;	/* disallow enabling 32bit I/O */
	unsigned no_unmask	: 1;	/* disallow setting unmask bit */
	unsigned auto_poll	: 1;	/* supports nop auto-poll */
	unsigned unmask		: 1;	/* flag: okay to unmask other irqs */
	unsigned slow		: 1;	/* flag: slow data port */
	unsigned io_32bit	: 1;	/* 0=16-bit, 1=32-bit */
	unsigned no_atapi_autodma : 1;	/* flag: use auto DMA only for disks */
	unsigned char bus_state;	/* power state of the IDE bus */
	int modes_map;			/* map of supported transfer modes */
};

/*
 * Register new hardware with ide
 */
extern int ide_register_hw(hw_regs_t *hw);
extern void ide_unregister(struct ata_channel *);

#define IDE_MAX_TAG	32

#ifdef CONFIG_BLK_DEV_IDE_TCQ
static inline int ata_pending_commands(struct ata_device *drive)
{
	if (drive->using_tcq)
		return blk_queue_tag_depth(&drive->queue);

	return 0;
}

static inline int ata_can_queue(struct ata_device *drive)
{
	if (drive->using_tcq)
		return blk_queue_tag_queue(&drive->queue);

	return 1;
}
#else
# define ata_pending_commands(drive)	(0)
# define ata_can_queue(drive)		(1)
#endif

/* FIXME: kill this as soon as possible */
#define PROC_IDE_READ_RETURN(page,start,off,count,eof,len) return 0;

/*
 * This structure describes the operations possible on a particular device type
 * (CD-ROM, tape, DISK and so on).
 *
 * This is the main hook for device type support submodules.
 */

struct ata_operations {
	struct module *owner;
	void (*attach) (struct ata_device *);
	int (*cleanup)(struct ata_device *);
	int (*standby)(struct ata_device *);
	ide_startstop_t	(*do_request)(struct ata_device *, struct request *, sector_t);
	int (*end_request)(struct ata_device *, struct request *, int);

	int (*ioctl)(struct ata_device *, struct inode *, struct file *, unsigned int, unsigned long);
	int (*open)(struct inode *, struct file *, struct ata_device *);
	void (*release)(struct inode *, struct file *, struct ata_device *);
	int (*check_media_change)(struct ata_device *);
	void (*revalidate)(struct ata_device *);

	sector_t (*capacity)(struct ata_device *);

	/* linked list of rgistered device type drivers */
	struct ata_operations *next;
};

/* Alas, no aliases. Too much hassle with bringing module.h everywhere */
#define ata_get(ata) \
	(((ata) && (ata)->owner)	\
		? ( try_inc_mod_count((ata)->owner) ? (ata) : NULL ) \
		: (ata))

#define ata_put(ata) \
do {	\
	if ((ata) && (ata)->owner) \
		__MOD_DEC_USE_COUNT((ata)->owner);	\
} while(0)

extern sector_t ata_capacity(struct ata_device *drive);

extern void unregister_ata_driver(struct ata_operations *driver);
extern int register_ata_driver(struct ata_operations *driver);
static inline int ata_driver_module(struct ata_operations *driver)
{
#ifdef MODULE
	if (register_ata_driver(driver) <= 0) {
		unregister_ata_driver(driver);
		return -ENODEV;
	}
#else
	register_ata_driver(driver);
#endif
	return 0;
}

#define ata_ops(drive)		((drive)->driver)

extern struct ata_channel ide_hwifs[];		/* master data repository */
extern int noautodma;

/*
 * We need blk.h, but we replace its end_request by our own version.
 */
#define IDE_DRIVER		/* Toggle some magic bits in blk.h */
#define LOCAL_END_REQUEST	/* Don't generate end_request in blk.h */
#define DEVICE_NR(device)	(minor(device) >> PARTN_BITS)
#include <linux/blk.h>

extern int ata_end_request(struct ata_device *, struct request *, int, unsigned int);
extern void ata_set_handler(struct ata_device *drive, ata_handler_t handler,
		unsigned long timeout, ata_expiry_t expiry);

extern u8 ata_dump(struct ata_device *, struct request *, const char *);
extern ide_startstop_t ata_error(struct ata_device *, struct request *rq, const char *);

extern void ide_fixstring(char *s, const int bytecount, const int byteswap);

/*
 * This routine is called from the partition-table code in genhd.c
 * to "convert" a drive to a logical geometry with fewer than 1024 cyls.
 */
int ide_xlate_1024(kdev_t, int, int, const char *);

/*
 * Convert kdev_t structure into struct ata_device * one.
 */
struct ata_device *get_info_ptr(kdev_t i_rdev);

/*
 * temporarily mapping a (possible) highmem bio for PIO transfer
 */
#define ide_rq_offset(rq) (((rq)->hard_cur_sectors - (rq)->current_nr_sectors) << 9)

struct ata_taskfile {
	struct hd_drive_task_hdr taskfile;
	struct hd_drive_task_hdr  hobfile;
	u8 cmd;					/* actual ATA command */
	int command_type;
	ide_startstop_t (*XXX_handler)(struct ata_device *, struct request *);
};

extern void ata_read(struct ata_device *, void *, unsigned int);
extern void ata_write(struct ata_device *, void *, unsigned int);

extern int ide_raw_taskfile(struct ata_device *, struct ata_taskfile *, char *);
extern int ide_config_drive_speed(struct ata_device *, u8);
extern int eighty_ninty_three(struct ata_device *);

extern int system_bus_speed;

/*
 * CompactFlash cards and their brethern pretend to be removable hard disks,
 * but they never have a slave unit, and they don't have doorlock mechanisms.
 * This test catches them, and is invoked elsewhere when setting appropriate
 * config bits.
 */

extern int drive_is_flashcard(struct ata_device *);

extern int ide_spin_wait_hwgroup(struct ata_device *);
extern void ide_timer_expiry(unsigned long data);
extern void ata_irq_request(int irq, void *data, struct pt_regs *regs);
extern void do_ide_request(request_queue_t * q);
extern void ide_init_subdrivers(void);

extern struct block_device_operations ide_fops[];

/* Probe for devices attached to the systems host controllers.
 */
extern int ideprobe_init(void);
#ifdef CONFIG_BLK_DEV_IDEDISK
extern int idedisk_init (void);
#endif
#ifdef CONFIG_BLK_DEV_IDECD
extern int ide_cdrom_init (void);
#endif
#ifdef CONFIG_BLK_DEV_IDETAPE
extern int idetape_init (void);
#endif
#ifdef CONFIG_BLK_DEV_IDEFLOPPY
extern int idefloppy_init (void);
#endif
#ifdef CONFIG_BLK_DEV_IDESCSI
extern int idescsi_init (void);
#endif

extern int ide_register_subdriver(struct ata_device *, struct ata_operations *);
extern int ide_unregister_subdriver(struct ata_device *drive);
extern int ata_revalidate(kdev_t i_rdev);
extern void ide_driver_module(void);

#ifdef CONFIG_PCI
# define ON_BOARD		0
# define NEVER_BOARD		1
# ifdef CONFIG_BLK_DEV_OFFBOARD
#  define OFF_BOARD		ON_BOARD
# else
#  define OFF_BOARD		NEVER_BOARD
# endif

void __init ide_scan_pcibus(int scan_direction);
#endif

static inline void udma_enable(struct ata_device *drive, int on, int verbose)
{
	drive->channel->udma_enable(drive, on, verbose);
}

static inline void udma_start(struct ata_device *drive, struct request *rq)
{
	drive->channel->udma_start(drive, rq);
}

static inline int udma_stop(struct ata_device *drive)
{
	int ret;

	ret = drive->channel->udma_stop(drive);
	clear_bit(IDE_DMA, drive->channel->active);

	return ret;
}

/*
 * Initiate actual DMA data transfer. The direction is encoded in the request.
 */
static inline ide_startstop_t udma_init(struct ata_device *drive, struct request *rq)
{
	int ret;

	set_bit(IDE_DMA, drive->channel->active);
	ret = drive->channel->udma_init(drive, rq);
	if (ret != ATA_OP_CONTINUES)
		clear_bit(IDE_DMA, drive->channel->active);

	return ret;
}

static inline int udma_irq_status(struct ata_device *drive)
{
	return drive->channel->udma_irq_status(drive);
}

static inline void udma_timeout(struct ata_device *drive)
{
	drive->channel->udma_timeout(drive);
}

static inline void udma_irq_lost(struct ata_device *drive)
{
	drive->channel->udma_irq_lost(drive);
}

#ifdef CONFIG_BLK_DEV_IDEDMA

extern void udma_pci_enable(struct ata_device *drive, int on, int verbose);
extern void udma_pci_start(struct ata_device *drive, struct request *rq);
extern int udma_pci_stop(struct ata_device *drive);
extern int udma_pci_init(struct ata_device *drive, struct request *rq);
extern int udma_pci_irq_status(struct ata_device *drive);
extern void udma_pci_timeout(struct ata_device *drive);
extern void udma_pci_irq_lost(struct ata_device *);
extern int udma_pci_setup(struct ata_device *, int);

extern int udma_generic_setup(struct ata_device *, int);

extern int udma_new_table(struct ata_device *, struct request *);
extern void udma_destroy_table(struct ata_channel *);
extern void udma_print(struct ata_device *);

extern int udma_black_list(struct ata_device *);
extern int udma_white_list(struct ata_device *);

extern ide_startstop_t udma_tcq_init(struct ata_device *, struct request *);
extern int udma_tcq_enable(struct ata_device *, int);

extern ide_startstop_t ide_dma_intr(struct ata_device *, struct request *);
extern int check_drive_lists(struct ata_device *, int good_bad);
extern void ide_release_dma(struct ata_channel *);
extern int ata_start_dma(struct ata_device *, struct request *rq);

extern void ata_init_dma(struct ata_channel *,	unsigned long) __init;

#endif

extern void ata_fix_driveid(struct hd_driveid *);

extern spinlock_t ide_lock;

#define DRIVE_LOCK(drive)	((drive)->queue.queue_lock)

/* Low level device access functions. */

extern void ata_select(struct ata_device *, unsigned long);
extern void ata_mask(struct ata_device *);
extern int ata_status(struct ata_device *, u8, u8);
extern int ata_status_irq(struct ata_device *drive);
extern int ata_status_poll( struct ata_device *, u8, u8,
		unsigned long, struct request *rq);

extern int ata_irq_enable(struct ata_device *, int);
extern void ata_reset(struct ata_channel *);
extern void ata_out_regfile(struct ata_device *, struct hd_drive_task_hdr *);
extern void ata_in_regfile(struct ata_device *, struct hd_drive_task_hdr *);

#endif

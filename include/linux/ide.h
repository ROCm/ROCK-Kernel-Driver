#ifndef _IDE_H
#define _IDE_H
/*
 *  linux/include/linux/ide.h
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/hdsmart.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/bio.h>
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/hdreg.h>
#include <asm/io.h>

#ifdef CONFIG_BLK_DEV_IDEDMA_TIMEOUT
#  define __IDEDMA_TIMEOUT
#else /* CONFIG_BLK_DEV_IDEDMA_TIMEOUT */
#  undef __IDEDMA_TIMEOUT
#endif /* CONFIG_BLK_DEV_IDEDMA_TIMEOUT */

/*
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to four IDE interfaces, on one or more IRQs (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * Primary i/f:    ide0: major=3;  (hda)         minor=0; (hdb)         minor=64
 * Secondary i/f:  ide1: major=22; (hdc or hd1a) minor=0; (hdd or hd1b) minor=64
 * Tertiary i/f:   ide2: major=33; (hde)         minor=0; (hdf)         minor=64
 * Quaternary i/f: ide3: major=34; (hdg)         minor=0; (hdh)         minor=64
 */

/******************************************************************************
 * IDE driver configuration options (play with these as desired):
 *
 * REALLY_SLOW_IO can be defined in ide.c and ide-cd.c, if necessary
 */
#undef REALLY_FAST_IO			/* define if ide ports are perfect */
#define INITIAL_MULT_COUNT	0	/* off=0; on=2,4,8,16,32, etc.. */

#ifndef SUPPORT_SLOW_DATA_PORTS		/* 1 to support slow data ports */
#define SUPPORT_SLOW_DATA_PORTS	1	/* 0 to reduce kernel size */
#endif
#ifndef SUPPORT_VLB_SYNC		/* 1 to support weird 32-bit chips */
#define SUPPORT_VLB_SYNC	1	/* 0 to reduce kernel size */
#endif
#ifndef DISK_RECOVERY_TIME		/* off=0; on=access_delay_time */
#define DISK_RECOVERY_TIME	0	/*  for hardware that needs it */
#endif
#ifndef OK_TO_RESET_CONTROLLER		/* 1 needed for good error recovery */
#define OK_TO_RESET_CONTROLLER	1	/* 0 for use with AH2372A/B interface */
#endif
#ifndef FANCY_STATUS_DUMPS		/* 1 for human-readable drive errors */
#define FANCY_STATUS_DUMPS	1	/* 0 to reduce kernel size */
#endif

#ifdef CONFIG_BLK_DEV_CMD640
#if 0	/* change to 1 when debugging cmd640 problems */
void cmd640_dump_regs (void);
#define CMD640_DUMP_REGS cmd640_dump_regs() /* for debugging cmd640 chipset */
#endif
#endif  /* CONFIG_BLK_DEV_CMD640 */

#ifndef DISABLE_IRQ_NOSYNC
#define DISABLE_IRQ_NOSYNC	0
#endif

/*
 * IDE_DRIVE_CMD is used to implement many features of the hdparm utility
 */
#define IDE_DRIVE_CMD			99	/* (magic) undef to reduce kernel size*/

#define IDE_DRIVE_TASK			98

/*
 * IDE_DRIVE_TASKFILE is used to implement many features needed for raw tasks
 */
#define IDE_DRIVE_TASKFILE		97

/*
 *  "No user-serviceable parts" beyond this point  :)
 *****************************************************************************/

typedef unsigned char	byte;	/* used everywhere */

/*
 * Probably not wise to fiddle with these
 */
#define ERROR_MAX	8	/* Max read/write errors per sector */
#define ERROR_RESET	3	/* Reset controller every 4th retry */
#define ERROR_RECAL	1	/* Recalibrate every 2nd retry */

/*
 * state flags
 */
#define DMA_PIO_RETRY	1	/* retrying in PIO */

/*
 * Ensure that various configuration flags have compatible settings
 */
#ifdef REALLY_SLOW_IO
#undef REALLY_FAST_IO
#endif

#define HWIF(drive)		((ide_hwif_t *)((drive)->hwif))
#define HWGROUP(drive)		((ide_hwgroup_t *)(HWIF(drive)->hwgroup))

/*
 * Definitions for accessing IDE controller registers
 */
#define IDE_NR_PORTS		(10)

#define IDE_DATA_OFFSET		(0)
#define IDE_ERROR_OFFSET	(1)
#define IDE_NSECTOR_OFFSET	(2)
#define IDE_SECTOR_OFFSET	(3)
#define IDE_LCYL_OFFSET		(4)
#define IDE_HCYL_OFFSET		(5)
#define IDE_SELECT_OFFSET	(6)
#define IDE_STATUS_OFFSET	(7)
#define IDE_CONTROL_OFFSET	(8)
#define IDE_IRQ_OFFSET		(9)

#define IDE_FEATURE_OFFSET	IDE_ERROR_OFFSET
#define IDE_COMMAND_OFFSET	IDE_STATUS_OFFSET

#define IDE_DATA_OFFSET_HOB	(0)
#define IDE_ERROR_OFFSET_HOB	(1)
#define IDE_NSECTOR_OFFSET_HOB	(2)
#define IDE_SECTOR_OFFSET_HOB	(3)
#define IDE_LCYL_OFFSET_HOB	(4)
#define IDE_HCYL_OFFSET_HOB	(5)
#define IDE_SELECT_OFFSET_HOB	(6)
#define IDE_CONTROL_OFFSET_HOB	(7)

#define IDE_FEATURE_OFFSET_HOB	IDE_ERROR_OFFSET_HOB

#define IDE_DATA_REG		(HWIF(drive)->io_ports[IDE_DATA_OFFSET])
#define IDE_ERROR_REG		(HWIF(drive)->io_ports[IDE_ERROR_OFFSET])
#define IDE_NSECTOR_REG		(HWIF(drive)->io_ports[IDE_NSECTOR_OFFSET])
#define IDE_SECTOR_REG		(HWIF(drive)->io_ports[IDE_SECTOR_OFFSET])
#define IDE_LCYL_REG		(HWIF(drive)->io_ports[IDE_LCYL_OFFSET])
#define IDE_HCYL_REG		(HWIF(drive)->io_ports[IDE_HCYL_OFFSET])
#define IDE_SELECT_REG		(HWIF(drive)->io_ports[IDE_SELECT_OFFSET])
#define IDE_STATUS_REG		(HWIF(drive)->io_ports[IDE_STATUS_OFFSET])
#define IDE_CONTROL_REG		(HWIF(drive)->io_ports[IDE_CONTROL_OFFSET])
#define IDE_IRQ_REG		(HWIF(drive)->io_ports[IDE_IRQ_OFFSET])

#define IDE_DATA_REG_HOB	(HWIF(drive)->io_ports[IDE_DATA_OFFSET])
#define IDE_ERROR_REG_HOB	(HWIF(drive)->io_ports[IDE_ERROR_OFFSET])
#define IDE_NSECTOR_REG_HOB	(HWIF(drive)->io_ports[IDE_NSECTOR_OFFSET])
#define IDE_SECTOR_REG_HOB	(HWIF(drive)->io_ports[IDE_SECTOR_OFFSET])
#define IDE_LCYL_REG_HOB	(HWIF(drive)->io_ports[IDE_LCYL_OFFSET])
#define IDE_HCYL_REG_HOB	(HWIF(drive)->io_ports[IDE_HCYL_OFFSET])
#define IDE_SELECT_REG_HOB	(HWIF(drive)->io_ports[IDE_SELECT_OFFSET])
#define IDE_STATUS_REG_HOB	(HWIF(drive)->io_ports[IDE_STATUS_OFFSET])
#define IDE_CONTROL_REG_HOB	(HWIF(drive)->io_ports[IDE_CONTROL_OFFSET])

#define IDE_FEATURE_REG		IDE_ERROR_REG
#define IDE_COMMAND_REG		IDE_STATUS_REG
#define IDE_ALTSTATUS_REG	IDE_CONTROL_REG
#define IDE_IREASON_REG		IDE_NSECTOR_REG
#define IDE_BCOUNTL_REG		IDE_LCYL_REG
#define IDE_BCOUNTH_REG		IDE_HCYL_REG

#define GET_ERR()		IN_BYTE(IDE_ERROR_REG)
#define GET_STAT()		IN_BYTE(IDE_STATUS_REG)
#define GET_ALTSTAT()		IN_BYTE(IDE_CONTROL_REG)
#define OK_STAT(stat,good,bad)	(((stat)&((good)|(bad)))==(good))
#define BAD_R_STAT		(BUSY_STAT   | ERR_STAT)
#define BAD_W_STAT		(BAD_R_STAT  | WRERR_STAT)
#define BAD_STAT		(BAD_R_STAT  | DRQ_STAT)
#define DRIVE_READY		(READY_STAT  | SEEK_STAT)
#define DATA_READY		(DRQ_STAT)

/*
 * sector count bits
 */
#define NSEC_CD			0x01
#define NSEC_IO			0x02
#define NSEC_REL		0x04


/*
 * Our Physical Region Descriptor (PRD) table should be large enough
 * to handle the biggest I/O request we are likely to see.  Since requests
 * can have no more than 256 sectors, and since the typical blocksize is
 * two or more sectors, we could get by with a limit of 128 entries here for
 * the usual worst case.  Most requests seem to include some contiguous blocks,
 * further reducing the number of table entries required.
 *
 * The driver reverts to PIO mode for individual requests that exceed
 * this limit (possible with 512 byte blocksizes, eg. MSDOS f/s), so handling
 * 100% of all crazy scenarios here is not necessary.
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
#define CASCADE_DRIVES	8	/* per interface; 8|2 assumed by lots of code */
#define SECTOR_SIZE	512
#define SECTOR_WORDS	(SECTOR_SIZE / 4)	/* number of 32bit words per sector */
#define IDE_LARGE_SEEK(b1,b2,t)	(((b1) > (b2) + (t)) || ((b2) > (b1) + (t)))
#define IDE_MIN(a,b)	((a)<(b) ? (a):(b))
#define IDE_MAX(a,b)	((a)>(b) ? (a):(b))

#ifndef SPLIT_WORD
#  define SPLIT_WORD(W,HB,LB) ((HB)=(W>>8), (LB)=(W-((W>>8)<<8)))
#endif
#ifndef MAKE_WORD
#  define MAKE_WORD(W,HB,LB) ((W)=((HB<<8)+LB))
#endif


/*
 * Timeouts for various operations:
 */
#define WAIT_DRQ	(5*HZ/100)	/* 50msec - spec allows up to 20ms */
#if defined(CONFIG_APM) || defined(CONFIG_APM_MODULE)
#define WAIT_READY	(5*HZ)		/* 5sec - some laptops are very slow */
#else
#define WAIT_READY	(3*HZ/100)	/* 30msec - should be instantaneous */
#endif /* CONFIG_APM || CONFIG_APM_MODULE */
#define WAIT_PIDENTIFY	(10*HZ)	/* 10sec  - should be less than 3ms (?), if all ATAPI CD is closed at boot */
#define WAIT_WORSTCASE	(30*HZ)	/* 30sec  - worst case when spinning up */
#define WAIT_CMD	(10*HZ)	/* 10sec  - maximum wait for an IRQ to happen */
#define WAIT_MIN_SLEEP	(2*HZ/100)	/* 20msec - minimum sleep time */

#define SELECT_DRIVE(hwif,drive)				\
{								\
	if (hwif->selectproc)					\
		hwif->selectproc(drive);			\
	OUT_BYTE((drive)->select.all, hwif->io_ports[IDE_SELECT_OFFSET]); \
}

#define SELECT_INTERRUPT(hwif,drive)				\
{								\
	if (hwif->intrproc)					\
		hwif->intrproc(drive);				\
	else							\
		OUT_BYTE((drive)->ctl|2, hwif->io_ports[IDE_CONTROL_OFFSET]);	\
}

#define SELECT_MASK(hwif,drive,mask)				\
{								\
	if (hwif->maskproc)					\
		hwif->maskproc(drive,mask);			\
}

#define SELECT_READ_WRITE(hwif,drive,func)			\
{								\
	if (hwif->rwproc)					\
		hwif->rwproc(drive,func);			\
}

#define QUIRK_LIST(hwif,drive)					\
{								\
	if (hwif->quirkproc)					\
		(drive)->quirk_list = hwif->quirkproc(drive);	\
}

#define HOST(hwif,chipset)					\
{								\
	return ((hwif)->chipset == chipset) ? 1 : 0;		\
}

#define IDE_DEBUG(lineno) \
	printk("%s,%s,line=%d\n", __FILE__, __FUNCTION__, (lineno))

/*
 * Check for an interrupt and acknowledge the interrupt status
 */
struct hwif_s;
typedef int (ide_ack_intr_t)(struct hwif_s *);

#ifndef NO_DMA
#define NO_DMA  255
#endif

/*
 * hwif_chipset_t is used to keep track of the specific hardware
 * chipset used by each IDE interface, if known.
 */
typedef enum {	ide_unknown,	ide_generic,	ide_pci,
		ide_cmd640,	ide_dtc2278,	ide_ali14xx,
		ide_qd65xx,	ide_umc8672,	ide_ht6560b,
		ide_pdc4030,	ide_rz1000,	ide_trm290,
		ide_cmd646,	ide_cy82c693,	ide_4drives,
		ide_pmac,	ide_etrax100
} hwif_chipset_t;

/*
 * Structure to hold all information about the location of this port
 */
typedef struct hw_regs_s {
	ide_ioreg_t	io_ports[IDE_NR_PORTS];	/* task file registers */
	int		irq;			/* our irq number */
	int		dma;			/* our dma entry */
	ide_ack_intr_t	*ack_intr;		/* acknowledge interrupt */
	void		*priv;			/* interface specific data */
	hwif_chipset_t  chipset;
} hw_regs_t;

/*
 * Register new hardware with ide
 */
int ide_register_hw(hw_regs_t *hw, struct hwif_s **hwifp);

/*
 * Set up hw_regs_t structure before calling ide_register_hw (optional)
 */
void ide_setup_ports(	hw_regs_t *hw,
			ide_ioreg_t base,
			int *offsets,
			ide_ioreg_t ctrl,
			ide_ioreg_t intr,
			ide_ack_intr_t *ack_intr,
			int irq);

#include <asm/ide.h>

/*
 * If the arch-dependant ide.h did not declare/define any OUT_BYTE
 * or IN_BYTE functions, we make some defaults here.
 */

#ifndef HAVE_ARCH_OUT_BYTE
# ifdef REALLY_FAST_IO
#  define OUT_BYTE(b,p)		outb((b),(p))
#  define OUT_WORD(w,p)		outw((w),(p))
# else
#  define OUT_BYTE(b,p)		outb_p((b),(p))
#  define OUT_WORD(w,p)		outw_p((w),(p))
# endif
#endif

#ifndef HAVE_ARCH_IN_BYTE
# ifdef REALLY_FAST_IO
#  define IN_BYTE(p)		(byte)inb(p)
#  define IN_WORD(p)		(short)inw(p)
# else
#  define IN_BYTE(p)		(byte)inb_p(p)
#  define IN_WORD(p)		(short)inw_p(p)
# endif
#endif

/*
 * Now for the data we need to maintain per-drive:  ide_drive_t
 */

#define ide_scsi	0x21
#define ide_disk	0x20
#define ide_optical	0x7
#define ide_cdrom	0x5
#define ide_tape	0x1
#define ide_floppy	0x0

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned set_geometry	: 1;	/* respecify drive geometry */
		unsigned recalibrate	: 1;	/* seek to cyl 0      */
		unsigned set_multmode	: 1;	/* set multmode count */
		unsigned set_tune	: 1;	/* tune interface for drive */
		unsigned serviced	: 1;	/* service command */
		unsigned reserved	: 3;	/* unused */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned reserved	: 3;	/* unused */
		unsigned serviced	: 1;	/* service command */
		unsigned set_tune	: 1;	/* tune interface for drive */
		unsigned set_multmode	: 1;	/* set multmode count */
		unsigned recalibrate	: 1;	/* seek to cyl 0      */
		unsigned set_geometry	: 1;	/* respecify drive geometry */
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} special_t;

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
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number: 0/1 */
		unsigned head		: 4;	/* always zeros here */
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} select_t;

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned bit0		: 1;
		unsigned nIEN		: 1;	/* device INTRQ to host */
		unsigned SRST		: 1;	/* host soft reset bit */
		unsigned bit3		: 1;	/* ATA-2 thingy */
		unsigned reserved456    : 3;
		unsigned HOB            : 1;	/* 48-bit address ordering */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned HOB		: 1;	/* 48-bit address ordering */
		unsigned reserved456	: 3;
		unsigned bit3		: 1;	/* ATA-2 thingy */
		unsigned SRST		: 1;	/* host soft reset bit */
		unsigned nIEN		: 1;	/* device INTRQ to host */
		unsigned bit0		: 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} control_t;


struct ide_driver_s;
struct ide_settings_s;

typedef struct ide_drive_s {
	request_queue_t		 queue;	/* request queue */
	struct ide_drive_s 	*next;	/* circular list of hwgroup drives */
	unsigned long sleep;		/* sleep until this time */
	unsigned long service_start;	/* time we started last request */
	unsigned long service_time;	/* service time of last request */
	unsigned long timeout;		/* max time to wait for irq */
	special_t	special;	/* special action flags */
	byte     keep_settings;		/* restore settings after drive reset */
	byte     using_dma;		/* disk is using dma for read/write */
	byte	 retry_pio;		/* retrying dma capable host in pio */
	byte	 state;			/* retry state */
	byte     waiting_for_dma;	/* dma currently in progress */
	byte     unmask;		/* flag: okay to unmask other irqs */
	byte     slow;			/* flag: slow data port */
	byte     bswap;			/* flag: byte swap data */
	byte     dsc_overlap;		/* flag: DSC overlap */
	byte     nice1;			/* flag: give potential excess bandwidth */
	unsigned present	: 1;	/* drive is physically present */
	unsigned noprobe 	: 1;	/* from:  hdx=noprobe */
	unsigned busy		: 1;	/* currently doing revalidate_disk() */
	unsigned removable	: 1;	/* 1 if need to do check_media_change */
	unsigned forced_geom	: 1;	/* 1 if hdx=c,h,s was given at boot */
	unsigned no_unmask	: 1;	/* disallow setting unmask bit */
	unsigned no_io_32bit	: 1;	/* disallow enabling 32bit I/O */
	unsigned nobios		: 1;	/* flag: do not probe bios for drive */
	unsigned revalidate	: 1;	/* request revalidation */
	unsigned atapi_overlap	: 1;	/* flag: ATAPI overlap (not supported) */
	unsigned nice0		: 1;	/* flag: give obvious excess bandwidth */
	unsigned nice2		: 1;	/* flag: give a share in our own bandwidth */
	unsigned doorlocking	: 1;	/* flag: for removable only: door lock/unlock works */
	unsigned autotune	: 2;	/* 1=autotune, 2=noautotune, 0=default */
	unsigned remap_0_to_1	: 2;	/* 0=remap if ezdrive, 1=remap, 2=noremap */
	unsigned ata_flash	: 1;	/* 1=present, 0=default */
	unsigned addressing;		/*	: 3;
					 *  0=28-bit
					 *  1=48-bit
					 *  2=48-bit doing 28-bit
					 *  3=64-bit
					 */
	byte		scsi;		/* 0=default, 1=skip current ide-subdriver for ide-scsi emulation */
	byte		media;		/* disk, cdrom, tape, floppy, ... */
	select_t	select;		/* basic drive/head select reg value */
	byte		ctl;		/* "normal" value for IDE_CONTROL_REG */
	byte		ready_stat;	/* min status value for drive ready */
	byte		mult_count;	/* current multiple sector setting */
	byte 		mult_req;	/* requested multiple sector setting */
	byte 		tune_req;	/* requested drive tuning setting */
	byte		io_32bit;	/* 0=16-bit, 1=32-bit, 2/3=32bit+sync */
	byte		bad_wstat;	/* used for ignoring WRERR_STAT */
	byte		nowerr;		/* used for ignoring WRERR_STAT */
	byte		sect0;		/* offset of first sector for DM6:DDO */
	unsigned int	usage;		/* current "open()" count for drive */
	byte 		head;		/* "real" number of heads */
	byte		sect;		/* "real" sectors per track */
	byte		bios_head;	/* BIOS/fdisk/LILO number of heads */
	byte		bios_sect;	/* BIOS/fdisk/LILO sectors per track */
	unsigned int	bios_cyl;	/* BIOS/fdisk/LILO number of cyls */
	unsigned int	cyl;		/* "real" number of cyls */
	unsigned long	capacity;	/* total number of sectors */
	unsigned long long capacity48;	/* total number of sectors */
	unsigned int	drive_data;	/* for use by tuneproc/selectproc as needed */
	struct hwif_s	  *hwif;	/* actually (ide_hwif_t *) */
	wait_queue_head_t wqueue;	/* used to wait for drive in open() */
	struct hd_driveid *id;		/* drive model identification info */
	struct hd_struct  *part;	/* drive partition table */
	char		name[4];	/* drive name, such as "hda" */
	struct ide_driver_s *driver;	/* (ide_driver_t *) */
	void		*driver_data;	/* extra driver data */
	devfs_handle_t	de;		/* directory for device */
	struct proc_dir_entry *proc;	/* /proc/ide/ directory entry */
	struct ide_settings_s *settings;	/* /proc/ide/ drive settings */
	char		driver_req[10];	/* requests specific driver */
	int		last_lun;	/* last logical unit */
	int		forced_lun;	/* if hdxlun was given at boot */
	int		lun;		/* logical unit */
	int		crc_count;	/* crc counter to reduce drive speed */
	byte		quirk_list;	/* drive is considered quirky if set for a specific host */
	byte		suspend_reset;	/* drive suspend mode flag, soft-reset recovers */
	byte		init_speed;	/* transfer rate set at boot */
	byte		current_speed;	/* current transfer rate set */
	byte		dn;		/* now wide spread use */
	byte		wcache;		/* status of write cache */
	byte		acoustic;	/* acoustic management */
	unsigned int	failures;	/* current failure count */
	unsigned int	max_failures;	/* maximum allowed failure count */
	struct list_head list;
} ide_drive_t;

/*
 * An ide_dmaproc_t() initiates/aborts DMA read/write operations on a drive.
 *
 * The caller is assumed to have selected the drive and programmed the drive's
 * sector address using CHS or LBA.  All that remains is to prepare for DMA
 * and then issue the actual read/write DMA/PIO command to the drive.
 *
 * Returns 0 if all went well.
 * Returns 1 if DMA read/write could not be started, in which case the caller
 * should either try again later, or revert to PIO for the current request.
 */
typedef enum {	ide_dma_read,	ide_dma_write,		ide_dma_begin,
		ide_dma_end,	ide_dma_check,		ide_dma_on,
		ide_dma_off,	ide_dma_off_quietly,	ide_dma_test_irq,
		ide_dma_host_on,			ide_dma_host_off,
		ide_dma_bad_drive,			ide_dma_good_drive,
		ide_dma_verbose,			ide_dma_retune,
		ide_dma_lostirq,			ide_dma_timeout
} ide_dma_action_t;

typedef int (ide_dmaproc_t)(ide_dma_action_t, ide_drive_t *);

/*
 * An ide_ideproc_t() performs CPU-polled transfers to/from a drive.
 * Arguments are: the drive, the buffer pointer, and the length (in bytes or
 * words depending on if it's an IDE or ATAPI call).
 *
 * If it is not defined for a controller, standard-code is used from ide.c.
 *
 * Controllers which are not memory-mapped in the standard way need to 
 * override that mechanism using this function to work.
 *
 */
typedef enum { ideproc_ide_input_data,    ideproc_ide_output_data,
	       ideproc_atapi_input_bytes, ideproc_atapi_output_bytes
} ide_ide_action_t;

typedef void (ide_ideproc_t)(ide_ide_action_t, ide_drive_t *, void *, unsigned int);

/*
 * mapping stuff, prepare for highmem...
 * 
 * temporarily mapping a (possible) highmem bio for PIO transfer
 */
#define ide_rq_offset(rq) \
	(((rq)->hard_cur_sectors - (rq)->current_nr_sectors) << 9)

/*
 * taskfiles really should use hard_cur_sectors as well!
 */
#define task_rq_offset(rq) \
	(((rq)->nr_sectors - (rq)->current_nr_sectors) * SECTOR_SIZE)

extern inline void *ide_map_buffer(struct request *rq, unsigned long *flags)
{
	/*
	 * fs request
	 */
	if (rq->bio)
		return bio_kmap_irq(rq->bio, flags) + ide_rq_offset(rq);

	/*
	 * task request
	 */
	return rq->buffer + task_rq_offset(rq);
}

extern inline void ide_unmap_buffer(char *buffer, unsigned long *flags)
{
	bio_kunmap_irq(buffer, flags);
}

/*
 * A Verbose noise maker for debugging on the attempted transfer rates.
 */
extern inline char *ide_xfer_verbose (byte xfer_rate)
{
	switch(xfer_rate) {
		case XFER_UDMA_7:	return("UDMA 7");
		case XFER_UDMA_6:	return("UDMA 6");
		case XFER_UDMA_5:	return("UDMA 5");
		case XFER_UDMA_4:	return("UDMA 4");
		case XFER_UDMA_3:	return("UDMA 3");
		case XFER_UDMA_2:	return("UDMA 2");
		case XFER_UDMA_1:	return("UDMA 1");
		case XFER_UDMA_0:	return("UDMA 0");
		case XFER_MW_DMA_2:	return("MW DMA 2");
		case XFER_MW_DMA_1:	return("MW DMA 1");
		case XFER_MW_DMA_0:	return("MW DMA 0");
		case XFER_SW_DMA_2:	return("SW DMA 2");
		case XFER_SW_DMA_1:	return("SW DMA 1");
		case XFER_SW_DMA_0:	return("SW DMA 0");
		case XFER_PIO_4:	return("PIO 4");
		case XFER_PIO_3:	return("PIO 3");
		case XFER_PIO_2:	return("PIO 2");
		case XFER_PIO_1:	return("PIO 1");
		case XFER_PIO_0:	return("PIO 0");
		case XFER_PIO_SLOW:	return("PIO SLOW");
		default:		return("XFER ERROR");
	}
}

/*
 * A Verbose noise maker for debugging on the attempted dmaing calls.
 */
extern inline char *ide_dmafunc_verbose (ide_dma_action_t dmafunc)
{
	switch (dmafunc) {
		case ide_dma_read:		return("ide_dma_read");
		case ide_dma_write:		return("ide_dma_write");
		case ide_dma_begin:		return("ide_dma_begin");
		case ide_dma_end:		return("ide_dma_end:");
		case ide_dma_check:		return("ide_dma_check");
		case ide_dma_on:		return("ide_dma_on");
		case ide_dma_off:		return("ide_dma_off");
		case ide_dma_off_quietly:	return("ide_dma_off_quietly");
		case ide_dma_test_irq:		return("ide_dma_test_irq");
		case ide_dma_host_on:		return("ide_dma_host_on");
		case ide_dma_host_off:		return("ide_dma_host_off");
		case ide_dma_bad_drive:		return("ide_dma_bad_drive");
		case ide_dma_good_drive:	return("ide_dma_good_drive");
		case ide_dma_verbose:		return("ide_dma_verbose");
		case ide_dma_retune:		return("ide_dma_retune");
		case ide_dma_lostirq:		return("ide_dma_lostirq");
		case ide_dma_timeout:		return("ide_dma_timeout");
		default:			return("unknown");
	}
}

/*
 * An ide_tuneproc_t() is used to set the speed of an IDE interface
 * to a particular PIO mode.  The "byte" parameter is used
 * to select the PIO mode by number (0,1,2,3,4,5), and a value of 255
 * indicates that the interface driver should "auto-tune" the PIO mode
 * according to the drive capabilities in drive->id;
 *
 * Not all interface types support tuning, and not all of those
 * support all possible PIO settings.  They may silently ignore
 * or round values as they see fit.
 */
typedef void (ide_tuneproc_t) (ide_drive_t *, byte);
typedef int (ide_speedproc_t) (ide_drive_t *, byte);

/*
 * This is used to provide support for strange interfaces
 */
typedef void (ide_selectproc_t) (ide_drive_t *);
typedef void (ide_resetproc_t) (ide_drive_t *);
typedef int (ide_quirkproc_t) (ide_drive_t *);
typedef void (ide_intrproc_t) (ide_drive_t *);
typedef void (ide_maskproc_t) (ide_drive_t *, int);
typedef void (ide_rw_proc_t) (ide_drive_t *, ide_dma_action_t);

/*
 * ide soft-power support
 */
typedef int (ide_busproc_t) (ide_drive_t *, int);

#define IDE_CHIPSET_PCI_MASK	\
    ((1<<ide_pci)|(1<<ide_cmd646)|(1<<ide_ali14xx))
#define IDE_CHIPSET_IS_PCI(c)	((IDE_CHIPSET_PCI_MASK >> (c)) & 1)

#ifdef CONFIG_BLK_DEV_IDEPCI
typedef struct ide_pci_devid_s {
	unsigned short	vid;
	unsigned short	did;
} ide_pci_devid_t;

#define IDE_PCI_DEVID_NULL	((ide_pci_devid_t){0,0})
#define IDE_PCI_DEVID_EQ(a,b)	(a.vid == b.vid && a.did == b.did)
#endif /* CONFIG_BLK_DEV_IDEPCI */

typedef struct hwif_s {
	struct hwif_s	*next;		/* for linked-list in ide_hwgroup_t */
	struct hwgroup_s *hwgroup;	/* actually (ide_hwgroup_t *) */
	ide_ioreg_t	io_ports[IDE_NR_PORTS];	/* task file registers */
/*
 *     FIXME!! need a generic register set :-/  PPC guys ideas??
 *
 *     ide_mmioreg_t   mm_ports[IDE_NR_PORTS]; "task file registers"
 *
 */
	hw_regs_t	hw;		/* Hardware info */
	ide_drive_t	drives[MAX_DRIVES];	/* drive info */
	struct gendisk	*gd[MAX_DRIVES];/* gendisk structure */
	int		addressing;	/* hosts addressing */
	void		(*tuneproc)(ide_drive_t *, byte);	/* routine to tune PIO mode for drives */
	int		(*speedproc)(ide_drive_t *, byte);	/* routine to retune DMA modes for drives */
	void		(*selectproc)(ide_drive_t *);		/* tweaks hardware to select drive */
	void		(*resetproc)(ide_drive_t *);		/* routine to reset controller after a disk reset */
	void		(*intrproc)(ide_drive_t *);		/* special interrupt handling for shared pci interrupts */
	void		(*maskproc)(ide_drive_t *, int);	/* special host masking for drive selection */
	int		(*quirkproc)(ide_drive_t *);		/* check host's drive quirk list */
	void		(*rwproc)(ide_drive_t *, ide_dma_action_t);	/* adjust timing based upon rq->cmd direction */
	void		(*ideproc)(ide_ide_action_t, ide_drive_t *, void *, unsigned int);	/* CPU-polled transfer routine */
	int		(*dmaproc)(ide_dma_action_t, ide_drive_t *);	/* dma read/write/abort routine */
	int		(*busproc)(ide_drive_t *, int);		/* driver soft-power interface */
	unsigned int	*dmatable_cpu;	/* dma physical region descriptor table (cpu view) */
	dma_addr_t	dmatable_dma;	/* dma physical region descriptor table (dma view) */
	struct scatterlist *sg_table;	/* Scatter-gather list used to build the above */
	int sg_nents;			/* Current number of entries in it */
	int sg_dma_direction;		/* dma transfer direction */
	int sg_dma_active;		/* is it in use */
	struct hwif_s	*mate;		/* other hwif from same PCI chip */
	unsigned long	dma_base;	/* base addr for dma ports */
	unsigned	dma_extra;	/* extra addr for dma ports */
	unsigned long	config_data;	/* for use by chipset-specific code */
	unsigned long	select_data;	/* for use by chipset-specific code */
	struct proc_dir_entry *proc;	/* /proc/ide/ directory entry */
	int		irq;		/* our irq number */
	byte		major;		/* our major number */
	char 		name[6];	/* name of interface, eg. "ide0" */
	byte		index;		/* 0 for ide0; 1 for ide1; ... */
	hwif_chipset_t	chipset;	/* sub-module for tuning.. */
	unsigned	noprobe    : 1;	/* don't probe for this interface */
	unsigned	present    : 1;	/* this interface exists */
	unsigned	serialized : 1;	/* serialized operation with mate hwif */
	unsigned	sharing_irq: 1;	/* 1 = sharing irq with another hwif */
	unsigned	reset      : 1;	/* reset after probe */
	unsigned	autodma    : 1;	/* automatically try to enable DMA at boot */
	unsigned	udma_four  : 1;	/* 1=ATA-66 capable, 0=default */
	unsigned	no_highmem : 1;	/* always use high i/o bounce */
	byte		channel;	/* for dual-port chips: 0=primary, 1=secondary */
#ifdef CONFIG_BLK_DEV_IDEPCI
	struct pci_dev	*pci_dev;	/* for pci chipsets */
	ide_pci_devid_t	pci_devid;	/* for pci chipsets: {VID,DID} */
#endif /* CONFIG_BLK_DEV_IDEPCI */
#if (DISK_RECOVERY_TIME > 0)
	unsigned long	last_time;	/* time when previous rq was done */
#endif
	byte		straight8;	/* Alan's straight 8 check */
	void		*hwif_data;	/* extra hwif data */
	byte		bus_state;	/* power state of the IDE bus */
} ide_hwif_t;

/*
 * Status returned from various ide_ functions
 */
typedef enum {
	ide_stopped,	/* no drive operation was started */
	ide_started	/* a drive operation was started, and a handler was set */
} ide_startstop_t;

/*
 *  internal ide interrupt handler type
 */
typedef ide_startstop_t (ide_pre_handler_t)(ide_drive_t *, struct request *);
typedef ide_startstop_t (ide_handler_t)(ide_drive_t *);
typedef ide_startstop_t (ide_post_handler_t)(ide_drive_t *);

/*
 * when ide_timer_expiry fires, invoke a handler of this type
 * to decide what to do.
 */
typedef int (ide_expiry_t)(ide_drive_t *);

typedef struct hwgroup_s {
	ide_handler_t		*handler;/* irq handler, if active */
	ide_handler_t		*handler_save;/* irq handler, if active */
	volatile int		busy;	/* BOOL: protects all fields below */
	int			sleeping; /* BOOL: wake us up on timer expiry */
	ide_drive_t		*drive;	/* current drive */
	ide_hwif_t		*hwif;	/* ptr to current hwif in linked-list */
	struct request		*rq;	/* current request */
	struct timer_list	timer;	/* failsafe timer */
	struct request		wrq;	/* local copy of current write rq */
	unsigned long		poll_timeout;	/* timeout value during long polls */
	ide_expiry_t		*expiry;	/* queried upon timeouts */
	int			pio_clock;	/* ide_system_bus_speed */
} ide_hwgroup_t;

/* structure attached to the request for IDE_TASK_CMDS */

/*
 * configurable drive settings
 */

#define TYPE_INT	0
#define TYPE_INTA	1
#define TYPE_BYTE	2
#define TYPE_SHORT	3

#define SETTING_READ	(1 << 0)
#define SETTING_WRITE	(1 << 1)
#define SETTING_RW	(SETTING_READ | SETTING_WRITE)

typedef int (ide_procset_t)(ide_drive_t *, int);
typedef struct ide_settings_s {
	char			*name;
	int			rw;
	int			read_ioctl;
	int			write_ioctl;
	int			data_type;
	int			min;
	int			max;
	int			mul_factor;
	int			div_factor;
	void			*data;
	ide_procset_t		*set;
	int			auto_remove;
	struct ide_settings_s	*next;
} ide_settings_t;

void ide_add_setting(ide_drive_t *drive, const char *name, int rw, int read_ioctl, int write_ioctl, int data_type, int min, int max, int mul_factor, int div_factor, void *data, ide_procset_t *set);
void ide_remove_setting(ide_drive_t *drive, char *name);
ide_settings_t *ide_find_setting_by_name(ide_drive_t *drive, char *name);
int ide_read_setting(ide_drive_t *t, ide_settings_t *setting);
int ide_write_setting(ide_drive_t *drive, ide_settings_t *setting, int val);
void ide_add_generic_settings(ide_drive_t *drive);

/*
 * /proc/ide interface
 */
typedef struct {
	const char	*name;
	mode_t		mode;
	read_proc_t	*read_proc;
	write_proc_t	*write_proc;
} ide_proc_entry_t;

#ifdef CONFIG_PROC_FS
void proc_ide_create(void);
void proc_ide_destroy(void);
void recreate_proc_ide_device(ide_hwif_t *, ide_drive_t *);
void destroy_proc_ide_device(ide_hwif_t *, ide_drive_t *);
void destroy_proc_ide_drives(ide_hwif_t *);
void create_proc_ide_interfaces(void);
void ide_add_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p, void *data);
void ide_remove_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p);
read_proc_t proc_ide_read_capacity;
read_proc_t proc_ide_read_geometry;

/*
 * Standard exit stuff:
 */
#define PROC_IDE_READ_RETURN(page,start,off,count,eof,len) \
{					\
	len -= off;			\
	if (len < count) {		\
		*eof = 1;		\
		if (len <= 0)		\
			return 0;	\
	} else				\
		len = count;		\
	*start = page + off;		\
	return len;			\
}
#else
#define PROC_IDE_READ_RETURN(page,start,off,count,eof,len) return 0;
#endif

/*
 * Subdrivers support.
 */
#define IDE_SUBDRIVER_VERSION	1

typedef struct ide_driver_s {
	struct module			*owner;
	const char			*name;
	const char			*version;
	byte				media;
	unsigned busy			: 1;
	unsigned supports_dma		: 1;
	unsigned supports_dsc_overlap	: 1;
	int		(*cleanup)(ide_drive_t *);
	int		(*standby)(ide_drive_t *);
	int		(*suspend)(ide_drive_t *);
	int		(*resume)(ide_drive_t *);
	int		(*flushcache)(ide_drive_t *);
	ide_startstop_t	(*do_request)(ide_drive_t *, struct request *, unsigned long);
	int		(*end_request)(ide_drive_t *, int);
	byte		(*sense)(ide_drive_t *, const char *, byte);
	ide_startstop_t (*error)(ide_drive_t *, const char *, byte);
	int		(*ioctl)(ide_drive_t *, struct inode *, struct file *, unsigned int, unsigned long);
	int		(*open)(struct inode *, struct file *, ide_drive_t *);
	void		(*release)(struct inode *, struct file *, ide_drive_t *);
	int		(*media_change)(ide_drive_t *);
	void		(*revalidate)(ide_drive_t *);
	void		(*pre_reset)(ide_drive_t *);
	unsigned long	(*capacity)(ide_drive_t *);
	ide_startstop_t	(*special)(ide_drive_t *);
	ide_proc_entry_t        *proc;
	int		(*init)(void);
	int		(*reinit)(ide_drive_t *);
	void		(*ata_prebuilder)(ide_drive_t *);
	void		(*atapi_prebuilder)(ide_drive_t *);
	struct list_head drives;
} ide_driver_t;

#define DRIVER(drive)		((drive)->driver)

/*
 * IDE modules.
 */
#define IDE_CHIPSET_MODULE		0	/* not supported yet */
#define IDE_PROBE_MODULE		1
#define IDE_DRIVER_MODULE		2

typedef int	(ide_module_init_proc)(void);

typedef struct ide_module_s {
	int				type;
	ide_module_init_proc		*init;
	void				*info;
	struct ide_module_s		*next;
} ide_module_t;

/*
 * ide_hwifs[] is the master data structure used to keep track
 * of just about everything in ide.c.  Whenever possible, routines
 * should be using pointers to a drive (ide_drive_t *) or
 * pointers to a hwif (ide_hwif_t *), rather than indexing this
 * structure directly (the allocation/layout may change!).
 *
 */
#ifndef _IDE_C
extern	ide_hwif_t	ide_hwifs[];		/* master data repository */
extern	ide_module_t	*ide_modules;
extern	ide_module_t	*ide_probe;
#endif
extern int noautodma;

/*
 * We need blk.h, but we replace its end_request by our own version.
 */
#define IDE_DRIVER		/* Toggle some magic bits in blk.h */
#define LOCAL_END_REQUEST	/* Don't generate end_request in blk.h */
#define DEVICE_NR(device)	(minor(device) >> PARTN_BITS)
#include <linux/blk.h>

int ide_end_request (ide_drive_t *drive, int uptodate);

/*
 * This is used on exit from the driver, to designate the next irq handler
 * and also to start the safety timer.
 */
void ide_set_handler (ide_drive_t *drive, ide_handler_t *handler, unsigned int timeout, ide_expiry_t *expiry);

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
byte ide_dump_status (ide_drive_t *drive, const char *msg, byte stat);

/*
 * ide_error() takes action based on the error returned by the controller.
 * The caller should return immediately after invoking this.
 */
ide_startstop_t ide_error (ide_drive_t *drive, const char *msg, byte stat);

/*
 * Issue a simple drive command
 * The drive must be selected beforehand.
 */
void ide_cmd (ide_drive_t *drive, byte cmd, byte nsect, ide_handler_t *handler);

/*
 * ide_fixstring() cleans up and (optionally) byte-swaps a text string,
 * removing leading/trailing blanks and compressing internal blanks.
 * It is primarily used to tidy up the model name/number fields as
 * returned by the WIN_[P]IDENTIFY commands.
 */
void ide_fixstring (byte *s, const int bytecount, const int byteswap);

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return 1 after doing "*startstop = ide_error()", and the
 * caller should return the updated value of "startstop" in this case.
 * "startstop" is unchanged when the function returns 0;
 */
int ide_wait_stat (ide_startstop_t *startstop, ide_drive_t *drive, byte good, byte bad, unsigned long timeout);

/*
 * This routine is called from the partition-table code in genhd.c
 * to "convert" a drive to a logical geometry with fewer than 1024 cyls.
 */
int ide_xlate_1024 (kdev_t, int, int, const char *);

/*
 * Convert kdev_t structure into ide_drive_t * one.
 */
ide_drive_t *get_info_ptr (kdev_t i_rdev);

/*
 * Return the current idea about the total capacity of this drive.
 */
unsigned long current_capacity (ide_drive_t *drive);

void ide_revalidate_drive (ide_drive_t *drive);

/*
 * Start a reset operation for an IDE interface.
 * The caller should return immediately after invoking this.
 */
ide_startstop_t ide_do_reset (ide_drive_t *);

/*
 * Re-Start an operation for an IDE interface.
 * The caller should return immediately after invoking this.
 */
int restart_request (ide_drive_t *, struct request *);

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
void ide_init_drive_cmd (struct request *rq);

/*
 * "action" parameter type for ide_do_drive_cmd() below.
 */
typedef enum {
	ide_wait,	/* insert rq at end of list, and wait for it */
	ide_next,	/* insert rq immediately after current request */
	ide_preempt,	/* insert rq in front of current request */
	ide_end		/* insert rq at end of list, but don't wait for it */
} ide_action_t;

/*
 * This function issues a special IDE device request
 * onto the request queue.
 *
 * If action is ide_wait, then the rq is queued at the end of the
 * request queue, and the function sleeps until it has been processed.
 * This is for use when invoked from an ioctl handler.
 *
 * If action is ide_preempt, then the rq is queued at the head of
 * the request queue, displacing the currently-being-processed
 * request and this function returns immediately without waiting
 * for the new rq to be completed.  This is VERY DANGEROUS, and is
 * intended for careful use by the ATAPI tape/cdrom driver code.
 *
 * If action is ide_next, then the rq is queued immediately after
 * the currently-being-processed-request (if any), and the function
 * returns without waiting for the new rq to be completed.  As above,
 * This is VERY DANGEROUS, and is intended for careful use by the
 * ATAPI tape/cdrom driver code.
 *
 * If action is ide_end, then the rq is queued at the end of the
 * request queue, and the function returns immediately without waiting
 * for the new rq to be completed. This is again intended for careful
 * use by the ATAPI tape/cdrom driver code.
 */
int ide_do_drive_cmd (ide_drive_t *drive, struct request *rq, ide_action_t action);

/*
 * Clean up after success/failure of an explicit drive cmd.
 * stat/err are used only when (HWGROUP(drive)->rq->cmd == IDE_DRIVE_CMD).
 * stat/err are used only when (HWGROUP(drive)->rq->cmd == IDE_DRIVE_TASK_MASK).
 */
void ide_end_drive_cmd (ide_drive_t *drive, byte stat, byte err);

/*
 * Issue ATA command and wait for completion. use for implementing commands in kernel
 */
int ide_wait_cmd (ide_drive_t *drive, int cmd, int nsect, int feature, int sectors, byte *buf);

int ide_wait_cmd_task (ide_drive_t *drive, byte *buf);
 
typedef struct ide_task_s {
	task_ioreg_t		tfRegister[8];
	task_ioreg_t		hobRegister[8];
	ide_reg_valid_t		tf_out_flags;
	ide_reg_valid_t		tf_in_flags;
	int			data_phase;
	int			command_type;
	ide_pre_handler_t	*prehandler;
	ide_handler_t		*handler;
	ide_post_handler_t	*posthandler;
	struct request		*rq;		/* copy of request */
	void			*special;	/* valid_t generally */
} ide_task_t;

typedef struct pkt_task_s {
	task_ioreg_t		tfRegister[8];
	int			data_phase;
	int			command_type;
	ide_handler_t		*handler;
	struct request		*rq;		/* copy of request */
	void			*special;
} pkt_task_t;

void ata_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount);
void ata_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount);
void atapi_input_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount);
void atapi_output_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount);
void taskfile_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount);
void taskfile_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount);

int drive_is_ready (ide_drive_t *drive);
int wait_for_ready (ide_drive_t *drive, int timeout);

/*
 * taskfile io for disks for now...and builds request from ide_ioctl
 */
ide_startstop_t do_rw_taskfile (ide_drive_t *drive, ide_task_t *task);

void ide_end_taskfile (ide_drive_t *drive, byte stat, byte err);

/*
 * Special Flagged Register Validation Caller
 */
ide_startstop_t flagged_taskfile (ide_drive_t *drive, ide_task_t *task);

ide_startstop_t set_multmode_intr (ide_drive_t *drive);
ide_startstop_t set_geometry_intr (ide_drive_t *drive);
ide_startstop_t recal_intr (ide_drive_t *drive);
ide_startstop_t task_no_data_intr (ide_drive_t *drive);
ide_startstop_t task_in_intr (ide_drive_t *drive);
ide_startstop_t task_mulin_intr (ide_drive_t *drive);
ide_startstop_t pre_task_out_intr (ide_drive_t *drive, struct request *rq);
ide_startstop_t task_out_intr (ide_drive_t *drive);
ide_startstop_t pre_task_mulout_intr (ide_drive_t *drive, struct request *rq);
ide_startstop_t task_mulout_intr (ide_drive_t *drive);
void ide_init_drive_taskfile (struct request *rq);

int ide_raw_taskfile (ide_drive_t *drive, ide_task_t *cmd, byte *buf);

ide_pre_handler_t * ide_pre_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile);
ide_handler_t * ide_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile);
ide_post_handler_t * ide_post_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile);
/* Expects args is a full set of TF registers and parses the command type */
int ide_cmd_type_parser (ide_task_t *args);

int ide_taskfile_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
int ide_cmd_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
int ide_task_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);

#ifdef CONFIG_PKT_TASK_IOCTL
int pkt_taskfile_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
#endif /* CONFIG_PKT_TASK_IOCTL */

void ide_delay_50ms (void);
int system_bus_clock(void);

byte ide_auto_reduce_xfer (ide_drive_t *drive);
int ide_driveid_update (ide_drive_t *drive);
int ide_ata66_check (ide_drive_t *drive, ide_task_t *args);
int ide_config_drive_speed (ide_drive_t *drive, byte speed);
byte eighty_ninty_three (ide_drive_t *drive);
int set_transfer (ide_drive_t *drive, ide_task_t *args);
int taskfile_lib_get_identify (ide_drive_t *drive, byte *buf);

/*
 * ide_system_bus_speed() returns what we think is the system VESA/PCI
 * bus speed (in MHz).  This is used for calculating interface PIO timings.
 * The default is 40 for known PCI systems, 50 otherwise.
 * The "idebus=xx" parameter can be used to override this value.
 */
int ide_system_bus_speed (void);

/*
 * ide_stall_queue() can be used by a drive to give excess bandwidth back
 * to the hwgroup by sleeping for timeout jiffies.
 */
void ide_stall_queue (ide_drive_t *drive, unsigned long timeout);

/*
 * ide_get_queue() returns the queue which corresponds to a given device.
 */
request_queue_t *ide_get_queue (kdev_t dev);

/*
 * CompactFlash cards and their brethern pretend to be removable hard disks,
 * but they never have a slave unit, and they don't have doorlock mechanisms.
 * This test catches them, and is invoked elsewhere when setting appropriate config bits.
 */
int drive_is_flashcard (ide_drive_t *drive);

int ide_spin_wait_hwgroup (ide_drive_t *drive);
void ide_timer_expiry (unsigned long data);
void ide_intr (int irq, void *dev_id, struct pt_regs *regs);
void do_ide_request (request_queue_t * q);
void ide_init_subdrivers (void);

#ifndef _IDE_C
extern struct block_device_operations ide_fops[];
extern ide_proc_entry_t generic_subdriver_entries[];
#endif

int ata_attach(ide_drive_t *drive);

#ifdef _IDE_C
#ifdef CONFIG_BLK_DEV_IDE
int ideprobe_init (void);
#endif /* CONFIG_BLK_DEV_IDE */
#endif /* _IDE_C */

int ide_register_module (ide_module_t *module);
void ide_unregister_module (ide_module_t *module);
int ide_register_subdriver (ide_drive_t *drive, ide_driver_t *driver, int version);
int ide_unregister_subdriver (ide_drive_t *drive);
int ide_replace_subdriver(ide_drive_t *drive, const char *driver);

#ifdef CONFIG_BLK_DEV_IDEPCI
#define ON_BOARD		1
#define NEVER_BOARD		0
#ifdef CONFIG_BLK_DEV_OFFBOARD
#  define OFF_BOARD		ON_BOARD
#else /* CONFIG_BLK_DEV_OFFBOARD */
#  define OFF_BOARD		NEVER_BOARD
#endif /* CONFIG_BLK_DEV_OFFBOARD */


typedef struct ide_pci_enablebit_s {
	byte	reg;	/* byte pci reg holding the enable-bit */
	byte	mask;	/* mask to isolate the enable-bit */
	byte	val;	/* value of masked reg when "enabled" */
} ide_pci_enablebit_t;

typedef struct ide_pci_device_s {
	ide_pci_devid_t		devid;
	char			*name;
	void			(*fixup_device)(struct pci_dev *, struct ide_pci_device_s *);
	unsigned int		(*init_chipset)(struct pci_dev *, const char *);
	unsigned int		(*ata66_check)(ide_hwif_t *);
	void			(*init_hwif)(ide_hwif_t *);
	void			(*dma_init)(ide_hwif_t *, unsigned long);
	ide_pci_enablebit_t	enablebits[2];
	byte			bootable;
	unsigned int		extra;
} ide_pci_device_t;

#ifdef LINUX_PCI_H
extern inline void ide_register_xp_fix(struct pci_dev *dev)
{
	int i;
	unsigned short cmd;
	unsigned long flags;
	unsigned long base_address[4] = { 0x1f0, 0x3f4, 0x170, 0x374 };

	local_irq_save(flags);
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	pci_write_config_word(dev, PCI_COMMAND, cmd & ~PCI_COMMAND_IO);
	for (i=0; i<4; i++) {
		dev->resource[i].start = 0;
		dev->resource[i].end = 0;
		dev->resource[i].flags = 0;
	}
	for (i=0; i<4; i++) {
		dev->resource[i].start = base_address[i];
		dev->resource[i].flags |= PCI_BASE_ADDRESS_SPACE_IO;
		pci_write_config_dword(dev,
			(PCI_BASE_ADDRESS_0 + (i * 4)),
			dev->resource[i].start);
	}
	pci_write_config_word(dev, PCI_COMMAND, cmd);
	local_irq_restore(flags);
}

void ide_setup_pci_device(struct pci_dev *dev, ide_pci_device_t *d) __init;
#endif /* LINUX_PCI_H */

unsigned long ide_find_free_region (unsigned short size) __init;
void ide_scan_pcibus (int scan_direction) __init;
#endif
#ifdef CONFIG_BLK_DEV_IDEDMA
#define BAD_DMA_DRIVE		0
#define GOOD_DMA_DRIVE		1
int ide_build_dmatable (ide_drive_t *drive, ide_dma_action_t func);
void ide_destroy_dmatable (ide_drive_t *drive);
ide_startstop_t ide_dma_intr (ide_drive_t *drive);
int check_drive_lists (ide_drive_t *drive, int good_bad);
int report_drive_dmaing (ide_drive_t *drive);
int ide_dmaproc (ide_dma_action_t func, ide_drive_t *drive);
int ide_release_dma (ide_hwif_t *hwif);
void ide_setup_dma (ide_hwif_t *hwif, unsigned long dmabase, unsigned int num_ports) __init;
unsigned long ide_get_or_set_dma_base (ide_hwif_t *hwif, int extra, const char *name) __init;
#endif /* CONFIG_BLK_DEV_IDEPCI */

void hwif_unregister (ide_hwif_t *hwif);

void export_ide_init_queue (ide_drive_t *drive);
byte export_probe_for_drive (ide_drive_t *drive);

extern spinlock_t ide_lock;

#define local_irq_set(flags)	do { local_save_flags((flags)); local_irq_enable(); } while (0)

#endif /* _IDE_H */

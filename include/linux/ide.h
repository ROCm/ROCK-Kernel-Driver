#ifndef _IDE_H
#define _IDE_H

/*
 *  Copyright (C) 1994-2002  Linus Torvalds & authors
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/ioport.h>
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


/* ATA/ATAPI Commands pre T13 Spec */
#define WIN_NOP				0x00
#define CFA_REQ_EXT_ERROR_CODE		0x03 /* CFA Request Extended Error Code */
#define WIN_SRST			0x08 /* ATAPI soft reset command */
#define WIN_DEVICE_RESET		0x08
#define WIN_RESTORE			0x10
#define WIN_READ			0x20 /* 28-Bit */
#define WIN_READ_EXT			0x24 /* 48-Bit */
#define WIN_READDMA_EXT			0x25 /* 48-Bit */
#define WIN_READDMA_QUEUED_EXT		0x26 /* 48-Bit */
#define WIN_READ_NATIVE_MAX_EXT		0x27 /* 48-Bit */
#define WIN_MULTREAD_EXT		0x29 /* 48-Bit */
#define WIN_WRITE			0x30 /* 28-Bit */
#define WIN_WRITE_EXT			0x34 /* 48-Bit */
#define WIN_WRITEDMA_EXT		0x35 /* 48-Bit */
#define WIN_WRITEDMA_QUEUED_EXT		0x36 /* 48-Bit */
#define WIN_SET_MAX_EXT			0x37 /* 48-Bit */
#define CFA_WRITE_SECT_WO_ERASE		0x38 /* CFA Write Sectors without erase */
#define WIN_MULTWRITE_EXT		0x39 /* 48-Bit */
#define WIN_WRITE_VERIFY		0x3C /* 28-Bit */
#define WIN_VERIFY			0x40 /* 28-Bit - Read Verify Sectors */
#define WIN_VERIFY_EXT			0x42 /* 48-Bit */
#define WIN_FORMAT			0x50
#define WIN_INIT			0x60
#define WIN_SEEK			0x70
#define CFA_TRANSLATE_SECTOR		0x87 /* CFA Translate Sector */
#define WIN_DIAGNOSE			0x90
#define WIN_SPECIFY			0x91 /* set drive geometry translation */
#define WIN_DOWNLOAD_MICROCODE		0x92
#define WIN_STANDBYNOW2			0x94
#define WIN_SETIDLE2			0x97
#define WIN_CHECKPOWERMODE2		0x98
#define WIN_SLEEPNOW2			0x99
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* identify ATAPI device	*/
#define WIN_QUEUED_SERVICE		0xA2
#define WIN_SMART			0xB0 /* self-monitoring and reporting */
#define CFA_ERASE_SECTORS		0xC0
#define WIN_MULTREAD			0xC4 /* read sectors using multiple mode*/
#define WIN_MULTWRITE			0xC5 /* write sectors using multiple mode */
#define WIN_SETMULT			0xC6 /* enable/disable multiple mode */
#define WIN_READDMA_QUEUED		0xC7 /* read sectors using Queued DMA transfers */
#define WIN_READDMA			0xC8 /* read sectors using DMA transfers */
#define WIN_WRITEDMA			0xCA /* write sectors using DMA transfers */
#define WIN_WRITEDMA_QUEUED		0xCC /* write sectors using Queued DMA transfers */
#define CFA_WRITE_MULTI_WO_ERASE	0xCD /* CFA Write multiple without erase */
#define WIN_GETMEDIASTATUS		0xDA
#define WIN_DOORLOCK			0xDE /* lock door on removable drives */
#define WIN_DOORUNLOCK			0xDF /* unlock door on removable drives */
#define WIN_STANDBYNOW1			0xE0
#define WIN_IDLEIMMEDIATE		0xE1 /* force drive to become "ready" */
#define WIN_STANDBY			0xE2 /* Set device in Standby Mode */
#define WIN_SETIDLE1			0xE3
#define WIN_READ_BUFFER			0xE4 /* force read only 1 sector */
#define WIN_CHECKPOWERMODE1		0xE5
#define WIN_SLEEPNOW1			0xE6
#define WIN_FLUSH_CACHE			0xE7
#define WIN_WRITE_BUFFER		0xE8 /* force write only 1 sector */
#define WIN_FLUSH_CACHE_EXT		0xEA /* 48-Bit */
#define WIN_IDENTIFY			0xEC /* ask drive to identify itself	*/
#define WIN_MEDIAEJECT			0xED
#define WIN_IDENTIFY_DMA		0xEE /* same as WIN_IDENTIFY, but DMA */
#define WIN_SETFEATURES			0xEF /* set special drive features */
#define EXABYTE_ENABLE_NEST		0xF0
#define WIN_SECURITY_SET_PASS		0xF1
#define WIN_SECURITY_UNLOCK		0xF2
#define WIN_SECURITY_ERASE_PREPARE	0xF3
#define WIN_SECURITY_ERASE_UNIT		0xF4
#define WIN_SECURITY_FREEZE_LOCK	0xF5
#define WIN_SECURITY_DISABLE		0xF6
#define WIN_READ_NATIVE_MAX		0xF8 /* return the native maximum address */
#define WIN_SET_MAX			0xF9
#define DISABLE_SEAGATE			0xFB

/* WIN_SMART sub-commands */

#define SMART_READ_VALUES		0xD0
#define SMART_READ_THRESHOLDS		0xD1
#define SMART_AUTOSAVE			0xD2
#define SMART_SAVE			0xD3
#define SMART_IMMEDIATE_OFFLINE		0xD4
#define SMART_READ_LOG_SECTOR		0xD5
#define SMART_WRITE_LOG_SECTOR		0xD6
#define SMART_WRITE_THRESHOLDS		0xD7
#define SMART_ENABLE			0xD8
#define SMART_DISABLE			0xD9
#define SMART_STATUS			0xDA
#define SMART_AUTO_OFFLINE		0xDB

/* Password used in TF4 & TF5 executing SMART commands */

#define SMART_LCYL_PASS			0x4F
#define SMART_HCYL_PASS			0xC2

/* WIN_SETFEATURES sub-commands */

#define SETFEATURES_EN_WCACHE	0x02	/* Enable write cache */

#define SETFEATURES_XFER	0x03	/* Set transfer mode */
#	define XFER_UDMA_7	0x47	/* 0100|0111 */
#	define XFER_UDMA_6	0x46	/* 0100|0110 */
#	define XFER_UDMA_5	0x45	/* 0100|0101 */
#	define XFER_UDMA_4	0x44	/* 0100|0100 */
#	define XFER_UDMA_3	0x43	/* 0100|0011 */
#	define XFER_UDMA_2	0x42	/* 0100|0010 */
#	define XFER_UDMA_1	0x41	/* 0100|0001 */
#	define XFER_UDMA_0	0x40	/* 0100|0000 */
#	define XFER_MW_DMA_2	0x22	/* 0010|0010 */
#	define XFER_MW_DMA_1	0x21	/* 0010|0001 */
#	define XFER_MW_DMA_0	0x20	/* 0010|0000 */
#	define XFER_SW_DMA_2	0x12	/* 0001|0010 */
#	define XFER_SW_DMA_1	0x11	/* 0001|0001 */
#	define XFER_SW_DMA_0	0x10	/* 0001|0000 */
#	define XFER_PIO_4	0x0C	/* 0000|1100 */
#	define XFER_PIO_3	0x0B	/* 0000|1011 */
#	define XFER_PIO_2	0x0A	/* 0000|1010 */
#	define XFER_PIO_1	0x09	/* 0000|1001 */
#	define XFER_PIO_0	0x08	/* 0000|1000 */
#	define XFER_PIO_SLOW	0x00	/* 0000|0000 */

#define SETFEATURES_DIS_DEFECT	0x04	/* Disable Defect Management */
#define SETFEATURES_EN_APM	0x05	/* Enable advanced power management */
#define SETFEATURES_DIS_MSN	0x31	/* Disable Media Status Notification */
#define SETFEATURES_EN_AAM	0x42	/* Enable Automatic Acoustic Management */
#define SETFEATURES_DIS_RLA	0x55	/* Disable read look-ahead feature */
#define SETFEATURES_EN_RI	0x5D	/* Enable release interrupt */
#define SETFEATURES_EN_SI	0x5E	/* Enable SERVICE interrupt */
#define SETFEATURES_DIS_RPOD	0x66	/* Disable reverting to power on defaults */
#define SETFEATURES_DIS_WCACHE	0x82	/* Disable write cache */
#define SETFEATURES_EN_DEFECT	0x84	/* Enable Defect Management */
#define SETFEATURES_DIS_APM	0x85	/* Disable advanced power management */
#define SETFEATURES_EN_MSN	0x95	/* Enable Media Status Notification */
#define SETFEATURES_EN_RLA	0xAA	/* Enable read look-ahead feature */
#define SETFEATURES_PREFETCH	0xAB	/* Sets drive prefetch value */
#define SETFEATURES_DIS_AAM	0xC2	/* Disable Automatic Acoustic Management */
#define SETFEATURES_EN_RPOD	0xCC	/* Enable reverting to power on defaults */
#define SETFEATURES_DIS_RI	0xDD	/* Disable release interrupt */
#define SETFEATURES_DIS_SI	0xDE	/* Disable SERVICE interrupt */

/* WIN_SECURITY sub-commands */

#define SECURITY_SET_PASSWORD		0xBA
#define SECURITY_UNLOCK			0xBB
#define SECURITY_ERASE_PREPARE		0xBC
#define SECURITY_ERASE_UNIT		0xBD
#define SECURITY_FREEZE_LOCK		0xBE
#define SECURITY_DISABLE_PASSWORD	0xBF


/* Taskfile related constants.
 */
#define IDE_DRIVE_TASK_INVALID		-1
#define IDE_DRIVE_TASK_NO_DATA		0
#define IDE_DRIVE_TASK_SET_XFER		1

#define IDE_DRIVE_TASK_IN		2

#define IDE_DRIVE_TASK_OUT		3
#define IDE_DRIVE_TASK_RAW_WRITE	4

struct hd_drive_task_hdr {
	u8 feature;
	u8 sector_count;
	u8 sector_number;
	u8 low_cylinder;
	u8 high_cylinder;
	u8 device_head;
} __attribute__((packed));

/*
 * Define standard taskfile in/out register
 */
#define IDE_TASKFILE_STD_OUT_FLAGS	0xFE
#define IDE_TASKFILE_STD_IN_FLAGS	0xFE
#define IDE_HOB_STD_OUT_FLAGS		0xC0
#define IDE_HOB_STD_IN_FLAGS		0xC0

#define TASKFILE_INVALID		0x7fff
#define TASKFILE_48			0x8000

#define TASKFILE_NO_DATA		0x0000

#define TASKFILE_IN			0x0001
#define TASKFILE_MULTI_IN		0x0002

#define TASKFILE_OUT			0x0004
#define TASKFILE_MULTI_OUT		0x0008
#define TASKFILE_IN_OUT			0x0010

#define TASKFILE_IN_DMA			0x0020
#define TASKFILE_OUT_DMA		0x0040
#define TASKFILE_IN_DMAQ		0x0080
#define TASKFILE_OUT_DMAQ		0x0100

#define TASKFILE_P_IN			0x0200
#define TASKFILE_P_OUT			0x0400
#define TASKFILE_P_IN_DMA		0x0800
#define TASKFILE_P_OUT_DMA		0x1000
#define TASKFILE_P_IN_DMAQ		0x2000
#define TASKFILE_P_OUT_DMAQ		0x4000

/* bus states */
enum {
	BUSSTATE_OFF = 0,
	BUSSTATE_ON,
	BUSSTATE_TRISTATE
};

/*
 * Structure returned by HDIO_GET_IDENTITY, as per ANSI NCITS ATA6 rev.1b spec.
 *
 * If you change something here, please remember to update fix_driveid() in
 * ide/probe.c.
 */
struct hd_driveid {
	u16	config;		/* lots of obsolete bit flags */
	u16	cyls;		/* Obsolete, "physical" cyls */
	u16	reserved2;	/* reserved (word 2) */
	u16	heads;		/* Obsolete, "physical" heads */
	u16	track_bytes;	/* unformatted bytes per track */
	u16	sector_bytes;	/* unformatted bytes per sector */
	u16	sectors;	/* Obsolete, "physical" sectors per track */
	u16	vendor0;	/* vendor unique */
	u16	vendor1;	/* vendor unique */
	u16	vendor2;	/* Retired vendor unique */
	u8	serial_no[20];	/* 0 = not_specified */
	u16	buf_type;	/* Retired */
	u16	buf_size;	/* Retired, 512 byte increments
				 * 0 = not_specified
				 */
	u16	ecc_bytes;	/* for r/w long cmds; 0 = not_specified */
	u8	fw_rev[8];	/* 0 = not_specified */
	char	model[40];	/* 0 = not_specified */
	u8	max_multsect;	/* 0=not_implemented */
	u8	vendor3;	/* vendor unique */
	u16	dword_io;	/* 0=not_implemented; 1=implemented */
	u8	vendor4;	/* vendor unique */
	u8	capability;	/* (upper byte of word 49)
				 *  3:	IORDYsup
				 *  2:	IORDYsw
				 *  1:	LBA
				 *  0:	DMA
				 */
	u16	reserved50;	/* reserved (word 50) */
	u8	vendor5;	/* Obsolete, vendor unique */
	u8	tPIO;		/* Obsolete, 0=slow, 1=medium, 2=fast */
	u8	vendor6;	/* Obsolete, vendor unique */
	u8	tDMA;		/* Obsolete, 0=slow, 1=medium, 2=fast */
	u16	field_valid;	/* (word 53)
				 *  2:	ultra_ok	word  88
				 *  1:	eide_ok		words 64-70
				 *  0:	cur_ok		words 54-58
				 */
	u16	cur_cyls;	/* Obsolete, logical cylinders */
	u16	cur_heads;	/* Obsolete, l heads */
	u16	cur_sectors;	/* Obsolete, l sectors per track */
	u16	cur_capacity0;	/* Obsolete, l total sectors on drive */
	u16	cur_capacity1;	/* Obsolete, (2 words, misaligned int)     */
	u8	multsect;	/* current multiple sector count */
	u8	multsect_valid;	/* when (bit0==1) multsect is ok */
	u32	lba_capacity;	/* Obsolete, total number of sectors */
	u16	dma_1word;	/* Obsolete, single-word dma info */
	u16	dma_mword;	/* multiple-word dma info */
	u16	eide_pio_modes; /* bits 0:mode3 1:mode4 */
	u16	eide_dma_min;	/* min mword dma cycle time (ns) */
	u16	eide_dma_time;	/* recommended mword dma cycle time (ns) */
	u16	eide_pio;       /* min cycle time (ns), no IORDY  */
	u16	eide_pio_iordy; /* min cycle time (ns), with IORDY */
	u16	words69_70[2];	/* reserved words 69-70
				 * future command overlap and queuing
				 */
	/* HDIO_GET_IDENTITY currently returns only words 0 through 70 */
	u16	words71_74[4];	/* reserved words 71-74
				 * for IDENTIFY PACKET DEVICE command
				 */
	u16	queue_depth;	/* (word 75)
				 * 15:5	reserved
				 *  4:0	Maximum queue depth -1
				 */
	u16	words76_79[4];	/* reserved words 76-79 */
	u16	major_rev_num;	/* (word 80) */
	u16	minor_rev_num;	/* (word 81) */
	u16	command_set_1;	/* (word 82) supported
				 * 15:	Obsolete
				 * 14:	NOP command
				 * 13:	READ_BUFFER
				 * 12:	WRITE_BUFFER
				 * 11:	Obsolete
				 * 10:	Host Protected Area
				 *  9:	DEVICE Reset
				 *  8:	SERVICE Interrupt
				 *  7:	Release Interrupt
				 *  6:	look-ahead
				 *  5:	write cache
				 *  4:	PACKET Command
				 *  3:	Power Management Feature Set
				 *  2:	Removable Feature Set
				 *  1:	Security Feature Set
				 *  0:	SMART Feature Set
				 */
	u16	command_set_2;	/* (word 83)
				 * 15:	Shall be ZERO
				 * 14:	Shall be ONE
				 * 13:	FLUSH CACHE EXT
				 * 12:	FLUSH CACHE
				 * 11:	Device Configuration Overlay
				 * 10:	48-bit Address Feature Set
				 *  9:	Automatic Acoustic Management
				 *  8:	SET MAX security
				 *  7:	reserved 1407DT PARTIES
				 *  6:	SetF sub-command Power-Up
				 *  5:	Power-Up in Standby Feature Set
				 *  4:	Removable Media Notification
				 *  3:	APM Feature Set
				 *  2:	CFA Feature Set
				 *  1:	READ/WRITE DMA QUEUED
				 *  0:	Download MicroCode
				 */
	u16	cfsse;		/* (word 84)
				 * cmd set-feature supported extensions
				 * 15:	Shall be ZERO
				 * 14:	Shall be ONE
				 * 13:3	reserved
				 *  2:	Media Serial Number Valid
				 *  1:	SMART selt-test supported
				 *  0:	SMART error logging
				 */
	u16	cfs_enable_1;	/* (word 85)
				 * command set-feature enabled
				 * 15:	Obsolete
				 * 14:	NOP command
				 * 13:	READ_BUFFER
				 * 12:	WRITE_BUFFER
				 * 11:	Obsolete
				 * 10:	Host Protected Area
				 *  9:	DEVICE Reset
				 *  8:	SERVICE Interrupt
				 *  7:	Release Interrupt
				 *  6:	look-ahead
				 *  5:	write cache
				 *  4:	PACKET Command
				 *  3:	Power Management Feature Set
				 *  2:	Removable Feature Set
				 *  1:	Security Feature Set
				 *  0:	SMART Feature Set
				 */
	u16	cfs_enable_2;	/* (word 86)
				 * command set-feature enabled
				 * 15:	Shall be ZERO
				 * 14:	Shall be ONE
				 * 13:	FLUSH CACHE EXT
				 * 12:	FLUSH CACHE
				 * 11:	Device Configuration Overlay
				 * 10:	48-bit Address Feature Set
				 *  9:	Automatic Acoustic Management
				 *  8:	SET MAX security
				 *  7:	reserved 1407DT PARTIES
				 *  6:	SetF sub-command Power-Up
				 *  5:	Power-Up in Standby Feature Set
				 *  4:	Removable Media Notification
				 *  3:	APM Feature Set
				 *  2:	CFA Feature Set
				 *  1:	READ/WRITE DMA QUEUED
				 *  0:	Download MicroCode
				 */
	u16	csf_default;	/* (word 87)
				 * command set-feature default
				 * 15:	Shall be ZERO
				 * 14:	Shall be ONE
				 * 13:3	reserved
				 *  2:	Media Serial Number Valid
				 *  1:	SMART selt-test supported
				 *  0:	SMART error logging
				 */
	u16	dma_ultra;	/* (word 88) */
	u16	word89;		/* reserved (word 89) */
	u16	word90;		/* reserved (word 90) */
	u16	CurAPMvalues;	/* current APM values */
	u16	word92;		/* reserved (word 92) */
	u16	hw_config;	/* hardware config (word 93)
				 * 15:
				 * 14:
				 * 13:
				 * 12:
				 * 11:
				 * 10:
				 *  9:
				 *  8:
				 *  7:
				 *  6:
				 *  5:
				 *  4:
				 *  3:
				 *  2:
				 *  1:
				 *  0:
				 */
	u16	acoustic;	/* (word 94)
				 * 15:8	Vendor's recommended value
				 *  7:0	current value
				 */
	u16	words95_99[5];	/* reserved words 95-99 */
	u64	lba_capacity_2;	/* 48-bit total number of sectors */
	u16	words104_125[22];/* reserved words 104-125 */
	u16	last_lun;	/* (word 126) */
	u16	word127;	/* (word 127) Feature Set
				 * Removable Media Notification
				 * 15:2	reserved
				 *  1:0	00 = not supported
				 *	01 = supported
				 *	10 = reserved
				 *	11 = reserved
				 */
	u16	dlf;		/* (word 128)
				 * device lock function
				 * 15:9	reserved
				 *  8	security level 1:max 0:high
				 *  7:6	reserved
				 *  5	enhanced erase
				 *  4	expire
				 *  3	frozen
				 *  2	locked
				 *  1	en/disabled
				 *  0	capability
				 */
	u16	csfo;		/* (word 129)
				 * current set features options
				 * 15:4	reserved
				 *  3:	auto reassign
				 *  2:	reverting
				 *  1:	read-look-ahead
				 *  0:	write cache
				 */
	u16	words130_155[26];/* reserved vendor words 130-155 */
	u16	word156;	/* reserved vendor word 156 */
	u16	words157_159[3];/* reserved vendor words 157-159 */
	u16	cfa_power;	/* (word 160) CFA Power Mode
				 * 15 word 160 supported
				 * 14 reserved
				 * 13
				 * 12
				 * 11:0
				 */
	u16	words161_175[14];/* Reserved for CFA */
	u16	words176_205[31];/* Current Media Serial Number */
	u16	words206_254[48];/* reserved words 206-254 */
	u16	integrity_word;	/* (word 255)
				 * 15:8 Checksum
				 *  7:0 Signature
				 */
} __attribute__((packed));

#define IDE_NICE_DSC_OVERLAP	(0)	/* per the DSC overlap protocol */

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

/* Bits of HD_STATUS */
#define ERR_STAT		0x01
#define INDEX_STAT		0x02
#define ECC_STAT		0x04	/* Corrected error */
#define DRQ_STAT		0x08
#define SEEK_STAT		0x10
#define SERVICE_STAT		SEEK_STAT
#define WRERR_STAT		0x20
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits for HD_ERROR */
#define MARK_ERR		0x01	/* Bad address mark */
#define TRK0_ERR		0x02	/* couldn't find track 0 */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR			0x08	/* media change request */
#define ID_ERR			0x10	/* ID field not found */
#define MC_ERR			0x20	/* media changed */
#define ECC_ERR			0x40	/* Uncorrectable ECC error */
#define BBD_ERR			0x80	/* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR		0x80	/* new meaning:  CRC error during transfer */

/*
 * sector count bits
 */
#define NSEC_CD			0x01
#define NSEC_IO			0x02
#define NSEC_REL		0x04

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
	unsigned ata_flash	: 1;	/* 1=present, 0=default */
	unsigned	addressing;	/* : 2; 0=28-bit, 1=48-bit, 2=64-bit */
	u8		scsi;		/* 0=default, 1=skip current ide-subdriver for ide-scsi emulation */

	select_t	select;		/* basic drive/head select reg value */
	u8		status;		/* last retrived status value for device */

	u8		ready_stat;	/* min status value for drive ready */
	u8		mult_count;	/* current multiple sector setting */
	u8		bad_wstat;	/* used for ignoring WRERR_STAT */
	u8		nowerr;		/* used for ignoring WRERR_STAT */
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

	unsigned long	seg_boundary_mask;
	unsigned int	max_segment_size;
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
extern int idedisk_init(void);
#endif
#ifdef CONFIG_BLK_DEV_IDECD
extern int ide_cdrom_init(void);
#endif
#ifdef CONFIG_BLK_DEV_IDETAPE
extern int idetape_init(void);
#endif
#ifdef CONFIG_BLK_DEV_IDEFLOPPY
extern int idefloppy_init(void);
#endif
#ifdef CONFIG_BLK_DEV_IDESCSI
extern int idescsi_init(void);
#endif

extern int ata_register_device(struct ata_device *, struct ata_operations *);
extern int ata_unregister_device(struct ata_device *drive);
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

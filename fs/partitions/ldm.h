#ifndef _FS_PT_LDM_H_
#define _FS_PT_LDM_H_
/*
 * ldm - Part of the Linux-NTFS project.
 *
 * Copyright (C) 2001 Richard Russon <ldm@flatcap.org>
 * Copyright (C) 2001 Anton Altaparmakov <antona@users.sf.net>
 *
 * Documentation is available at http://linux-ntfs.sf.net/ldm
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS source
 * in the file COPYING); if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <linux/genhd.h>

/* Borrowed from kernel.h. */
#define LDM_PREFIX	"LDM: "	   /* Prefix our error messages with this. */
#define LDM_CRIT	KERN_CRIT	LDM_PREFIX /* critical conditions */
#define LDM_ERR		KERN_ERR	LDM_PREFIX /* error conditions */
#define LDM_DEBUG	KERN_DEBUG	LDM_PREFIX /* debug-level messages */

/* Magic numbers in CPU format. */
#define MAGIC_VMDB	0x564D4442		/* VMDB */
#define MAGIC_VBLK	0x56424C4B		/* VBLK */
#define MAGIC_PRIVHEAD	0x5052495648454144	/* PRIVHEAD */
#define MAGIC_TOCBLOCK	0x544F43424C4F434B	/* TOCBLOCK */

/* The defined vblk types. */
#define VBLK_COMP		0x32		/* Component */
#define VBLK_PART		0x33		/* Partition */
#define VBLK_DSK1		0x34		/* Disk */
#define VBLK_DSK2		0x44		/* Disk */
#define VBLK_DGR1		0x35		/* Disk Group */
#define VBLK_DGR2		0x45		/* Disk Group */
#define VBLK_VOLU		0x51		/* Volume */

/* Other constants. */
#define LDM_BLOCKSIZE		1024		/* Size of block in bytes. */
#define LDM_DB_SIZE		2048		/* Size in sectors (= 1MiB). */
#define LDM_FIRST_PART_OFFSET	4		/* Add this to first_part_minor
						   to get to the first data
						   partition device minor. */

#define OFF_PRIVHEAD1		3		/* Offset of the first privhead
						   relative to the start of the
						   device in units of
						   LDM_BLOCKSIZE. */

/* Offsets to structures within the LDM Database in units of LDM_BLOCKSIZE. */
#define OFF_PRIVHEAD2		928		/* Backup private headers. */
#define OFF_PRIVHEAD3		1023

#define OFF_TOCBLOCK1		0		/* Tables of contents. */
#define OFF_TOCBLOCK2		1
#define OFF_TOCBLOCK3		1022
#define OFF_TOCBLOCK4		1023

#define OFF_VMDB		8		/* List of partitions. */
#define OFF_VBLK		9

#define WIN2K_DYNAMIC_PARTITION		0x42	/* Formerly SFS (Landis). */
#define WIN2K_EXTENDED_PARTITION	0x05	/* A standard extended
						   partition. */

#define TOC_BITMAP1		"config"	/* Names of the two defined */
#define TOC_BITMAP2		"log"		/* bitmaps in the TOCBLOCK. */

/* Most numbers we deal with are big-endian and won't be aligned. */
#define BE16(x)			((u16)be16_to_cpu(get_unaligned((u16*)(x))))
#define BE32(x)			((u32)be32_to_cpu(get_unaligned((u32*)(x))))
#define BE64(x)			((u64)be64_to_cpu(get_unaligned((u64*)(x))))

/* Borrowed from msdos.c. */
#define SYS_IND(p)		(get_unaligned(&(p)->sys_ind))
#define NR_SECTS(p)		({ __typeof__((p)->nr_sects) __a =	\
					get_unaligned(&(p)->nr_sects);	\
					le32_to_cpu(__a);		\
				})

#define START_SECT(p)		({ __typeof__((p)->start_sect) __a =	\
					get_unaligned(&(p)->start_sect);\
					le32_to_cpu(__a);		\
				})

/* In memory LDM database structures. */

#define DISK_ID_SIZE		64	/* Size in bytes. */

struct ldmdisk {
	u64	obj_id;
	u8	disk_id[DISK_ID_SIZE];
};

struct privhead	{			/* Offsets and sizes are in sectors. */
	u16	ver_major;
	u16	ver_minor;
	u64	logical_disk_start;
	u64	logical_disk_size;
	u64	config_start;
	u64	config_size;
	u8	disk_id[DISK_ID_SIZE];
};

struct tocblock {			/* We have exactly two bitmaps. */
	u8	bitmap1_name[16];
	u64	bitmap1_start;
	u64	bitmap1_size;
	/*u64	bitmap1_flags;*/
	u8	bitmap2_name[16];
	u64	bitmap2_start;
	u64	bitmap2_size;
	/*u64	bitmap2_flags;*/
};

struct vmdb {
	u16	ver_major;
	u16	ver_minor;
	u32	vblk_size;
	u32	vblk_offset;
	u32	last_vblk_seq;
};

struct vblk {
	u8	name[64];
	u8	vblk_type;
	u64	obj_id;
	u64	disk_id;
	u64	start_sector;
	u64	num_sectors;
};

struct ldm_part {
	struct list_head part_list;
	unsigned long start;
	unsigned long size;
};

int ldm_partition(struct gendisk *hd, struct block_device *bdev,
		unsigned long first_sector, int first_part_minor);

#endif /* _FS_PT_LDM_H_ */


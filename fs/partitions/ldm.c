/*
 * ldm - Part of the Linux-NTFS project.
 *
 * Copyright (C) 2001 Richard Russon <ldm@flatcap.org>
 * Copyright (C) 2001 Anton Altaparmakov <antona@users.sf.net> (AIA)
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
 *
 * 28/10/2001 - Added sorting of ldm partitions. (AIA)
 */
#include <linux/types.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include "check.h"
#include "ldm.h"
#include "msdos.h"

#if 0 /* Fool kernel-doc since it doesn't do macros yet. */
/**
 * ldm_debug - output an error message if debugging was enabled at compile time
 * @f:		a printf format string containing the message
 * @...:	the variables to substitute into @f
 *
 * ldm_debug() writes a DEBUG level message to the syslog but only if the
 * driver was compiled with debug enabled. Otherwise, the call turns into a NOP.
 */
static void ldm_debug(const char *f, ...);
#endif
#ifdef CONFIG_LDM_DEBUG
#define ldm_debug(f, a...)						\
	{								\
		printk(LDM_DEBUG " DEBUG (%s, %d): %s: ",		\
				__FILE__, __LINE__, __FUNCTION__);	\
		printk(f, ##a);						\
	}
#else	/* !CONFIG_LDM_DEBUG */
#define ldm_debug(f, a...)	do {} while (0)
#endif	/* !CONFIG_LDM_DEBUG */

/* Necessary forward declarations. */
static int create_partition(struct gendisk *, int, int, int);
static int parse_privhead(const u8 *, struct privhead *);
static u64 get_vnum(const u8 *, int *);
static int get_vstr(const u8 *, u8 *, const int);

/**
 * parse_vblk_part - parse a LDM database vblk partition record
 * @buffer:	vblk partition record loaded from the LDM database
 * @buf_size:	size of @buffer in bytes
 * @vb:		in memory vblk structure to return parsed information in
 *
 * This parses the LDM database vblk record of type VBLK_PART, i.e. a partition
 * record, supplied in @buffer and sets up the in memory vblk structure @vb
 * with the obtained information.
 *
 * Return 1 on success and -1 on error, in which case @vb is undefined.
 */
static int parse_vblk_part(const u8 *buffer, const int buf_size,
		struct vblk *vb)
{
	int err, rel_objid, rel_name, rel_size, rel_parent;

	if (0x34 >= buf_size)
		return -1;
	/* Calculate relative offsets. */
	rel_objid  = 1 + buffer[0x18];
	if (0x18 + rel_objid >= buf_size)
		return -1;
	rel_name   = 1 + buffer[0x18 + rel_objid] + rel_objid;
	if (0x34 + rel_name >= buf_size)
		return -1;
	rel_size   = 1 + buffer[0x34 + rel_name] + rel_name;
	if (0x34 + rel_size >= buf_size)
		return -1;
	rel_parent = 1 + buffer[0x34 + rel_size] + rel_size;
	if (0x34 + rel_parent >= buf_size)
		return -1;
	/* Setup @vb. */
	vb->vblk_type    = VBLK_PART;
	vb->obj_id       = get_vnum(buffer + 0x18, &err);
	if (err || 0x34 + rel_parent + buffer[0x34 + rel_parent] >= buf_size)
		return -1;
	vb->disk_id      = get_vnum(buffer + 0x34 + rel_parent, &err);
	if (err || 0x24 + rel_name + 8 > buf_size)
		return -1;
	vb->start_sector = BE64(buffer + 0x24 + rel_name);
	if (0x34 + rel_name + buffer[0x34 + rel_name] >= buf_size)
		return -1;
	vb->num_sectors  = get_vnum(buffer + 0x34 + rel_name, &err);
	if (err || 0x18 + rel_objid + buffer[0x18 + rel_objid] >= buf_size)
		return -1;
	err = get_vstr(buffer + 0x18 + rel_objid, vb->name, sizeof(vb->name));
	if (err == -1)
		return err;
	ldm_debug("Parsed Partition VBLK successfully.\n");
	return 1;
}

/**
 * parse_vblk - parse a LDM database vblk record
 * @buffer:	vblk record loaded from the LDM database
 * @buf_size:	size of @buffer in bytes
 * @vb:		in memory vblk structure to return parsed information in
 *
 * This parses the LDM database vblk record supplied in @buffer and sets up
 * the in memory vblk structure @vb with the obtained information.
 *
 * Return 1 on success, 0 if successful but record not in use, and -1 on error.
 * If the return value is 0 or -1, @vb is undefined.
 *
 * NOTE: Currently the only record type we handle is VBLK_PART, i.e. records
 * describing a partition. For all others, we just set @vb->vblk_type to 0 and
 * return success. This of course means that if @vb->vblk_type is zero, all
 * other fields in @vb are undefined.
 */
static int parse_vblk(const u8 *buffer, const int buf_size, struct vblk *vb)
{
	int err = 1;

	if (buf_size < 0x14)
		return -1;
	if (MAGIC_VBLK != BE32(buffer)) {
		printk(LDM_CRIT "Cannot find VBLK, database may be corrupt.\n");
		return -1;
	}
	if ((BE16(buffer + 0x0E) == 0) ||       /* Record is not in use. */
	    (BE16(buffer + 0x0C) != 0))         /* Part 2 of an ext. record */
		return 0;
	/* FIXME: What about extended VBLKs? */
	switch (buffer[0x13]) {
	case VBLK_PART:
		err = parse_vblk_part(buffer, buf_size, vb);
		break;
	default:
		vb->vblk_type = 0;
	}
	if (err != -1)
		ldm_debug("Parsed VBLK successfully.\n");
	return err;
}

/**
 * add_partition_to_list - insert partition into a partition list
 * @pl:		sorted list of partitions
 * @hd:		gendisk structure to which the data partition belongs
 * @disk_minor:	minor number of the disk device
 * @start:	first sector within the disk device
 * @size:	number of sectors on the partition device
 *
 * This sanity checks the partition specified by @start and @size against the
 * device specified by @hd and inserts the partition into the sorted partition
 * list @pl if the checks pass.
 *
 * On success return 1, otherwise return -1.
 *
 * TODO: Add sanity check for overlapping partitions. (AIA)
 */ 
static int add_partition_to_list(struct list_head *pl, const struct gendisk *hd,
		const int disk_minor, const unsigned long start,
		const unsigned long size)
{
	struct ldm_part *lp, *lptmp;
	struct list_head *tmp;

	if (!hd->part)
		return -1;
	if ((start < 1) || ((start + size) > hd->part[disk_minor].nr_sects)) {
		printk(LDM_CRIT "LDM partition exceeds physical disk. "
				"Skipping.\n");
		return -1;
	}
	lp = (struct ldm_part*)kmalloc(sizeof(struct ldm_part), GFP_KERNEL);
	if (!lp) {
		printk(LDM_CRIT "Not enough memory! Aborting LDM partition "
				"parsing.\n");
		return -2;
	}
	INIT_LIST_HEAD(&lp->part_list);
	lp->start = start;
	lp->size = size;
	list_for_each(tmp, pl) {
		lptmp = list_entry(tmp, struct ldm_part, part_list);
		if (start > lptmp->start)
			continue;
		if (start < lptmp->start)
			break;
		printk(LDM_CRIT "Duplicate LDM partition entry! Skipping.\n");
		kfree(lp);
		return -1;
	}
	list_add_tail(&lp->part_list, tmp);
	ldm_debug("Added LDM partition successfully.\n");
	return 1;
}

/**
 * create_data_partitions - create the data partition devices
 * @hd:			gendisk structure in which to create the data partitions
 * @first_sector:	first sector within the disk device
 * @first_part_minor:	first minor number of data partition devices
 * @dev:		partition device holding the LDM database
 * @vm:			in memory vmdb structure of @dev
 * @ph:			in memory privhead structure of the disk device
 * @dk:			in memory ldmdisk structure of the disk device
 *
 * The database contains ALL the partitions for ALL the disks, so we need to
 * filter out this specific disk. Using the disk's object id, we can find all
 * the partitions in the database that belong to this disk.
 *
 * For each found partition, we create a corresponding partition device starting
 * with minor number @first_part_minor. But we do this in such a way that we
 * actually sort the partitions in order of on-disk position. Any invalid
 * partitions are completely ignored/skipped (an error is output but that's
 * all).
 *
 * Return 1 on success and -1 on error.
 */
static int create_data_partitions(struct gendisk *hd,
		const unsigned long first_sector, int first_part_minor,
		struct block_device *bdev, const struct vmdb *vm,
		const struct privhead *ph, const struct ldmdisk *dk,
		unsigned long base)
{
	Sector sect;
	unsigned char *data;
	struct vblk *vb;
	LIST_HEAD(pl);		/* Sorted list of partitions. */
	struct ldm_part *lp;
	struct list_head *tmp;
	int vblk;
	int vsize;		/* VBLK size. */
	int perbuf;		/* VBLKs per buffer. */
	int buffer, lastbuf, lastofs, err, disk_minor;

	vb = (struct vblk*)kmalloc(sizeof(struct vblk), GFP_KERNEL);
	if (!vb)
		goto no_mem;
	vsize   = vm->vblk_size;
	if (vsize < 1 || vsize > 512)
		goto err_out;
	perbuf  = 512 / vsize;
	if (perbuf < 1 || 512 % vsize)
		goto err_out;
					/* 512 == VMDB size */
	lastbuf = vm->last_vblk_seq / perbuf - 1;
	lastofs = vm->last_vblk_seq % perbuf;
	if (lastofs)
		lastbuf++;
	if (OFF_VBLK * LDM_BLOCKSIZE + vm->last_vblk_seq * vsize >
			ph->config_size * 512)
		goto err_out;
	/*
	 * Get the minor number of the parent device so we can check we don't
	 * go beyond the end of the device.
	 */
	disk_minor = (first_part_minor >> hd->minor_shift) << hd->minor_shift;
	for (buffer = 0; buffer < lastbuf; buffer++) {
		data = read_dev_sector(bdev, base + 2*OFF_VBLK + buffer, &sect);
		if (!data)
			goto read_err;
		for (vblk = 0; vblk < perbuf; vblk++) {
			u8 *block;
			
			if (lastofs && buffer == lastbuf - 1 && vblk >= lastofs)
				break;
			block = data + vsize * vblk;
			if (block + vsize > data + 512)
				goto brelse_out;
			if (parse_vblk(block, vsize, vb) != 1)
				continue;
			if (vb->vblk_type != VBLK_PART)
				continue;
			if (dk->obj_id != vb->disk_id)
				continue;
			/* Ignore invalid partition errors. */
			if (add_partition_to_list(&pl, hd, disk_minor,
					first_sector + vb->start_sector +
					ph->logical_disk_start,
					vb->num_sectors) < -1)
				goto brelse_out;
		}
		put_dev_sector(sect);
	}
	err = 1;
out:
	/* Finally create the nicely sorted data partitions. */
	printk(" <");
	list_for_each(tmp, &pl) {
		lp = list_entry(tmp, struct ldm_part, part_list);
		add_gd_partition(hd, first_part_minor++, lp->start, lp->size);
	}
	printk(" >\n");
	if (!list_empty(&pl)) {
		struct list_head *tmp2;

		/* Cleanup the partition list which is now superfluous. */
		list_for_each_safe(tmp, tmp2, &pl) {
			lp = list_entry(tmp, struct ldm_part, part_list);
			list_del(tmp);
			kfree(lp);
		}
	}
	kfree(vb);
	return err;
brelse_out:
	put_dev_sector(sect);
	goto err_out;
no_mem:
	printk(LDM_CRIT "Not enough memory to allocate required buffers.\n");
	goto err_out;
read_err:
	printk(LDM_CRIT "Disk read failed in create_partitions.\n");
err_out:
	err = -1;
	goto out;
}

/**
 * get_vnum - convert a variable-width, big endian number, to cpu u64 one
 * @block:	pointer to the variable-width number to convert
 * @err:	address of an integer into which to return the error code.
 *
 * This converts a variable-width, big endian number into a 64-bit, CPU format
 * number and returns the result with err set to 0. If an error occurs return 0
 * with err set to -1.
 *
 * The first byte of a variable-width number is the size of the number in bytes.
 */
static u64 get_vnum(const u8 *block, int *err)
{
	u64 tmp = 0ULL;
	u8 length = *block++;

	if (length && length <= 8) {
		while (length--)
			tmp = (tmp << 8) | *block++;
		*err = 0;
	} else {
		printk(LDM_ERR "Illegal length in get_vnum(): %d.\n", length);
		*err = 1;
	}
	return tmp;
}

/**
 * get_vstr - convert a counted, non-null-terminated ASCII string to C-style one
 * @block:	string to convert
 * @buffer:	output buffer
 * @buflen:	size of output buffer
 *
 * This converts @block, a counted, non-null-terminated ASCII string, into a
 * C-style, null-terminated, ASCII string and returns this in @buffer. The
 * maximum number of characters converted is given by @buflen.
 *
 * The first bytes of a counted string stores the length of the string in bytes.
 *
 * Return the number of characters written to @buffer, not including the
 * terminating null character, on success, and -1 on error, in which case
 * @buffer is not defined.
 */
static int get_vstr(const u8 *block, u8 *buffer, const int buflen)
{
	int length = block[0];

	if (length < 1)
		return -1;
	if (length >= buflen) {
		printk(LDM_ERR "String too long for buffer in get_vstr(): "
				"(%d/%d). Truncating.\n", length, buflen);
		length = buflen - 1;
	}
	memcpy(buffer, block + 1, length);
	buffer[length] = (u8)'\0';
	return length;
}

/**
 * get_disk_objid - obtain the object id for the device we are working on
 * @dev:	partition device holding the LDM database
 * @vm:		in memory vmdb structure of the LDM database
 * @ph:		in memory privhead structure of the device we are working on
 * @dk:		in memory ldmdisk structure to return information into
 *
 * This obtains the object id for the device we are working on as defined by
 * the private header @ph. The obtained object id, together with the disk's
 * GUID from @ph are returned in the ldmdisk structure pointed to by @dk.
 *
 * A Disk has two Ids. The main one is a GUID in string format. The second,
 * used internally for cross-referencing, is a small, sequentially allocated,
 * number. The PRIVHEAD, just after the partition table, tells us the disk's
 * GUID. To find the disk's object id, we have to look through the database.
 *
 * Return 1 on success and -1 on error, in which case @dk is undefined.
 */
static int get_disk_objid(struct block_device *bdev, const struct vmdb *vm,
		const struct privhead *ph, struct ldmdisk *dk,
		unsigned long base)
{
	Sector sect;
	unsigned char *data;
	u8 *disk_id;
	int vblk;
	int vsize;		/* VBLK size. */
	int perbuf;		/* VBLKs per buffer. */
	int buffer, lastbuf, lastofs, err;

	disk_id = (u8*)kmalloc(DISK_ID_SIZE, GFP_KERNEL);
	if (!disk_id)
		goto no_mem;
	vsize   = vm->vblk_size;
	if (vsize < 1 || vsize > 512)
		goto err_out;
	perbuf  = 512 / vsize;
	if (perbuf < 1 || 512 % vsize)
		goto err_out;
					/* 512 == VMDB size */
	lastbuf = vm->last_vblk_seq / perbuf - 1;
	lastofs = vm->last_vblk_seq % perbuf;
	if (lastofs)
		lastbuf++;
	if (OFF_VBLK * LDM_BLOCKSIZE + vm->last_vblk_seq * vsize >
			ph->config_size * 512)
		goto err_out;
	for (buffer = 0; buffer < lastbuf; buffer++) {
		data = read_dev_sector(bdev, base + 2*OFF_VBLK + buffer, &sect);
		if (!data)
			goto read_err;
		for (vblk = 0; vblk < perbuf; vblk++) {
			int rel_objid, rel_name, delta;
			u8 *block;

			if (lastofs && buffer == lastbuf - 1 && vblk >= lastofs)
				break;
			block = data + vblk * vsize;
			delta = vblk * vsize + 0x18;
			if (delta >= 512)
				goto brelse_out;
			if (block[0x0D] != 0)	/* Extended VBLK, ignore */
				continue;
			if ((block[0x13] != VBLK_DSK1) &&
			    (block[0x13] != VBLK_DSK2))
				continue;
			/* Calculate relative offsets. */
			rel_objid = 1 + block[0x18];
			if (delta + rel_objid >= 512)
				goto brelse_out;
			rel_name  = 1 + block[0x18 + rel_objid] + rel_objid;
			if (delta + rel_name >= 512 ||
			    delta + rel_name + block[0x18 + rel_name] >= 512)
				goto brelse_out;
			err = get_vstr(block + 0x18 + rel_name, disk_id,
					DISK_ID_SIZE);
			if (err == -1)
				goto brelse_out;
			if (!strncmp(disk_id, ph->disk_id, DISK_ID_SIZE)) {
				dk->obj_id = get_vnum(block + 0x18, &err);
				put_dev_sector(sect);
				if (err)
					goto out;
				strncpy(dk->disk_id, ph->disk_id,
						sizeof(dk->disk_id));
				dk->disk_id[sizeof(dk->disk_id) - 1] = (u8)'\0';
				err = 1;
				goto out;
			}
		}
		put_dev_sector(sect);
	}
	err = -1;
out:
	kfree(disk_id);
	return err;
brelse_out:
	put_dev_sector(sect);
	goto err_out;
no_mem:
	printk(LDM_CRIT "Not enough memory to allocate required buffers.\n");
	goto err_out;
read_err:
	printk(LDM_CRIT "Disk read failed in get_disk_objid.\n");
err_out:
	err = -1;
	goto out;
}

/**
 * parse_vmdb - parse the LDM database vmdb structure
 * @buffer:	LDM database vmdb structure loaded from the device
 * @vm:		in memory vmdb structure to return parsed information in
 *
 * This parses the LDM database vmdb structure supplied in @buffer and sets up
 * the in memory vmdb structure @vm with the obtained information.
 *
 * Return 1 on success and -1 on error, in which case @vm is undefined.
 *
 * NOTE: The *_start, *_size and *_seq values returned in @vm have not been
 * checked for validity, so make sure to check them when using them.
 */
static int parse_vmdb(const u8 *buffer, struct vmdb *vm)
{
	if (MAGIC_VMDB != BE32(buffer)) {
		printk(LDM_CRIT "Cannot find VMDB, database may be corrupt.\n");
		return -1;
	}
	vm->ver_major = BE16(buffer + 0x12);
	vm->ver_minor = BE16(buffer + 0x14);
	if ((vm->ver_major != 4) || (vm->ver_minor != 10)) {
		printk(LDM_ERR "Expected VMDB version %d.%d, got %d.%d. "
				"Aborting.\n", 4, 10, vm->ver_major,
				vm->ver_minor);
		return -1;
	}
	vm->vblk_size	  = BE32(buffer + 0x08);
	vm->vblk_offset   = BE32(buffer + 0x0C);
	vm->last_vblk_seq = BE32(buffer + 0x04);

	ldm_debug("Parsed VMDB successfully.\n");
	return 1;
}

/**
 * validate_vmdb - validate the vmdb
 * @dev:	partition device holding the LDM database
 * @vm:		in memory vmdb in which to return information
 *
 * Find the vmdb of the LDM database stored on @dev and return the parsed
 * information into @vm.
 *
 * Return 1 on success and -1 on error, in which case @vm is undefined.
 */
static int validate_vmdb(struct block_device *bdev, struct vmdb *vm, unsigned long base)
{
	Sector sect;
	unsigned char *data;
	int ret;

	data = read_dev_sector(bdev, base + OFF_VMDB * 2 + 1, &sect);
	if (!data) {
		printk(LDM_CRIT "Disk read failed in validate_vmdb.\n");
		return -1;
	}
	ret = parse_vmdb(data, vm);
	put_dev_sector(sect);
	return ret;
}

/**
 * compare_tocblocks - compare two tables of contents
 * @toc1:	first toc
 * @toc2:	second toc
 *
 * This compares the two tables of contents @toc1 and @toc2.
 *
 * Return 1 if @toc1 and @toc2 are equal and -1 otherwise.
 */
static int compare_tocblocks(const struct tocblock *toc1,
		const struct tocblock *toc2)
{
	if ((toc1->bitmap1_start == toc2->bitmap1_start)	&&
	    (toc1->bitmap1_size  == toc2->bitmap1_size)		&&
	    (toc1->bitmap2_start == toc2->bitmap2_start)	&&
	    (toc1->bitmap2_size  == toc2->bitmap2_size)		&&
	    !strncmp(toc1->bitmap1_name, toc2->bitmap1_name,
			sizeof(toc1->bitmap1_name))		&&
	    !strncmp(toc1->bitmap2_name, toc2->bitmap2_name,
			sizeof(toc1->bitmap2_name)))
		return 1;
	return -1;
}

/**
 * parse_tocblock - parse the LDM database table of contents structure
 * @buffer:	LDM database toc structure loaded from the device
 * @toc:	in memory toc structure to return parsed information in
 *
 * This parses the LDM database table of contents structure supplied in @buffer
 * and sets up the in memory table of contents structure @toc with the obtained
 * information.
 *
 * Return 1 on success and -1 on error, in which case @toc is undefined.
 *
 * FIXME: The *_start and *_size values returned in @toc are not been checked
 * for validity but as we don't use the actual values for anything other than
 * comparing between the toc and its backups, the values are not important.
 */
static int parse_tocblock(const u8 *buffer, struct tocblock *toc)
{
	if (MAGIC_TOCBLOCK != BE64(buffer)) {
		printk(LDM_CRIT "Cannot find TOCBLOCK, database may be "
				"corrupt.\n");
		return -1;
	}
	strncpy(toc->bitmap1_name, buffer + 0x24, sizeof(toc->bitmap1_name));
	toc->bitmap1_name[sizeof(toc->bitmap1_name) - 1] = (u8)'\0';
	toc->bitmap1_start = BE64(buffer + 0x2E);
	toc->bitmap1_size  = BE64(buffer + 0x36);
	/*toc->bitmap1_flags = BE64(buffer + 0x3E);*/
	if (strncmp(toc->bitmap1_name, TOC_BITMAP1,
			sizeof(toc->bitmap1_name)) != 0) {
		printk(LDM_CRIT "TOCBLOCK's first bitmap should be %s, but is "
				"%s.\n", TOC_BITMAP1, toc->bitmap1_name);
		return -1;
	}
	strncpy(toc->bitmap2_name, buffer + 0x46, sizeof(toc->bitmap2_name));
	toc->bitmap2_name[sizeof(toc->bitmap2_name) - 1] = (u8)'\0';
	toc->bitmap2_start = BE64(buffer + 0x50);
	toc->bitmap2_size  = BE64(buffer + 0x58);
	/*toc->bitmap2_flags = BE64(buffer + 0x60);*/
	if (strncmp(toc->bitmap2_name, TOC_BITMAP2,
			sizeof(toc->bitmap2_name)) != 0) {
		printk(LDM_CRIT "TOCBLOCK's second bitmap should be %s, but is "
				"%s.\n", TOC_BITMAP2, toc->bitmap2_name);
		return -1;
	}
	ldm_debug("Parsed TOCBLOCK successfully.\n");
	return 1;
}

/**
 * validate_tocblocks - validate the table of contents and its backups
 * @dev:	partition device holding the LDM database
 * @toc1:	in memory table of contents in which to return information
 *
 * Find and compare the four tables of contents of the LDM database stored on
 * @dev and return the parsed information into @toc1.
 *
 * Return 1 on success and -1 on error, in which case @toc1 is undefined.
 */
static int validate_tocblocks(struct block_device *bdev,
			struct tocblock *toc1,
			unsigned long base)
{
	Sector sect;
	unsigned char *data;
	struct tocblock *toc2 = NULL, *toc3 = NULL, *toc4 = NULL;
	int err;

	toc2 = (struct tocblock*)kmalloc(sizeof(*toc2), GFP_KERNEL);
	if (!toc2)
		goto no_mem;
	toc3 = (struct tocblock*)kmalloc(sizeof(*toc3), GFP_KERNEL);
	if (!toc3)
		goto no_mem;
	toc4 = (struct tocblock*)kmalloc(sizeof(*toc4), GFP_KERNEL);
	if (!toc4)
		goto no_mem;
	/* Read and parse first toc. */
	data = read_dev_sector(bdev, base + OFF_TOCBLOCK1 * 2 + 1, &sect);
	if (!data) {
		printk(LDM_CRIT "Disk read 1 failed in validate_tocblocks.\n");
		goto err_out;
	}
	err = parse_tocblock(data, toc1);
	put_dev_sector(sect);
	if (err != 1)
		goto out;
	/* Read and parse second toc. */
	data = read_dev_sector(bdev, base + OFF_TOCBLOCK2 * 2, &sect);
	if (!data) {
		printk(LDM_CRIT "Disk read 2 failed in validate_tocblocks.\n");
		goto err_out;
	}
	err = parse_tocblock(data, toc2);
	put_dev_sector(sect);
	if (err != 1)
		goto out;
	/* Read and parse third toc. */
	data = read_dev_sector(bdev, base + OFF_TOCBLOCK3 * 2 + 1, &sect);
	if (!data) {
		printk(LDM_CRIT "Disk read 3 failed in validate_tocblocks.\n");
		goto err_out;
	}
	err = parse_tocblock(data, toc3);
	put_dev_sector(sect);
	if (err != 1)
		goto out;
	/* Read and parse fourth toc. */
	data = read_dev_sector(bdev, base + OFF_TOCBLOCK4 * 2, &sect);
	if (!data) {
		printk(LDM_CRIT "Disk read 4 failed in validate_tocblocks.\n");
		goto err_out;
	}
	err = parse_tocblock(data, toc4);
	put_dev_sector(sect);
	if (err != 1)
		goto out;
	/* Compare all tocs. */
	err = compare_tocblocks(toc1, toc2);
	if (err != 1) {
		printk(LDM_CRIT "First and second TOCBLOCKs don't match.\n");
		goto out;
	}
	err = compare_tocblocks(toc3, toc4);
	if (err != 1) {
		printk(LDM_CRIT "Third and fourth TOCBLOCKs don't match.\n");
		goto out;
	}
	err = compare_tocblocks(toc1, toc3);
	if (err != 1)
		printk(LDM_CRIT "First and third TOCBLOCKs don't match.\n");
	else
		ldm_debug("Validated TOCBLOCKs successfully.\n");
out:
	kfree(toc2);
	kfree(toc3);
	kfree(toc4);
	return err;
no_mem:
	printk(LDM_CRIT "Not enough memory to allocate required buffers.\n");
err_out:
	err = -1;
	goto out;
}

/**
 * compare_privheads - compare two privheads
 * @ph1:	first privhead
 * @ph2:	second privhead
 *
 * This compares the two privheads @ph1 and @ph2.
 *
 * Return 1 if @ph1 and @ph2 are equal and -1 otherwise.
 */
static int compare_privheads(const struct privhead *ph1,
		const struct privhead *ph2)
{
	if ((ph1->ver_major == ph2->ver_major)			 &&
	    (ph1->ver_minor == ph2->ver_minor)			 &&
	    (ph1->logical_disk_start == ph2->logical_disk_start) &&
	    (ph1->logical_disk_size  == ph2->logical_disk_size)	 &&
	    (ph1->config_start == ph2->config_start)		 &&
	    (ph1->config_size  == ph2->config_size)		 &&
	    !strncmp(ph1->disk_id, ph2->disk_id, sizeof(ph1->disk_id)))
		return 1;
	return -1;
}

/**
 * validate_privheads - compare the privhead backups to the first one
 * @dev:	partition device holding the LDM database
 * @ph1:	first privhead which we have already validated before
 *
 * We already have one privhead from the beginning of the disk.
 * Now we compare the two other copies for safety.
 *
 * Return 1 on succes and -1 on error.
 */
static int validate_privheads(struct block_device *bdev,
			      const struct privhead *ph1,
			      unsigned long base)
{
	Sector sect;
	unsigned char *data;
	struct privhead *ph2 = NULL, *ph3 = NULL;
	int err;

	ph2 = (struct privhead*)kmalloc(sizeof(*ph2), GFP_KERNEL);
	if (!ph2)
		goto no_mem;
	ph3 = (struct privhead*)kmalloc(sizeof(*ph3), GFP_KERNEL);
	if (!ph3)
		goto no_mem;
	data = read_dev_sector(bdev, base + OFF_PRIVHEAD2 * 2, &sect);
	if (!data) {
		printk(LDM_CRIT "Disk read 1 failed in validate_privheads.\n");
		goto err_out;
	}
	err = parse_privhead(data, ph2);
	put_dev_sector(sect);
	if (err != 1)
		goto out;
	data = read_dev_sector(bdev, base + OFF_PRIVHEAD3 * 2 + 1, &sect);
	if (!data) {
		printk(LDM_CRIT "Disk read 2 failed in validate_privheads.\n");
		goto err_out;
	}
	err = parse_privhead(data, ph3);
	put_dev_sector(sect);
	if (err != 1)
		goto out;
	err = compare_privheads(ph1, ph2);
	if (err != 1) {
		printk(LDM_CRIT "First and second PRIVHEADs don't match.\n");
		goto out;
	}
	err = compare_privheads(ph1, ph3);
	if (err != 1)
		printk(LDM_CRIT "First and third PRIVHEADs don't match.\n");
	else
		/* We _could_ have checked more. */
		ldm_debug("Validated PRIVHEADs successfully.\n");
out:
	kfree(ph2);
	kfree(ph3);
	return err;
no_mem:
	printk(LDM_CRIT "Not enough memory to allocate required buffers.\n");
err_out:
	err = -1;
	goto out;
}

/**
 * create_partition - validate input and create a kernel partition device
 * @hd:		gendisk structure in which to create partition
 * @minor:	minor number for device to create
 * @start:	starting offset of the partition into the parent device
 * @size:	size of the partition
 *
 * This validates the range, then puts an entry into the kernel's partition
 * table.
 *
 * @start and @size are numbers of sectors.
 *
 * Return 1 on succes and -1 on error.
 */
static int create_partition(struct gendisk *hd, const int minor,
		const int start, const int size)
{
	int disk_minor;

	if (!hd->part)
		return -1;
	/*
	 * Get the minor number of the parent device so we can check we don't
	 * go beyond the end of the device.
	 */
	disk_minor = (minor >> hd->minor_shift) << hd->minor_shift;
	if ((start < 1) || ((start + size) > hd->part[disk_minor].nr_sects)) {
		printk(LDM_CRIT "LDM Partition exceeds physical disk. "
				"Aborting.\n");
		return -1;
	}
	add_gd_partition(hd, minor, start, size);
	ldm_debug("Created partition successfully.\n");
	return 1;
}

/**
 * parse_privhead - parse the LDM database PRIVHEAD structure
 * @buffer:	LDM database privhead structure loaded from the device
 * @ph:		in memory privhead structure to return parsed information in
 *
 * This parses the LDM database PRIVHEAD structure supplied in @buffer and
 * sets up the in memory privhead structure @ph with the obtained information.
 *
 * Return 1 on succes and -1 on error, in which case @ph is undefined.
 */
static int parse_privhead(const u8 *buffer, struct privhead *ph)
{
	if (MAGIC_PRIVHEAD != BE64(buffer)) {
		printk(LDM_ERR "Cannot find PRIVHEAD structure. LDM database "
				"is corrupt. Aborting.\n");
		return -1;
	}
	ph->ver_major = BE16(buffer + 0x000C);
	ph->ver_minor = BE16(buffer + 0x000E);
	if ((ph->ver_major != 2) || (ph->ver_minor != 11)) {
		printk(LDM_ERR "Expected PRIVHEAD version %d.%d, got %d.%d. "
				"Aborting.\n", 2, 11, ph->ver_major,
				ph->ver_minor);
		return -1;
	}
	ph->config_start = BE64(buffer + 0x012B);
	ph->config_size  = BE64(buffer + 0x0133);
	if (ph->config_size != LDM_DB_SIZE) {	/* 1 MiB in sectors. */
		printk(LDM_ERR "Database should be %u bytes, claims to be %Lu "
				"bytes. Aborting.\n", LDM_DB_SIZE,
				ph->config_size);
		return -1;
	}
	ph->logical_disk_start = BE64(buffer + 0x011B);
	ph->logical_disk_size  = BE64(buffer + 0x0123);
	if (!ph->logical_disk_size ||
	    ph->logical_disk_start + ph->logical_disk_size > ph->config_start)
		return -1;

	memcpy(ph->disk_id, buffer + 0x0030, sizeof(ph->disk_id));

	ldm_debug("Parsed PRIVHEAD successfully.\n");
	return 1;
}

/**
 * create_db_partition - create a dedicated partition for our database
 * @hd:		gendisk structure in which to create partition
 * @dev:	device of which to create partition
 * @ph:		@dev's LDM database private header
 *
 * Find the primary private header, locate the LDM database, then create a
 * partition to wrap it.
 *
 * Return 1 on succes, 0 if device is not a dynamic disk and -1 on error.
 */
static int create_db_partition(struct gendisk *hd, struct block_device *bdev,
		const unsigned long first_sector, const int first_part_minor,
		struct privhead *ph)
{
	Sector sect;
	unsigned char *data;
	int err;

	data = read_dev_sector(bdev, OFF_PRIVHEAD1*2, &sect);
	if (!data) {
		printk(LDM_CRIT __FUNCTION__ "(): Device read failed.\n");
		return -1;
	}
	if (BE64(data) != MAGIC_PRIVHEAD) {
		ldm_debug("Cannot find PRIVHEAD structure. Not a dynamic disk "
				"or corrupt LDM database.\n");
		return 0;
	}
	err = parse_privhead(data, ph);
	if (err == 1)
		err = create_partition(hd, first_part_minor, first_sector +
				ph->config_start, ph->config_size);
	put_dev_sector(sect);
	return err;
}

/**
 * validate_patition_table - check whether @dev is a dynamic disk
 * @dev:	device to test
 *
 * Check whether @dev is a dynamic disk by looking for an MS-DOS-style partition
 * table with one or more entries of type 0x42 (the former Secure File System
 * (Landis) partition type, now recycled by Microsoft for dynamic disks) in it.
 * If this succeeds we assume we have a dynamic disk, and not otherwise.
 *
 * Return 1 if @dev is a dynamic disk, 0 if not and -1 on error.
 */
static int validate_partition_table(struct block_device *bdev)
{
	Sector sect;
	unsigned char *data;
	struct partition *p;
	int i, nr_sfs;

	data = read_dev_sector(bdev, 0, &sect);
	if (!data)
		return -1;

	if (*(u16*)(data + 0x01FE) != cpu_to_le16(MSDOS_LABEL_MAGIC)) {
		ldm_debug("No MS-DOS partition found.\n");
		goto no_msdos_partition;
	}
	nr_sfs = 0;
	p = (struct partition*)(data + 0x01BE);
	for (i = 0; i < 4; i++) {
		if (!SYS_IND(p+i) || SYS_IND(p+i) == WIN2K_EXTENDED_PARTITION)
			continue;
		if (SYS_IND(p+i) == WIN2K_DYNAMIC_PARTITION) {
			nr_sfs++;
			continue;
		}
		goto not_dynamic_disk;
	}
	if (!nr_sfs)
		goto not_dynamic_disk;
	ldm_debug("Parsed partition table successfully.\n");
	put_dev_sector(sect);
	return 1;
not_dynamic_disk:
//	ldm_debug("Found basic MS-DOS partition, not a dynamic disk.\n");
no_msdos_partition:
	put_dev_sector(sect);
	return 0;
}

/**
 * ldm_partition - find out whether a device is a dynamic disk and handle it
 * @hd:			gendisk structure in which to return the handled disk
 * @dev:		device we need to look at
 * @first_sector:	first sector within the device
 * @first_part_minor:	first minor number of partitions for the device
 *
 * Description:
 *
 * This determines whether the device @dev is a dynamic disk and if so creates
 * the partitions necessary in the gendisk structure pointed to by @hd.
 *
 * We create a dummy device 1, which contains the LDM database, we skip
 * devices 2-4 and then create each partition described by the LDM database
 * in sequence as devices 5 and following. For example, if the device is hda,
 * we would have: hda1: LDM database, hda2-4: nothing, hda5-following: the
 * actual data containing partitions.
 *
 * Return values:
 *
 *	 1 if @dev is a dynamic disk and we handled it,
 *	 0 if @dev is not a dynamic disk,
 *	-1 if an error occured.
 */
int ldm_partition(struct gendisk *hd, struct block_device *bdev,
		unsigned long first_sector, int first_part_minor)
{
	struct privhead *ph  = NULL;
	struct tocblock *toc = NULL;
	struct vmdb     *vm  = NULL;
	struct ldmdisk  *dk  = NULL;
	unsigned long db_first;
	int err;

	if (!hd)
		return 0;
	/* Check the partition table. */
	err = validate_partition_table(bdev);
	if (err != 1)
		return err;
	if (!(ph = (struct privhead*)kmalloc(sizeof(*ph), GFP_KERNEL)))
		goto no_mem;
	/* Create the LDM database device. */
	err = create_db_partition(hd, bdev, first_sector, first_part_minor, ph);
	if (err != 1)
		goto out;
	db_first = hd->part[first_part_minor].start_sect;
	/* Check the backup privheads. */
	err = validate_privheads(bdev, ph, db_first);
	if (err != 1)
		goto out;
	/* Check the table of contents and its backups. */
	if (!(toc = (struct tocblock*)kmalloc(sizeof(*toc), GFP_KERNEL)))
		goto no_mem;
	err = validate_tocblocks(bdev, toc, db_first);
	if (err != 1)
		goto out;
	/* Check the vmdb. */
	if (!(vm = (struct vmdb*)kmalloc(sizeof(*vm), GFP_KERNEL)))
		goto no_mem;
	err = validate_vmdb(bdev, vm, db_first);
	if (err != 1)
		goto out;
	/* Find the object id for @dev in the LDM database. */
	if (!(dk = (struct ldmdisk*)kmalloc(sizeof(*dk), GFP_KERNEL)))
		goto no_mem;
	err = get_disk_objid(bdev, vm, ph, dk, db_first);
	if (err != 1)
		goto out;
	/* Finally, create the data partition devices. */
	err = create_data_partitions(hd, first_sector, first_part_minor +
			LDM_FIRST_PART_OFFSET, bdev, vm, ph, dk, db_first);
	if (err == 1)
		ldm_debug("Parsed LDM database successfully.\n");
out:
	kfree(ph);
	kfree(toc);
	kfree(vm);
	kfree(dk);
	return err;
no_mem:
	printk(LDM_CRIT "Not enough memory to allocate required buffers.\n");
	err = -1;
	goto out;
}


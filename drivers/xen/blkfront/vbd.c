/******************************************************************************
 * vbd.c
 * 
 * XenLinux virtual block-device driver (xvd).
 * 
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Modifications by Mark A. Williamson are (c) Intel Research Cambridge
 * Copyright (c) 2004-2005, Christian Limpach
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "block.h"
#include <linux/bitmap.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <xen/xen_pvonhvm.h>

#ifdef HAVE_XEN_PLATFORM_COMPAT_H
#include <xen/platform-compat.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
#define XENVBD_MAJOR 202
#endif

#define BLKIF_MAJOR(dev) ((dev)>>8)
#define BLKIF_MINOR(dev) ((dev) & 0xff)

#define EXT_SHIFT 28
#define EXTENDED (1<<EXT_SHIFT)
#define VDEV_IS_EXTENDED(dev) ((dev)&(EXTENDED))
#define BLKIF_MINOR_EXT(dev) ((dev)&(~EXTENDED))

struct xlbd_minor_state {
	unsigned int nr;
	unsigned long *bitmap;
	spinlock_t lock;
};

/*
 * For convenience we distinguish between ide, scsi and 'other' (i.e.,
 * potentially combinations of the two) in the naming scheme and in a few other
 * places.
 */

#define NUM_IDE_MAJORS 10
#define NUM_SD_MAJORS 16
#define NUM_VBD_MAJORS 2

struct xlbd_type_info
{
	int partn_shift;
	int disks_per_major;
	char *devname;
	char *diskname;
};

static const struct xlbd_type_info xlbd_ide_type = {
	.partn_shift = 6,
	.disks_per_major = 2,
	.devname = "ide",
	.diskname = "hd",
};

static const struct xlbd_type_info xlbd_sd_type = {
	.partn_shift = 4,
	.disks_per_major = 16,
	.devname = "sd",
	.diskname = "sd",
};

static const struct xlbd_type_info xlbd_sr_type = {
	.partn_shift = 0,
	.disks_per_major = 256,
	.devname = "sr",
	.diskname = "sr",
};

static const struct xlbd_type_info xlbd_vbd_type = {
	.partn_shift = 4,
	.disks_per_major = 16,
	.devname = "xvd",
	.diskname = "xvd",
};

static const struct xlbd_type_info xlbd_vbd_type_ext = {
	.partn_shift = 8,
	.disks_per_major = 256,
	.devname = "xvd",
	.diskname = "xvd",
};

static struct xlbd_major_info *major_info[NUM_IDE_MAJORS + NUM_SD_MAJORS + 1 +
					 NUM_VBD_MAJORS];

#define XLBD_MAJOR_IDE_START	0
#define XLBD_MAJOR_SD_START	(NUM_IDE_MAJORS)
#define XLBD_MAJOR_SR_START	(NUM_IDE_MAJORS + NUM_SD_MAJORS)
#define XLBD_MAJOR_VBD_START	(NUM_IDE_MAJORS + NUM_SD_MAJORS + 1)

#define XLBD_MAJOR_IDE_RANGE	XLBD_MAJOR_IDE_START ... XLBD_MAJOR_SD_START - 1
#define XLBD_MAJOR_SD_RANGE	XLBD_MAJOR_SD_START ... XLBD_MAJOR_SR_START - 1
#define XLBD_MAJOR_SR_RANGE	XLBD_MAJOR_SR_START
#define XLBD_MAJOR_VBD_RANGE	XLBD_MAJOR_VBD_START ... XLBD_MAJOR_VBD_START + NUM_VBD_MAJORS - 1

#define XLBD_MAJOR_VBD_ALT(idx) ((idx) ^ XLBD_MAJOR_VBD_START ^ (XLBD_MAJOR_VBD_START + 1))

static const struct block_device_operations xlvbd_block_fops =
{
	.owner = THIS_MODULE,
	.open = blkif_open,
	.release = blkif_release,
	.ioctl  = blkif_ioctl,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
	.getgeo = blkif_getgeo
#endif
};

static struct xlbd_major_info *
xlbd_alloc_major_info(int major, int minor, int index)
{
	struct xlbd_major_info *ptr;
	struct xlbd_minor_state *minors;
	int do_register;

	ptr = kzalloc(sizeof(struct xlbd_major_info), GFP_KERNEL);
	if (ptr == NULL)
		return NULL;

	ptr->major = major;
	minors = kmalloc(sizeof(*minors), GFP_KERNEL);
	if (minors == NULL) {
		kfree(ptr);
		return NULL;
	}

	minors->bitmap = kzalloc(BITS_TO_LONGS(256) * sizeof(*minors->bitmap),
				 GFP_KERNEL);
	if (minors->bitmap == NULL) {
		kfree(minors);
		kfree(ptr);
		return NULL;
	}

	spin_lock_init(&minors->lock);
	minors->nr = 256;
	do_register = 1;

	switch (index) {
	case XLBD_MAJOR_IDE_RANGE:
		ptr->type = &xlbd_ide_type;
		ptr->index = index - XLBD_MAJOR_IDE_START;
		break;
	case XLBD_MAJOR_SD_RANGE:
		ptr->type = &xlbd_sd_type;
		ptr->index = index - XLBD_MAJOR_SD_START;
		break;
	case XLBD_MAJOR_SR_RANGE:
		ptr->type = &xlbd_sr_type;
		ptr->index = index - XLBD_MAJOR_SR_START;
		break;
	case XLBD_MAJOR_VBD_RANGE:
		ptr->index = 0;
		if ((index - XLBD_MAJOR_VBD_START) == 0)
			ptr->type = &xlbd_vbd_type;
		else
			ptr->type = &xlbd_vbd_type_ext;

		/* 
		 * if someone already registered block major XENVBD_MAJOR,
		 * don't try to register it again
		 */
		if (major_info[XLBD_MAJOR_VBD_ALT(index)] != NULL) {
			kfree(minors->bitmap);
			kfree(minors);
			minors = major_info[XLBD_MAJOR_VBD_ALT(index)]->minors;
			do_register = 0;
		}
		break;
	}

	if (do_register) {
		if (register_blkdev(ptr->major, ptr->type->devname)) {
			kfree(minors->bitmap);
			kfree(minors);
			kfree(ptr);
			return NULL;
		}

		pr_info("xen-vbd: registered block device major %i\n",
			ptr->major);
	}

	ptr->minors = minors;
	major_info[index] = ptr;
	return ptr;
}

static struct xlbd_major_info *
xlbd_get_major_info(int major, int minor, int vdevice)
{
	struct xlbd_major_info *mi;
	int index;

	switch (major) {
	case IDE0_MAJOR: index = 0; break;
	case IDE1_MAJOR: index = 1; break;
	case IDE2_MAJOR: index = 2; break;
	case IDE3_MAJOR: index = 3; break;
	case IDE4_MAJOR: index = 4; break;
	case IDE5_MAJOR: index = 5; break;
	case IDE6_MAJOR: index = 6; break;
	case IDE7_MAJOR: index = 7; break;
	case IDE8_MAJOR: index = 8; break;
	case IDE9_MAJOR: index = 9; break;
	case SCSI_DISK0_MAJOR: index = XLBD_MAJOR_SD_START; break;
	case SCSI_DISK1_MAJOR ... SCSI_DISK7_MAJOR:
		index = XLBD_MAJOR_SD_START + 1 + major - SCSI_DISK1_MAJOR;
		break;
	case SCSI_DISK8_MAJOR ... SCSI_DISK15_MAJOR:
		index = XLBD_MAJOR_SD_START + 8 + major - SCSI_DISK8_MAJOR;
		break;
	case SCSI_CDROM_MAJOR:
		index = XLBD_MAJOR_SR_START;
		break;
	case XENVBD_MAJOR:
		index = XLBD_MAJOR_VBD_START + !!VDEV_IS_EXTENDED(vdevice);
		break;
	default:
		return NULL;
	}

	mi = ((major_info[index] != NULL) ? major_info[index] :
	      xlbd_alloc_major_info(major, minor, index));
	if (mi)
		mi->usage++;
	return mi;
}

static void
xlbd_put_major_info(struct xlbd_major_info *mi)
{
	mi->usage--;
	/* XXX: release major if 0 */
}

void __exit
xlbd_release_major_info(void)
{
	unsigned int i;
	int vbd_done = 0;

	for (i = 0; i < ARRAY_SIZE(major_info); ++i) {
		struct xlbd_major_info *mi = major_info[i];

		if (!mi)
			continue;
		if (mi->usage)
			pr_warning("vbd: major %u still in use (%u times)\n",
				   mi->major, mi->usage);
		if (mi->major != XENVBD_MAJOR || !vbd_done) {
			unregister_blkdev(mi->major, mi->type->devname);
			kfree(mi->minors->bitmap);
			kfree(mi->minors);
		}
		if (mi->major == XENVBD_MAJOR)
			vbd_done = 1;
		kfree(mi);
	}
}

static int
xlbd_reserve_minors(struct xlbd_major_info *mi, unsigned int minor,
		    unsigned int nr_minors)
{
	struct xlbd_minor_state *ms = mi->minors;
	unsigned int end = minor + nr_minors;
	int rc;

	if (end > ms->nr) {
		unsigned long *bitmap, *old;

		bitmap = kcalloc(BITS_TO_LONGS(end), sizeof(*bitmap),
				 GFP_KERNEL);
		if (bitmap == NULL)
			return -ENOMEM;

		spin_lock(&ms->lock);
		if (end > ms->nr) {
			old = ms->bitmap;
			memcpy(bitmap, ms->bitmap,
			       BITS_TO_LONGS(ms->nr) * sizeof(*bitmap));
			ms->bitmap = bitmap;
			ms->nr = BITS_TO_LONGS(end) * BITS_PER_LONG;
		} else
			old = bitmap;
		spin_unlock(&ms->lock);
		kfree(old);
	}

	spin_lock(&ms->lock);
	if (find_next_bit(ms->bitmap, end, minor) >= end) {
		bitmap_set(ms->bitmap, minor, nr_minors);
		rc = 0;
	} else
		rc = -EBUSY;
	spin_unlock(&ms->lock);

	return rc;
}

static void
xlbd_release_minors(struct xlbd_major_info *mi, unsigned int minor,
		    unsigned int nr_minors)
{
	struct xlbd_minor_state *ms = mi->minors;

	BUG_ON(minor + nr_minors > ms->nr);
	spin_lock(&ms->lock);
	bitmap_clear(ms->bitmap, minor, nr_minors);
	spin_unlock(&ms->lock);
}

static char *encode_disk_name(char *ptr, unsigned int n)
{
	if (n >= 26)
		ptr = encode_disk_name(ptr, n / 26 - 1);
	*ptr = 'a' + n % 26;
	return ptr + 1;
}

static int
xlvbd_init_blk_queue(struct gendisk *gd, unsigned int sector_size,
		     unsigned int physical_sector_size,
		     struct blkfront_info *info)
{
	struct request_queue *rq;

	rq = blk_init_queue(do_blkif_request, &info->io_lock);
	if (rq == NULL)
		return -1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
	queue_flag_set_unlocked(QUEUE_FLAG_VIRT, rq);
#endif

	if (info->feature_discard) {
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, rq);
		blk_queue_max_discard_sectors(rq, get_capacity(gd));
		rq->limits.discard_granularity = info->discard_granularity;
		rq->limits.discard_alignment = info->discard_alignment;
		if (info->feature_secdiscard)
			queue_flag_set_unlocked(QUEUE_FLAG_SECDISCARD, rq);
	}

	/* Hard sector size and max sectors impersonate the equiv. hardware. */
	blk_queue_logical_block_size(rq, sector_size);
	blk_queue_physical_block_size(rq, physical_sector_size);
	blk_queue_max_hw_sectors(rq, 512);

	/* Each segment in a request is up to an aligned page in size. */
	blk_queue_segment_boundary(rq, PAGE_SIZE - 1);
	blk_queue_max_segment_size(rq, PAGE_SIZE);

	/* Ensure a merged request will fit in a single I/O ring slot. */
	blk_queue_max_segments(rq, info->max_segs_per_req);

	/* Make sure buffer addresses are sector-aligned. */
	blk_queue_dma_alignment(rq, 511);

	/* Make sure we don't use bounce buffers. */
	blk_queue_bounce_limit(rq, BLK_BOUNCE_ANY);

	gd->queue = rq;
	info->rq = rq;

	return 0;
}

static inline bool
xlvbd_ide_unplugged(int major)
{
#ifndef CONFIG_XEN
	/*
	 * For HVM guests:
	 * - pv driver required if booted with xen_emul_unplug=ide-disks|all
	 * - native driver required with xen_emul_unplug=nics|never
	 */
	if (xen_pvonhvm_unplugged_disks)
		return true;

	switch (major) {
	case IDE0_MAJOR:
	case IDE1_MAJOR:
	case IDE2_MAJOR:
	case IDE3_MAJOR:
	case IDE4_MAJOR:
	case IDE5_MAJOR:
	case IDE6_MAJOR:
	case IDE7_MAJOR:
	case IDE8_MAJOR:
	case IDE9_MAJOR:
		return false;
	default:
		break;
	}
#endif
	return true;
}

int
xlvbd_add(blkif_sector_t capacity, int vdevice, unsigned int vdisk_info,
	  unsigned int sector_size, unsigned int physical_sector_size,
	  struct blkfront_info *info)
{
	int major, minor;
	struct gendisk *gd;
	struct xlbd_major_info *mi;
	int nr_minors = 1;
	int err = -ENODEV;
	char *ptr;
	unsigned int offset;

	if ((vdevice>>EXT_SHIFT) > 1) {
		/* this is above the extended range; something is wrong */
		pr_warning("blkfront: vdevice %#x is above the extended range;"
			   " ignoring\n", vdevice);
		return -ENODEV;
	}

	if (!VDEV_IS_EXTENDED(vdevice)) {
		major = BLKIF_MAJOR(vdevice);
		minor = BLKIF_MINOR(vdevice);
	}
	else {
		major = XENVBD_MAJOR;
		minor = BLKIF_MINOR_EXT(vdevice);
		if (minor >> MINORBITS) {
			pr_warning("blkfront: %#x's minor (%#x) out of range;"
				   " ignoring\n", vdevice, minor);
			return -ENODEV;
		}
	}
	if (!xlvbd_ide_unplugged(major)) {
		pr_warning("blkfront: skipping IDE major on %x, native driver required\n", vdevice);
		return -ENODEV;
	}

	BUG_ON(info->gd != NULL);
	BUG_ON(info->mi != NULL);
	BUG_ON(info->rq != NULL);

	mi = xlbd_get_major_info(major, minor, vdevice);
	if (mi == NULL)
		goto out;
	info->mi = mi;

	if ((vdisk_info & VDISK_CDROM) ||
	    !(minor & ((1 << mi->type->partn_shift) - 1)))
		nr_minors = 1 << mi->type->partn_shift;

	err = xlbd_reserve_minors(mi, minor & ~(nr_minors - 1), nr_minors);
	if (err)
		goto out;
	err = -ENODEV;

	gd = alloc_disk(vdisk_info & VDISK_CDROM ? 1 : nr_minors);
	if (gd == NULL)
		goto release;

	strcpy(gd->disk_name, mi->type->diskname);
	ptr = gd->disk_name + strlen(mi->type->diskname);
	offset = mi->index * mi->type->disks_per_major +
		 (minor >> mi->type->partn_shift);
	if (mi->type->partn_shift) {
		ptr = encode_disk_name(ptr, offset);
		offset = minor & ((1 << mi->type->partn_shift) - 1);
	} else
		gd->flags |= GENHD_FL_CD;
	BUG_ON(ptr >= gd->disk_name + ARRAY_SIZE(gd->disk_name));
	if (nr_minors > 1)
		*ptr = 0;
	else
		snprintf(ptr, gd->disk_name + ARRAY_SIZE(gd->disk_name) - ptr,
			 "%u", offset);

	gd->major = mi->major;
	gd->first_minor = minor;
	gd->fops = &xlvbd_block_fops;
	gd->private_data = info;
	gd->driverfs_dev = &(info->xbdev->dev);
	set_capacity(gd, capacity);

	if (xlvbd_init_blk_queue(gd, sector_size, physical_sector_size, info)) {
		del_gendisk(gd);
		goto release;
	}

	info->gd = gd;

	xlvbd_flush(info);

	if (vdisk_info & VDISK_READONLY)
		set_disk_ro(gd, 1);

	if (vdisk_info & VDISK_REMOVABLE)
		gd->flags |= GENHD_FL_REMOVABLE;

	if (vdisk_info & VDISK_CDROM)
		gd->flags |= GENHD_FL_CD;

	return 0;

 release:
	xlbd_release_minors(mi, minor, nr_minors);
 out:
	if (mi)
		xlbd_put_major_info(mi);
	info->mi = NULL;
	return err;
}

void
xlvbd_del(struct blkfront_info *info)
{
	unsigned int minor, nr_minors;

	if (info->mi == NULL)
		return;

	BUG_ON(info->gd == NULL);
	minor = info->gd->first_minor;
	nr_minors = (info->gd->flags & GENHD_FL_CD)
		    || !(minor & ((1 << info->mi->type->partn_shift) - 1))
		    ? 1 << info->mi->type->partn_shift : 1;
	del_gendisk(info->gd);
	put_disk(info->gd);
	info->gd = NULL;

	xlbd_release_minors(info->mi, minor & ~(nr_minors - 1), nr_minors);
	xlbd_put_major_info(info->mi);
	info->mi = NULL;

	BUG_ON(info->rq == NULL);
	blk_cleanup_queue(info->rq);
	info->rq = NULL;
}

void
xlvbd_flush(struct blkfront_info *info)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	blk_queue_flush(info->rq, info->feature_flush);
	pr_info("blkfront: %s: %s: %s\n",
		info->gd->disk_name,
		info->flush_op == BLKIF_OP_WRITE_BARRIER ?
		"barrier" : (info->flush_op == BLKIF_OP_FLUSH_DISKCACHE ?
			     "flush diskcache" : "barrier or flush"),
		info->feature_flush ? "enabled" : "disabled");
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
	int err;
	const char *barrier;

	switch (info->feature_flush) {
	case QUEUE_ORDERED_DRAIN:	barrier = "enabled (drain)"; break;
	case QUEUE_ORDERED_TAG:		barrier = "enabled (tag)"; break;
	case QUEUE_ORDERED_NONE:	barrier = "disabled"; break;
	default:			return -EINVAL;
	}

	err = blk_queue_ordered(info->rq, info->feature_flush);
	if (err)
		return err;
	pr_info("blkfront: %s: barriers %s\n",
		info->gd->disk_name, barrier);
#else
	if (info->feature_flush)
		pr_info("blkfront: %s: barriers disabled\n", info->gd->disk_name);
#endif
}

#ifdef CONFIG_SYSFS
static ssize_t show_media(struct device *dev,
		                  struct device_attribute *attr, char *buf)
{
	struct xenbus_device *xendev = to_xenbus_device(dev);
	struct blkfront_info *info = dev_get_drvdata(&xendev->dev);

	if (info->gd->flags & GENHD_FL_CD)
		return sprintf(buf, "cdrom\n");
	return sprintf(buf, "disk\n");
}

static struct device_attribute xlvbd_attrs[] = {
	__ATTR(media, S_IRUGO, show_media, NULL),
};

int xlvbd_sysfs_addif(struct blkfront_info *info)
{
	int i;
	int error = 0;

	for (i = 0; i < ARRAY_SIZE(xlvbd_attrs); i++) {
		error = device_create_file(info->gd->driverfs_dev,
				&xlvbd_attrs[i]);
		if (error)
			goto fail;
	}
	return 0;

fail:
	while (--i >= 0)
		device_remove_file(info->gd->driverfs_dev, &xlvbd_attrs[i]);
	return error;
}

void xlvbd_sysfs_delif(struct blkfront_info *info)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xlvbd_attrs); i++)
		device_remove_file(info->gd->driverfs_dev, &xlvbd_attrs[i]);
}

#endif /* CONFIG_SYSFS */

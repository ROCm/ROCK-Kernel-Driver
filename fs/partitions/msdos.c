/*
 *  fs/partitions/msdos.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 *
 *  Support for DiskManager v6.0x added by Mark Lord,
 *  with information provided by OnTrack.  This now works for linux fdisk
 *  and LILO, as well as loadlin and bootln.  Note that disks other than
 *  /dev/hda *must* have a "DOS" type 0x51 partition in the first slot (hda1).
 *
 *  More flexible handling of extended partitions - aeb, 950831
 *
 *  Check partition table on IDE disks for common CHS translations
 *
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>

#ifdef CONFIG_BLK_DEV_IDE
#include <linux/ide.h>	/* IDE xlate */
#endif /* CONFIG_BLK_DEV_IDE */

#include <asm/system.h>

#include "check.h"
#include "msdos.h"

#if CONFIG_BLK_DEV_MD && CONFIG_AUTODETECT_RAID
extern void md_autodetect_dev(kdev_t dev);
#endif

static int current_minor;

/*
 * Many architectures don't like unaligned accesses, which is
 * frequently the case with the nr_sects and start_sect partition
 * table entries.
 */
#include <asm/unaligned.h>

#define SYS_IND(p)	(get_unaligned(&p->sys_ind))
#define NR_SECTS(p)	({ __typeof__(p->nr_sects) __a =	\
				get_unaligned(&p->nr_sects);	\
				le32_to_cpu(__a); \
			})

#define START_SECT(p)	({ __typeof__(p->start_sect) __a =	\
				get_unaligned(&p->start_sect);	\
				le32_to_cpu(__a); \
			})

static inline int is_extended_partition(struct partition *p)
{
	return (SYS_IND(p) == DOS_EXTENDED_PARTITION ||
		SYS_IND(p) == WIN98_EXTENDED_PARTITION ||
		SYS_IND(p) == LINUX_EXTENDED_PARTITION);
}

/*
 * Create devices for each logical partition in an extended partition.
 * The logical partitions form a linked list, with each entry being
 * a partition table with two entries.  The first entry
 * is the real data partition (with a start relative to the partition
 * table start).  The second is a pointer to the next logical partition
 * (with a start relative to the entire extended partition).
 * We do not create a Linux partition for the partition tables, but
 * only for the actual data partitions.
 */

static void extended_partition(struct gendisk *hd, kdev_t dev)
{
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector, first_size, this_sector, this_size;
	int mask = (1 << hd->minor_shift) - 1;
	int sector_size = get_hardsect_size(dev) / 512;
	int loopct = 0;		/* number of links followed
				   without finding a data partition */
	int i;

	first_sector = hd->part[MINOR(dev)].start_sect;
	first_size = hd->part[MINOR(dev)].nr_sects;
	this_sector = first_sector;

	while (1) {
		if (++loopct > 100)
			return;
		if ((current_minor & mask) == 0)
			return;
		if (!(bh = bread(dev,0,get_ptable_blocksize(dev))))
			return;

		if ((*(__u16 *) (bh->b_data+510)) != cpu_to_le16(MSDOS_LABEL_MAGIC))
			goto done;

		p = (struct partition *) (0x1BE + bh->b_data);

		this_size = hd->part[MINOR(dev)].nr_sects;

		/*
		 * Usually, the first entry is the real data partition,
		 * the 2nd entry is the next extended partition, or empty,
		 * and the 3rd and 4th entries are unused.
		 * However, DRDOS sometimes has the extended partition as
		 * the first entry (when the data partition is empty),
		 * and OS/2 seems to use all four entries.
		 */

		/* 
		 * First process the data partition(s)
		 */
		for (i=0; i<4; i++, p++) {
			if (!NR_SECTS(p) || is_extended_partition(p))
				continue;

			/* Check the 3rd and 4th entries -
			   these sometimes contain random garbage */
			if (i >= 2
				&& START_SECT(p) + NR_SECTS(p) > this_size
				&& (this_sector + START_SECT(p) < first_sector ||
				    this_sector + START_SECT(p) + NR_SECTS(p) >
				     first_sector + first_size))
				continue;

			add_gd_partition(hd, current_minor,
					 this_sector+START_SECT(p)*sector_size,
					 NR_SECTS(p)*sector_size);
#if CONFIG_BLK_DEV_MD && CONFIG_AUTODETECT_RAID
			if (SYS_IND(p) == LINUX_RAID_PARTITION) {
			    md_autodetect_dev(MKDEV(hd->major,current_minor));
			}
#endif

			current_minor++;
			loopct = 0;
			if ((current_minor & mask) == 0)
				goto done;
		}
		/*
		 * Next, process the (first) extended partition, if present.
		 * (So far, there seems to be no reason to make
		 *  extended_partition()  recursive and allow a tree
		 *  of extended partitions.)
		 * It should be a link to the next logical partition.
		 * Create a minor for this just long enough to get the next
		 * partition table.  The minor will be reused for the next
		 * data partition.
		 */
		p -= 4;
		for (i=0; i<4; i++, p++)
			if(NR_SECTS(p) && is_extended_partition(p))
				break;
		if (i == 4)
			goto done;	 /* nothing left to do */

		hd->part[current_minor].nr_sects = NR_SECTS(p) * sector_size; /* JSt */
		hd->part[current_minor].start_sect = first_sector + START_SECT(p) * sector_size;
		this_sector = first_sector + START_SECT(p) * sector_size;
		dev = MKDEV(hd->major, current_minor);

		/* Use bforget(), as we have changed the disk geometry */
		bforget(bh);
	}
done:
	bforget(bh);
}

static inline struct buffer_head *
get_partition_table_block(struct gendisk *hd, int minor, int blocknr) {
	kdev_t dev = MKDEV(hd->major, minor);
	return bread(dev, blocknr, get_ptable_blocksize(dev));
}

#ifdef CONFIG_SOLARIS_X86_PARTITION

/* james@bpgc.com: Solaris has a nasty indicator: 0x82 which also
   indicates linux swap.  Be careful before believing this is Solaris. */

static void
solaris_x86_partition(struct gendisk *hd, int minor) {
	long offset = hd->part[minor].start_sect;

	struct buffer_head *bh;
	struct solaris_x86_vtoc *v;
	struct solaris_x86_slice *s;
	int i;
	char buf[40];

	if(!(bh = get_partition_table_block(hd, minor, 0)))
		return;
	v = (struct solaris_x86_vtoc *)(bh->b_data + 512);
	if(v->v_sanity != SOLARIS_X86_VTOC_SANE) {
		brelse(bh);
		return;
	}
	printk(" %s: <solaris:", disk_name(hd, minor, buf));
	if(v->v_version != 1) {
		printk("  cannot handle version %ld vtoc>\n", v->v_version);
		brelse(bh);
		return;
	}
	for(i=0; i<SOLARIS_X86_NUMSLICE; i++) {
		s = &v->v_slice[i];

		if (s->s_size == 0)
			continue;
		printk(" [s%d]", i);
		/* solaris partitions are relative to current MS-DOS
		 * one but add_gd_partition starts relative to sector
		 * zero of the disk.  Therefore, must add the offset
		 * of the current partition */
		add_gd_partition(hd, current_minor, s->s_start+offset, s->s_size);
		current_minor++;
	}
	brelse(bh);
	printk(" >\n");
}
#endif

#ifdef CONFIG_BSD_DISKLABEL
static void
check_and_add_bsd_partition(struct gendisk *hd,
			    struct bsd_partition *bsd_p, int minor) {
	struct hd_struct *lin_p;
		/* check relative position of partitions.  */
	for (lin_p = hd->part + 1 + minor;
	     lin_p - hd->part - minor < current_minor; lin_p++) {
			/* no relationship -> try again */
		if (lin_p->start_sect + lin_p->nr_sects <= bsd_p->p_offset ||
		    lin_p->start_sect >= bsd_p->p_offset + bsd_p->p_size)
			continue;	
			/* equal -> no need to add */
		if (lin_p->start_sect == bsd_p->p_offset && 
			lin_p->nr_sects == bsd_p->p_size) 
			return;
			/* bsd living within dos partition */
		if (lin_p->start_sect <= bsd_p->p_offset && lin_p->start_sect 
			+ lin_p->nr_sects >= bsd_p->p_offset + bsd_p->p_size) {
#ifdef DEBUG_BSD_DISKLABEL
			printk("w: %d %ld+%ld,%d+%d", 
				lin_p - hd->part, 
				lin_p->start_sect, lin_p->nr_sects, 
				bsd_p->p_offset, bsd_p->p_size);
#endif
			break;
		}
	 /* ouch: bsd and linux overlap. Don't even try for that partition */
#ifdef DEBUG_BSD_DISKLABEL
		printk("???: %d %ld+%ld,%d+%d",
			lin_p - hd->part, lin_p->start_sect, lin_p->nr_sects,
			bsd_p->p_offset, bsd_p->p_size);
#endif
		printk("???");
		return;
	} /* if the bsd partition is not currently known to linux, we end
	   * up here 
	   */
	add_gd_partition(hd, current_minor, bsd_p->p_offset, bsd_p->p_size);
	current_minor++;
}

/* 
 * Create devices for BSD partitions listed in a disklabel, under a
 * dos-like partition. See extended_partition() for more information.
 */
static void bsd_disklabel_partition(struct gendisk *hd, int minor, int type) {
	struct buffer_head *bh;
	struct bsd_disklabel *l;
	struct bsd_partition *p;
	int max_partitions;
	int mask = (1 << hd->minor_shift) - 1;
	char buf[40];

	if (!(bh = get_partition_table_block(hd, minor, 0)))
		return;
	l = (struct bsd_disklabel *) (bh->b_data+512);
	if (l->d_magic != BSD_DISKMAGIC) {
		brelse(bh);
		return;
	}
	printk(" %s:", disk_name(hd, minor, buf));
	printk((type == OPENBSD_PARTITION) ? " <openbsd:" :
	       (type == NETBSD_PARTITION) ? " <netbsd:" : " <bsd:");

	max_partitions = ((type == OPENBSD_PARTITION) ? OPENBSD_MAXPARTITIONS
			                              : BSD_MAXPARTITIONS);
	if (l->d_npartitions < max_partitions)
		max_partitions = l->d_npartitions;
	for (p = l->d_partitions; p - l->d_partitions <  max_partitions; p++) {
		if ((current_minor & mask) >= (4 + hd->max_p))
			break;

		if (p->p_fstype != BSD_FS_UNUSED) 
			check_and_add_bsd_partition(hd, p, minor);
	}

	/* Use bforget(), as we have changed the disk setup */
	bforget(bh);

	printk(" >\n");
}
#endif

#ifdef CONFIG_UNIXWARE_DISKLABEL
/*
 * Create devices for Unixware partitions listed in a disklabel, under a
 * dos-like partition. See extended_partition() for more information.
 */
static void unixware_partition(struct gendisk *hd, int minor) {
	struct buffer_head *bh;
	struct unixware_disklabel *l;
	struct unixware_slice *p;
	int mask = (1 << hd->minor_shift) - 1;
	char buf[40];

	if (!(bh = get_partition_table_block(hd, minor, 14)))
		return;
	l = (struct unixware_disklabel *) (bh->b_data+512);
	if (le32_to_cpu(l->d_magic) != UNIXWARE_DISKMAGIC ||
	    le32_to_cpu(l->vtoc.v_magic) != UNIXWARE_DISKMAGIC2) {
		brelse(bh);
		return;
	}
	printk(" %s: <unixware:", disk_name(hd, minor, buf));
	p = &l->vtoc.v_slice[1];
	/* I omit the 0th slice as it is the same as whole disk. */
	while (p - &l->vtoc.v_slice[0] < UNIXWARE_NUMSLICE) {
		if ((current_minor & mask) == 0)
			break;

		if (p->s_label != UNIXWARE_FS_UNUSED) {
			add_gd_partition(hd, current_minor, START_SECT(p),
					 NR_SECTS(p));
			current_minor++;
		}
		p++;
	}
	/* Use bforget, as we have changed the disk setup */
	bforget(bh);
	printk(" >\n");
}
#endif

int msdos_partition(struct gendisk *hd, kdev_t dev,
		    unsigned long first_sector, int first_part_minor) {
	int i, minor = current_minor = first_part_minor;
	struct buffer_head *bh;
	struct partition *p;
	unsigned char *data;
	int mask = (1 << hd->minor_shift) - 1;
	int sector_size = get_hardsect_size(dev) / 512;
#ifdef CONFIG_BLK_DEV_IDE
	int tested_for_xlate = 0;

read_mbr:
#endif /* CONFIG_BLK_DEV_IDE */
	if (!(bh = bread(dev,0,get_ptable_blocksize(dev)))) {
		if (warn_no_part) printk(" unable to read partition table\n");
		return -1;
	}
	data = bh->b_data;
#ifdef CONFIG_BLK_DEV_IDE
check_table:
#endif /* CONFIG_BLK_DEV_IDE */
	/* Use bforget(), because we may have changed the disk geometry */
	if (*(unsigned short *)  (0x1fe + data) != cpu_to_le16(MSDOS_LABEL_MAGIC)) {
		bforget(bh);
		return 0;
	}
	p = (struct partition *) (0x1be + data);

#ifdef CONFIG_BLK_DEV_IDE
	if (!tested_for_xlate++) {	/* Do this only once per disk */
		/*
		 * Look for various forms of IDE disk geometry translation
		 */
		unsigned int sig = le16_to_cpu(*(unsigned short *)(data + 2));
		int heads = 0;
		/*
		 * The i386 partition handling programs very often
		 * make partitions end on cylinder boundaries.
		 * There is no need to do so, and Linux fdisk doesnt always
		 * do this, and Windows NT on Alpha doesnt do this either,
		 * but still, this helps to guess #heads.
		 */
		for (i = 0; i < 4; i++) {
			struct partition *q = &p[i];
			if (NR_SECTS(q)) {
				if ((q->sector & 63) == 1 &&
				    (q->end_sector & 63) == 63)
					heads = q->end_head + 1;
				break;
			}
		}
		if (SYS_IND(p) == EZD_PARTITION) {
			/*
			 * Accesses to sector 0 must go to sector 1 instead.
			 */
			if (ide_xlate_1024(dev, -1, heads, " [EZD]")) {
				data += 512;
				goto check_table;
			}
		} else if (SYS_IND(p) == DM6_PARTITION) {

			/*
			 * Everything on the disk is offset by 63 sectors,
			 * including a "new" MBR with its own partition table.
			 */
			if (ide_xlate_1024(dev, 1, heads, " [DM6:DDO]")) {
				bforget(bh);
				goto read_mbr;	/* start over with new MBR */
			}
		} else if (sig <= 0x1ae &&
			   data[sig] == 0xAA && data[sig+1] == 0x55 &&
			   (data[sig+2] & 1)) {
			/* DM6 signature in MBR, courtesy of OnTrack */
			(void) ide_xlate_1024 (dev, 0, heads, " [DM6:MBR]");
		} else if (SYS_IND(p) == DM6_AUX1PARTITION ||
			   SYS_IND(p) == DM6_AUX3PARTITION) {
			/*
			 * DM6 on other than the first (boot) drive
			 */
			(void) ide_xlate_1024(dev, 0, heads, " [DM6:AUX]");
		} else {
			(void) ide_xlate_1024(dev, 2, heads, " [PTBL]");
		}
	}
#endif /* CONFIG_BLK_DEV_IDE */

	/* Look for partitions in two passes:
	   First find the primary partitions, and the DOS-type extended partitions.
	   On the second pass look inside *BSD and Unixware and Solaris partitions. */

	current_minor += 4;  /* first "extra" minor (for extended partitions) */
	for (i=1 ; i<=4 ; minor++,i++,p++) {
		if (!NR_SECTS(p))
			continue;
		add_gd_partition(hd, minor, first_sector+START_SECT(p)*sector_size,
				 NR_SECTS(p)*sector_size);
#if CONFIG_BLK_DEV_MD && CONFIG_AUTODETECT_RAID
		if (SYS_IND(p) == LINUX_RAID_PARTITION) {
			md_autodetect_dev(MKDEV(hd->major,minor));
		}
#endif
		if (is_extended_partition(p)) {
			printk(" <");
			/*
			 * If we are rereading the partition table, we need
			 * to set the size of the partition so that we will
			 * be able to bread the block containing the extended
			 * partition info.
			 */
			hd->sizes[minor] = hd->part[minor].nr_sects 
			  	>> (BLOCK_SIZE_BITS - 9);
			extended_partition(hd, MKDEV(hd->major, minor));
			printk(" >");
			/* prevent someone doing mkfs or mkswap on an
			   extended partition, but leave room for LILO */
			if (hd->part[minor].nr_sects > 2)
				hd->part[minor].nr_sects = 2;
		}
	}

	/*
	 *  Check for old-style Disk Manager partition table
	 */
	if (*(unsigned short *) (data+0xfc) == cpu_to_le16(MSDOS_LABEL_MAGIC)) {
		p = (struct partition *) (0x1be + data);
		for (i = 4 ; i < 16 ; i++, current_minor++) {
			p--;
			if ((current_minor & mask) == 0)
				break;
			if (!(START_SECT(p) && NR_SECTS(p)))
				continue;
			add_gd_partition(hd, current_minor, START_SECT(p), NR_SECTS(p));
		}
	}
	printk("\n");

	/* second pass - output for each on a separate line */
	minor -= 4;
	p = (struct partition *) (0x1be + data);
	for (i=1 ; i<=4 ; minor++,i++,p++) {
		if (!NR_SECTS(p))
			continue;
#ifdef CONFIG_BSD_DISKLABEL
		if (SYS_IND(p) == BSD_PARTITION ||
		    SYS_IND(p) == NETBSD_PARTITION ||
		    SYS_IND(p) == OPENBSD_PARTITION)
			bsd_disklabel_partition(hd, minor, SYS_IND(p));
#endif
#ifdef CONFIG_UNIXWARE_DISKLABEL
		if (SYS_IND(p) == UNIXWARE_PARTITION)
			unixware_partition(hd, minor);
#endif
#ifdef CONFIG_SOLARIS_X86_PARTITION
		if(SYS_IND(p) == SOLARIS_X86_PARTITION)
			solaris_x86_partition(hd, minor);
#endif
	}

	bforget(bh);
	return 1;
}

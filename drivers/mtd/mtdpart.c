/*
 * Simple MTD partitioning layer
 *
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 *
 * This code is GPL
 *
 * $Id: mtdpart.c,v 1.7 2000/12/09 23:29:47 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/list.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>


/* Our partition linked list */
static LIST_HEAD(mtd_partitions);

/* Our partition node structure */
struct mtd_part {
	struct mtd_info mtd;
	struct mtd_info *master;
	loff_t offset;
	int index;
	struct list_head list;
};

/*
 * Given a pointer to the MTD object in the mtd_part structure, we can retrieve
 * the pointer to that structure with this macro.
 */
#define PART(x)  ((struct mtd_part *)(x))

	
/* 
 * MTD methods which simply translate the effective address and pass through
 * to the _real_ device.
 */

static int part_read (struct mtd_info *mtd, loff_t from, size_t len, 
			size_t *retlen, u_char *buf)
{
	struct mtd_part *part = PART(mtd);
	if (from >= mtd->size)
		len = 0;
	else if (from + len > mtd->size)
		len = mtd->size - from;
	return part->master->read (part->master, from + part->offset, 
					len, retlen, buf);
}

static int part_write (struct mtd_info *mtd, loff_t to, size_t len,
			size_t *retlen, const u_char *buf)
{
	struct mtd_part *part = PART(mtd);
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (to >= mtd->size)
		len = 0;
	else if (to + len > mtd->size)
		len = mtd->size - to;
	return part->master->write (part->master, to + part->offset, 
					len, retlen, buf);
}

static int part_writev (struct mtd_info *mtd,  const struct iovec *vecs,
			 unsigned long count, loff_t to, size_t *retlen)
{
	struct mtd_part *part = PART(mtd);
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	return part->master->writev (part->master, vecs, count,
					to + part->offset, retlen);
}

static int part_readv (struct mtd_info *mtd,  struct iovec *vecs,
			 unsigned long count, loff_t from, size_t *retlen)
{
	struct mtd_part *part = PART(mtd);
	return part->master->readv (part->master, vecs, count,
					from + part->offset, retlen);
}

static int part_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	struct mtd_part *part = PART(mtd);
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (instr->addr >= mtd->size)
		return -EINVAL;
	instr->addr += part->offset;
	return part->master->erase(part->master, instr);
}

static int part_lock (struct mtd_info *mtd, loff_t ofs, size_t len)
{
	struct mtd_part *part = PART(mtd);
	return part->master->lock(part->master, ofs + part->offset, len);
}

static int part_unlock (struct mtd_info *mtd, loff_t ofs, size_t len)
{
	struct mtd_part *part = PART(mtd);
	return part->master->unlock(part->master, ofs + part->offset, len);
}


/* 
 * This function unregisters and destroy all slave MTD objects which are 
 * attached to the given master MTD object.
 */

int del_mtd_partitions(struct mtd_info *master)
{
	struct list_head *node;
	struct mtd_part *slave;

	for (node = mtd_partitions.next;
	     node != &mtd_partitions;
	     node = node->next) {
		slave = list_entry(node, struct mtd_part, list);
		if (slave->master == master) {
			struct list_head *prev = node->prev;
			__list_del(prev, node->next);
			del_mtd_device(&slave->mtd);
			kfree(slave);
			node = prev;
			MOD_DEC_USE_COUNT;
		}
	}

	return 0;
}


/*
 * This function, given a master MTD object and a partition table, creates
 * and registers slave MTD objects which are bound to the master according to
 * the partition definitions.
 * (Q: should we register the master MTD object as well?)
 */

int add_mtd_partitions(struct mtd_info *master, 
		       struct mtd_partition *parts,
		       int nbparts)
{
	struct mtd_part *slave;
	u_long cur_offset = 0;
	int i;

	for (i = 0; i < nbparts; i++) {
		/* allocate the partition structure */
		slave = kmalloc (sizeof(*slave), GFP_KERNEL);
		if (!slave) {
			printk ("memory allocation error while creating partitions for \"%s\"\n",
				master->name);
			del_mtd_partitions(master);
			return -ENOMEM;
		}
		list_add(&slave->list, &mtd_partitions);

		/* set up the MTD object for this partition */
		slave->mtd = *master;
		slave->mtd.name = parts[i].name;
		slave->mtd.size = parts[i].size;
		slave->mtd.flags &= ~parts[i].mask_flags;
		slave->mtd.read = part_read;
		slave->mtd.write = part_write;
		if (slave->mtd.writev)
			slave->mtd.writev = part_writev;
		if (slave->mtd.readv)
			slave->mtd.readv = part_readv;
		if (slave->mtd.lock)
			slave->mtd.lock = part_lock;
		if (slave->mtd.unlock)
			slave->mtd.unlock = part_unlock;
		slave->mtd.erase = part_erase;
		slave->master = master;
		slave->offset = parts[i].offset;
		slave->index = i;

		if (slave->offset == 0)
			slave->offset = cur_offset;
		if (slave->mtd.size == 0)
			slave->mtd.size = master->size - slave->offset;
		cur_offset = slave->offset + slave->mtd.size;

		/* let's do some sanity checks */
		if ((slave->mtd.flags & MTD_WRITEABLE) && 
		    (parts[i].offset % master->erasesize)) {
			slave->mtd.flags &= ~MTD_WRITEABLE;
			printk ("mtd: partition \"%s\" doesn't start on an erase block boundary -- force read-only\n",
					parts[i].name);
		}
		if ((slave->mtd.flags & MTD_WRITEABLE) && 
		    (parts[i].size % master->erasesize)) {
			slave->mtd.flags &= ~MTD_WRITEABLE;
			printk ("mtd: partition \"%s\" doesn't end on an erase block -- force read-only\n",
					parts[i].name);
		}
		if (parts[i].offset >= master->size) {
			/* let's register it anyway to preserve ordering */
			slave->offset = 0;
			slave->mtd.size = 0;
			printk ("mtd: partition \"%s\" is out of reach -- disabled\n",
					parts[i].name);
		}
		if (parts[i].offset + parts[i].size > master->size) {
			slave->mtd.size = master->size - parts[i].offset;
			printk ("mtd: partition \"%s\" extends beyond the end of device \"%s\" -- size truncated to %#lx\n",
					parts[i].name, master->name, slave->mtd.size);
		}

		/* register our partition */
		add_mtd_device(&slave->mtd);
		MOD_INC_USE_COUNT;
	}

	return 0;
}

EXPORT_SYMBOL(add_mtd_partitions);
EXPORT_SYMBOL(del_mtd_partitions);

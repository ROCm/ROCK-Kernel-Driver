/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2001  Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/major.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>
#include <linux/iobuf.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/blkpg.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>

#include <asm/uaccess.h>

static sector_t max_block(struct block_device *bdev)
{
	sector_t retval = ~0U;
	loff_t sz = bdev->bd_inode->i_size;

	if (sz) {
		sector_t size = block_size(bdev);
		unsigned sizebits = blksize_bits(size);
		retval = (sz >> sizebits);
	}
	return retval;
}

/* Kill _all_ buffers, dirty or not.. */
static void kill_bdev(struct block_device *bdev)
{
	invalidate_bdev(bdev, 1);
	truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
}	

int set_blocksize(struct block_device *bdev, int size)
{
	int oldsize;

	/* Size must be a power of two, and between 512 and PAGE_SIZE */
	if (size > PAGE_SIZE || size < 512 || (size & (size-1)))
		return -EINVAL;

	/* Size cannot be smaller than the size supported by the device */
	if (size < bdev_hardsect_size(bdev))
		return -EINVAL;

	oldsize = bdev->bd_block_size;
	if (oldsize == size)
		return 0;

	/* Ok, we're actually changing the blocksize.. */
	sync_blockdev(bdev);
	bdev->bd_block_size = size;
	bdev->bd_inode->i_blkbits = blksize_bits(size);
	kill_bdev(bdev);
	return 0;
}

int sb_set_blocksize(struct super_block *sb, int size)
{
	int bits;
	if (set_blocksize(sb->s_bdev, size) < 0)
		return 0;
	sb->s_blocksize = size;
	for (bits = 9, size >>= 9; size >>= 1; bits++)
		;
	sb->s_blocksize_bits = bits;
	return sb->s_blocksize;
}

int sb_min_blocksize(struct super_block *sb, int size)
{
	int minsize = bdev_hardsect_size(sb->s_bdev);
	if (size < minsize)
		size = minsize;
	return sb_set_blocksize(sb, size);
}

static int
blkdev_get_block(struct inode *inode, sector_t iblock,
		struct buffer_head *bh, int create)
{
	if (iblock >= max_block(inode->i_bdev))
		return -EIO;

	bh->b_bdev = inode->i_bdev;
	bh->b_blocknr = iblock;
	set_buffer_mapped(bh);
	return 0;
}

static int
blkdev_get_blocks(struct inode *inode, sector_t iblock,
		unsigned long max_blocks, struct buffer_head *bh, int create)
{
	if ((iblock + max_blocks) >= max_block(inode->i_bdev))
		return -EIO;

	bh->b_bdev = inode->i_bdev;
	bh->b_blocknr = iblock;
	bh->b_size = max_blocks << inode->i_blkbits;
	set_buffer_mapped(bh);
	return 0;
}

static int
blkdev_direct_IO(int rw, struct inode *inode, char *buf,
			loff_t offset, size_t count)
{
	return generic_direct_IO(rw, inode, buf, offset,
				count, blkdev_get_blocks);
}

static int blkdev_writepage(struct page * page)
{
	return block_write_full_page(page, blkdev_get_block);
}

static int blkdev_readpage(struct file * file, struct page * page)
{
	return block_read_full_page(page, blkdev_get_block);
}

static int blkdev_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return block_prepare_write(page, from, to, blkdev_get_block);
}

static int blkdev_commit_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return block_commit_write(page, from, to);
}

/*
 * private llseek:
 * for a block special file file->f_dentry->d_inode->i_size is zero
 * so we compute the size by hand (just as in block_read/write above)
 */
static loff_t block_llseek(struct file *file, loff_t offset, int origin)
{
	/* ewww */
	loff_t size = file->f_dentry->d_inode->i_bdev->bd_inode->i_size;
	loff_t retval;

	lock_kernel();

	switch (origin) {
		case 2:
			offset += size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0 && offset <= size) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = ++event;
		}
		retval = offset;
	}
	unlock_kernel();
	return retval;
}
	
/*
 *	Filp may be NULL when we are called by an msync of a vma
 *	since the vma has no handle.
 */
 
static int block_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	struct inode * inode = dentry->d_inode;

	return sync_blockdev(inode->i_bdev);
}

/*
 * pseudo-fs
 */

static struct super_block *bd_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "bdev:", NULL, 0x62646576);
}

static struct file_system_type bd_type = {
	name:		"bdev",
	get_sb:		bd_get_sb,
	kill_sb:	kill_anon_super,
};

static struct vfsmount *bd_mnt;
struct super_block *blockdev_superblock;

/*
 * bdev cache handling - shamelessly stolen from inode.c
 * We use smaller hashtable, though.
 */

#define HASH_BITS	6
#define HASH_SIZE	(1UL << HASH_BITS)
#define HASH_MASK	(HASH_SIZE-1)
static struct list_head bdev_hashtable[HASH_SIZE];
static spinlock_t bdev_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;
static kmem_cache_t * bdev_cachep;

#define alloc_bdev() \
	 ((struct block_device *) kmem_cache_alloc(bdev_cachep, SLAB_KERNEL))
#define destroy_bdev(bdev) kmem_cache_free(bdev_cachep, (bdev))

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct block_device * bdev = (struct block_device *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
	{
		memset(bdev, 0, sizeof(*bdev));
		sema_init(&bdev->bd_sem, 1);
		INIT_LIST_HEAD(&bdev->bd_inodes);
	}
}

void __init bdev_cache_init(void)
{
	int i, err;
	struct list_head *head = bdev_hashtable;

	i = HASH_SIZE;
	do {
		INIT_LIST_HEAD(head);
		head++;
		i--;
	} while (i);

	bdev_cachep = kmem_cache_create("bdev_cache",
					 sizeof(struct block_device),
					 0, SLAB_HWCACHE_ALIGN, init_once,
					 NULL);
	if (!bdev_cachep)
		panic("Cannot create bdev_cache SLAB cache");
	err = register_filesystem(&bd_type);
	if (err)
		panic("Cannot register bdev pseudo-fs");
	bd_mnt = kern_mount(&bd_type);
	err = PTR_ERR(bd_mnt);
	if (IS_ERR(bd_mnt))
		panic("Cannot create bdev pseudo-fs");
	blockdev_superblock = bd_mnt->mnt_sb;	/* For writeback */
}

/*
 * Most likely _very_ bad one - but then it's hardly critical for small
 * /dev and can be fixed when somebody will need really large one.
 */
static inline unsigned long hash(dev_t dev)
{
	unsigned long tmp = dev;
	tmp = tmp + (tmp >> HASH_BITS) + (tmp >> HASH_BITS*2);
	return tmp & HASH_MASK;
}

static struct block_device *bdfind(dev_t dev, struct list_head *head)
{
	struct list_head *p;
	struct block_device *bdev;
	list_for_each(p, head) {
		bdev = list_entry(p, struct block_device, bd_hash);
		if (bdev->bd_dev != dev)
			continue;
		atomic_inc(&bdev->bd_count);
		return bdev;
	}
	return NULL;
}

struct block_device *bdget(dev_t dev)
{
	struct list_head * head = bdev_hashtable + hash(dev);
	struct block_device *bdev, *new_bdev;
	spin_lock(&bdev_lock);
	bdev = bdfind(dev, head);
	spin_unlock(&bdev_lock);
	if (bdev)
		return bdev;
	new_bdev = alloc_bdev();
	if (new_bdev) {
		struct inode *inode = new_inode(bd_mnt->mnt_sb);
		if (inode) {
			kdev_t kdev = to_kdev_t(dev);

			atomic_set(&new_bdev->bd_count,1);
			new_bdev->bd_dev = dev;
			new_bdev->bd_op = NULL;
			new_bdev->bd_queue = NULL;
			new_bdev->bd_contains = NULL;
			new_bdev->bd_inode = inode;
			new_bdev->bd_part_count = 0;
			sema_init(&new_bdev->bd_part_sem, 1);
			inode->i_mode = S_IFBLK;
			inode->i_rdev = kdev;
			inode->i_bdev = new_bdev;
			inode->i_data.a_ops = &def_blk_aops;
			inode->i_data.gfp_mask = GFP_USER;
			inode->i_data.backing_dev_info = &default_backing_dev_info;
			spin_lock(&bdev_lock);
			bdev = bdfind(dev, head);
			if (!bdev) {
				list_add(&new_bdev->bd_hash, head);
				spin_unlock(&bdev_lock);
				return new_bdev;
			}
			spin_unlock(&bdev_lock);
			iput(new_bdev->bd_inode);
		}
		destroy_bdev(new_bdev);
	}
	return bdev;
}

static inline void __bd_forget(struct inode *inode)
{
	list_del_init(&inode->i_devices);
	inode->i_bdev = NULL;
	inode->i_mapping = &inode->i_data;
}

void bdput(struct block_device *bdev)
{
	if (atomic_dec_and_lock(&bdev->bd_count, &bdev_lock)) {
		struct list_head *p;
		if (bdev->bd_openers)
			BUG();
		list_del(&bdev->bd_hash);
		while ( (p = bdev->bd_inodes.next) != &bdev->bd_inodes ) {
			__bd_forget(list_entry(p, struct inode, i_devices));
		}
		spin_unlock(&bdev_lock);
		iput(bdev->bd_inode);
		destroy_bdev(bdev);
	}
}
 
int bd_acquire(struct inode *inode)
{
	struct block_device *bdev;
	spin_lock(&bdev_lock);
	if (inode->i_bdev) {
		atomic_inc(&inode->i_bdev->bd_count);
		spin_unlock(&bdev_lock);
		return 0;
	}
	spin_unlock(&bdev_lock);
	bdev = bdget(kdev_t_to_nr(inode->i_rdev));
	if (!bdev)
		return -ENOMEM;
	spin_lock(&bdev_lock);
	if (!inode->i_bdev) {
		inode->i_bdev = bdev;
		inode->i_mapping = bdev->bd_inode->i_mapping;
		list_add(&inode->i_devices, &bdev->bd_inodes);
	} else if (inode->i_bdev != bdev)
		BUG();
	spin_unlock(&bdev_lock);
	return 0;
}

/* Call when you free inode */

void bd_forget(struct inode *inode)
{
	spin_lock(&bdev_lock);
	if (inode->i_bdev)
		__bd_forget(inode);
	spin_unlock(&bdev_lock);
}

int bd_claim(struct block_device *bdev, void *holder)
{
	int res = -EBUSY;
	spin_lock(&bdev_lock);
	if (!bdev->bd_holder || bdev->bd_holder == holder) {
		bdev->bd_holder = holder;
		bdev->bd_holders++;
		res = 0;
	}
	spin_unlock(&bdev_lock);
	return res;
}

void bd_release(struct block_device *bdev)
{
	spin_lock(&bdev_lock);
	if (!--bdev->bd_holders)
		bdev->bd_holder = NULL;
	spin_unlock(&bdev_lock);
}

static struct {
	const char *name;
	struct block_device_operations *bdops;
} blkdevs[MAX_BLKDEV];

int get_blkdev_list(char * p)
{
	int i;
	int len;

	len = sprintf(p, "\nBlock devices:\n");
	for (i = 0; i < MAX_BLKDEV ; i++) {
		if (blkdevs[i].bdops) {
			len += sprintf(p+len, "%3d %s\n", i, blkdevs[i].name);
		}
	}
	return len;
}

/*
	Return the function table of a device.
	Load the driver if needed.
*/
struct block_device_operations * get_blkfops(unsigned int major)
{
	struct block_device_operations *ret = NULL;

	/* major 0 is used for non-device mounts */
	if (major && major < MAX_BLKDEV) {
#ifdef CONFIG_KMOD
		if (!blkdevs[major].bdops) {
			char name[20];
			sprintf(name, "block-major-%d", major);
			request_module(name);
		}
#endif
		ret = blkdevs[major].bdops;
	}
	return ret;
}

int register_blkdev(unsigned int major, const char * name, struct block_device_operations *bdops)
{
	if (major == 0) {
		for (major = MAX_BLKDEV-1; major > 0; major--) {
			if (blkdevs[major].bdops == NULL) {
				blkdevs[major].name = name;
				blkdevs[major].bdops = bdops;
				return major;
			}
		}
		return -EBUSY;
	}
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	if (blkdevs[major].bdops && blkdevs[major].bdops != bdops)
		return -EBUSY;
	blkdevs[major].name = name;
	blkdevs[major].bdops = bdops;
	return 0;
}

int unregister_blkdev(unsigned int major, const char * name)
{
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	if (!blkdevs[major].bdops)
		return -EINVAL;
	if (strcmp(blkdevs[major].name, name))
		return -EINVAL;
	blkdevs[major].name = NULL;
	blkdevs[major].bdops = NULL;
	return 0;
}

/*
 * This routine checks whether a removable media has been changed,
 * and invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to lose :-)
 */
int check_disk_change(kdev_t dev)
{
	int i;
	struct block_device_operations * bdops = NULL;

	i = major(dev);
	if (i < MAX_BLKDEV)
		bdops = blkdevs[i].bdops;
	if (bdops == NULL) {
		devfs_handle_t de;

		de = devfs_get_handle(NULL, NULL, i, minor(dev),
				      DEVFS_SPECIAL_BLK, 0);
		if (de) {
			bdops = devfs_get_ops(de);
			devfs_put_ops(de); /* We're running in owner module */
			devfs_put(de);
		}
	}
	if (bdops == NULL)
		return 0;
	if (bdops->check_media_change == NULL)
		return 0;
	if (!bdops->check_media_change(dev))
		return 0;

	printk(KERN_DEBUG "VFS: Disk change detected on device %s\n",
		__bdevname(dev));

	if (invalidate_device(dev, 0))
		printk("VFS: busy inodes on changed media.\n");

	if (bdops->revalidate)
		bdops->revalidate(dev);
	return 1;
}

static int do_open(struct block_device *bdev, struct inode *inode, struct file *file)
{
	int ret = -ENXIO;
	kdev_t dev = to_kdev_t(bdev->bd_dev);
	struct module *owner = NULL;
	struct block_device_operations *ops, *current_ops;

	lock_kernel();
	ops = get_blkfops(major(dev));
	if (ops) {
		owner = ops->owner;
		if (owner)
			__MOD_INC_USE_COUNT(owner);
	}

	down(&bdev->bd_sem);
	if (!bdev->bd_op)
		current_ops = ops;
	else
		current_ops = bdev->bd_op;
	if (!current_ops)
		goto out;
	if (!bdev->bd_contains) {
		unsigned minor = minor(dev);
		struct gendisk *g = get_gendisk(dev);
		bdev->bd_contains = bdev;
		if (g) {
			int shift = g->minor_shift;
			unsigned minor0 = (minor >> shift) << shift;
			if (minor != minor0) {
				struct block_device *disk;
				disk = bdget(MKDEV(major(dev), minor0));
				ret = -ENOMEM;
				if (!disk)
					goto out1;
				ret = blkdev_get(disk, file->f_mode, file->f_flags, BDEV_RAW);
				if (ret)
					goto out1;
				bdev->bd_contains = disk;
			}
		}
	}
	if (bdev->bd_contains == bdev) {
		if (current_ops->open) {
			ret = current_ops->open(inode, file);
			if (ret)
				goto out2;
		}
	} else {
		down(&bdev->bd_contains->bd_part_sem);
		bdev->bd_contains->bd_part_count++;
		up(&bdev->bd_contains->bd_part_sem);
	}
	if (!bdev->bd_op)
		bdev->bd_op = ops;
	else if (owner)
		__MOD_DEC_USE_COUNT(owner);
	if (!bdev->bd_openers) {
		struct blk_dev_struct *p = blk_dev + major(dev);
		struct gendisk *g = get_gendisk(dev);
		unsigned bsize = bdev_hardsect_size(bdev);

		bdev->bd_offset = 0;
		if (g) {
			bdev->bd_inode->i_size =
				(loff_t) g->part[minor(dev)].nr_sects << 9;
			bdev->bd_offset = g->part[minor(dev)].start_sect;
		} else if (blk_size[major(dev)])
			bdev->bd_inode->i_size =
				(loff_t) blk_size[major(dev)][minor(dev)] << 10;
		else
			bdev->bd_inode->i_size = 0;
		while (bsize < PAGE_CACHE_SIZE) {
			if (bdev->bd_inode->i_size & bsize)
				break;
			bsize <<= 1;
		}
		bdev->bd_block_size = bsize;
		bdev->bd_inode->i_blkbits = blksize_bits(bsize);
		if (p->queue)
			bdev->bd_queue =  p->queue(dev);
		else
			bdev->bd_queue = &p->request_queue;
		if (bdev->bd_inode->i_data.backing_dev_info ==
					&default_backing_dev_info) {
			struct backing_dev_info *bdi;

			bdi = blk_get_backing_dev_info(bdev);
			if (bdi == NULL)
				bdi = &default_backing_dev_info;
			inode->i_data.backing_dev_info = bdi;
			bdev->bd_inode->i_data.backing_dev_info = bdi;
		}
	}
	bdev->bd_openers++;
	up(&bdev->bd_sem);
	unlock_kernel();
	return 0;

out2:
	if (!bdev->bd_openers) {
		bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
		if (bdev != bdev->bd_contains) {
			blkdev_put(bdev->bd_contains, BDEV_RAW);
			bdev->bd_contains = NULL;
		}
	}
out1:
	if (owner)
		__MOD_DEC_USE_COUNT(owner);
out:
	up(&bdev->bd_sem);
	unlock_kernel();
	if (ret)
		bdput(bdev);
	return ret;
}

int blkdev_get(struct block_device *bdev, mode_t mode, unsigned flags, int kind)
{
	/*
	 * This crockload is due to bad choice of ->open() type.
	 * It will go away.
	 * For now, block device ->open() routine must _not_
	 * examine anything in 'inode' argument except ->i_rdev.
	 */
	struct file fake_file = {};
	struct dentry fake_dentry = {};
	fake_file.f_mode = mode;
	fake_file.f_flags = flags;
	fake_file.f_dentry = &fake_dentry;
	fake_dentry.d_inode = bdev->bd_inode;

	return do_open(bdev, bdev->bd_inode, &fake_file);
}

int blkdev_open(struct inode * inode, struct file * filp)
{
	struct block_device *bdev;

	/*
	 * Preserve backwards compatibility and allow large file access
	 * even if userspace doesn't ask for it explicitly. Some mkfs
	 * binary needs it. We might want to drop this workaround
	 * during an unstable branch.
	 */
	filp->f_flags |= O_LARGEFILE;

	bd_acquire(inode);
	bdev = inode->i_bdev;

	return do_open(bdev, inode, filp);
}	

int blkdev_put(struct block_device *bdev, int kind)
{
	int ret = 0;
	struct inode *bd_inode = bdev->bd_inode;

	down(&bdev->bd_sem);
	lock_kernel();
	switch (kind) {
	case BDEV_FILE:
	case BDEV_FS:
		sync_blockdev(bd_inode->i_bdev);
		break;
	}
	if (!--bdev->bd_openers)
		kill_bdev(bdev);
	if (bdev->bd_contains == bdev) {
		if (bdev->bd_op->release)
			ret = bdev->bd_op->release(bd_inode, NULL);
	} else {
		down(&bdev->bd_contains->bd_part_sem);
		bdev->bd_contains->bd_part_count--;
		up(&bdev->bd_contains->bd_part_sem);
	}
	if (!bdev->bd_openers) {
		if (bdev->bd_op->owner)
			__MOD_DEC_USE_COUNT(bdev->bd_op->owner);
		bdev->bd_op = NULL;
		bdev->bd_queue = NULL;
		bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
		if (bdev != bdev->bd_contains) {
			blkdev_put(bdev->bd_contains, BDEV_RAW);
			bdev->bd_contains = NULL;
		}
	}
	unlock_kernel();
	up(&bdev->bd_sem);
	bdput(bdev);
	return ret;
}

int blkdev_close(struct inode * inode, struct file * filp)
{
	return blkdev_put(inode->i_bdev, BDEV_FILE);
}

static int blkdev_ioctl(struct inode *inode, struct file *file, unsigned cmd,
			unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
	/*
	 * deprecated, use the /proc/iosched interface instead
	 */
	case BLKELVGET:
	case BLKELVSET:
		ret = -ENOTTY;
		break;
	case BLKRAGET:
	case BLKROGET:
	case BLKBSZGET:
	case BLKSSZGET:
	case BLKFRAGET:
	case BLKSECTGET:
	case BLKRASET:
	case BLKFRASET:
	case BLKBSZSET:
	case BLKPG:
		ret = blk_ioctl(inode->i_bdev, cmd, arg);
		break;
	default:
		if (inode->i_bdev->bd_op->ioctl)
			ret =inode->i_bdev->bd_op->ioctl(inode, file, cmd, arg);
		if (ret == -EINVAL) {
			switch (cmd) {
				case BLKGETSIZE:
				case BLKGETSIZE64:
				case BLKFLSBUF:
				case BLKROSET:
					ret = blk_ioctl(inode->i_bdev,cmd,arg);
			}
		}
		break;
	}
	return ret;
}

struct address_space_operations def_blk_aops = {
	readpage: blkdev_readpage,
	writepage: blkdev_writepage,
	sync_page: block_sync_page,
	prepare_write: blkdev_prepare_write,
	commit_write: blkdev_commit_write,
	writepages: generic_writepages,
	vm_writeback: generic_vm_writeback,
	direct_IO: blkdev_direct_IO,
};

struct file_operations def_blk_fops = {
	open:		blkdev_open,
	release:	blkdev_close,
	llseek:		block_llseek,
	read:		generic_file_read,
	write:		generic_file_write,
	mmap:		generic_file_mmap,
	fsync:		block_fsync,
	ioctl:		blkdev_ioctl,
};

int ioctl_by_bdev(struct block_device *bdev, unsigned cmd, unsigned long arg)
{
	int res;
	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
	res = blkdev_ioctl(bdev->bd_inode, NULL, cmd, arg);
	set_fs(old_fs);
	return res;
}

const char *__bdevname(kdev_t dev)
{
	static char buffer[32];
	const char * name = blkdevs[major(dev)].name;

	if (!name)
		name = "unknown-block";

	sprintf(buffer, "%s(%d,%d)", name, major(dev), minor(dev));
	return buffer;
}

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
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/blkpg.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/mount.h>
#include <linux/uio.h>
#include <linux/namei.h>
#include <asm/uaccess.h>


static sector_t max_block(struct block_device *bdev)
{
	sector_t retval = ~((sector_t)0);
	loff_t sz = bdev->bd_inode->i_size;

	if (sz) {
		unsigned int size = block_size(bdev);
		unsigned int sizebits = blksize_bits(size);
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
	if ((iblock + max_blocks) > max_block(inode->i_bdev))
		return -EIO;

	bh->b_bdev = inode->i_bdev;
	bh->b_blocknr = iblock;
	bh->b_size = max_blocks << inode->i_blkbits;
	set_buffer_mapped(bh);
	return 0;
}

static int
blkdev_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
			loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_dentry->d_inode->i_mapping->host;

	return blockdev_direct_IO(rw, iocb, inode, inode->i_bdev, iov, offset,
				nr_segs, blkdev_get_blocks);
}

static int blkdev_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, blkdev_get_block, wbc);
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
	int flags, const char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "bdev:", NULL, 0x62646576);
}

static struct file_system_type bd_type = {
	.name		= "bdev",
	.get_sb		= bd_get_sb,
	.kill_sb	= kill_anon_super,
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
			new_bdev->bd_contains = NULL;
			new_bdev->bd_inode = inode;
			new_bdev->bd_block_size = (1 << inode->i_blkbits);
			new_bdev->bd_part_count = 0;
			new_bdev->bd_invalidated = 0;
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

long nr_blockdev_pages(void)
{
	long ret = 0;
	int i;

	spin_lock(&bdev_lock);
	for (i = 0; i < ARRAY_SIZE(bdev_hashtable); i++) {
		struct list_head *head = &bdev_hashtable[i];
		struct list_head *lh;

		if (head == NULL)
			continue;
		list_for_each(lh, head) {
			struct block_device *bdev;

			bdev = list_entry(lh, struct block_device, bd_hash);
			ret += bdev->bd_inode->i_mapping->nrpages;
		}
	}
	spin_unlock(&bdev_lock);
	return ret;
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

/*
 * Tries to open block device by device number.  Use it ONLY if you
 * really do not have anything better - i.e. when you are behind a
 * truly sucky interface and all you are given is a device number.  _Never_
 * to be used for internal purposes.  If you ever need it - reconsider
 * your API.
 */
struct block_device *open_by_devnum(dev_t dev, unsigned mode, int kind)
{
	struct block_device *bdev = bdget(dev);
	int err = -ENOMEM;
	int flags = mode & FMODE_WRITE ? O_RDWR : O_RDONLY;
	if (bdev)
		err = blkdev_get(bdev, mode, flags, kind);
	return err ? ERR_PTR(err) : bdev;
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
int check_disk_change(struct block_device *bdev)
{
	struct gendisk *disk = bdev->bd_disk;
	struct block_device_operations * bdops = disk->fops;

	if (!bdops->media_changed)
		return 0;
	if (!bdops->media_changed(bdev->bd_disk))
		return 0;

	if (__invalidate_device(bdev, 0))
		printk("VFS: busy inodes on changed media.\n");

	if (bdops->revalidate_disk)
		bdops->revalidate_disk(bdev->bd_disk);
	if (bdev->bd_disk->minors > 1)
		bdev->bd_invalidated = 1;
	return 1;
}

static void bd_set_size(struct block_device *bdev, loff_t size)
{
	unsigned bsize = bdev_hardsect_size(bdev);
	bdev->bd_inode->i_size = size;
	while (bsize < PAGE_CACHE_SIZE) {
		if (size & bsize)
			break;
		bsize <<= 1;
	}
	bdev->bd_block_size = bsize;
	bdev->bd_inode->i_blkbits = blksize_bits(bsize);
}

static int do_open(struct block_device *bdev, struct inode *inode, struct file *file)
{
	struct module *owner = NULL;
	struct gendisk *disk;
	int ret = -ENXIO;
	int part;

	lock_kernel();
	disk = get_gendisk(bdev->bd_dev, &part);
	if (!disk) {
		unlock_kernel();
		bdput(bdev);
		return ret;
	}
	owner = disk->fops->owner;

	down(&bdev->bd_sem);
	if (!bdev->bd_openers) {
		bdev->bd_disk = disk;
		bdev->bd_contains = bdev;
		if (!part) {
			struct backing_dev_info *bdi;
			if (disk->fops->open) {
				ret = disk->fops->open(inode, file);
				if (ret)
					goto out_first;
			}
			bdev->bd_offset = 0;
			if (!bdev->bd_openers) {
				bd_set_size(bdev,(loff_t)get_capacity(disk)<<9);
				bdi = blk_get_backing_dev_info(bdev);
				if (bdi == NULL)
					bdi = &default_backing_dev_info;
				bdev->bd_inode->i_data.backing_dev_info = bdi;
			}
			if (bdev->bd_invalidated)
				rescan_partitions(disk, bdev);
		} else {
			struct hd_struct *p;
			struct block_device *whole;
			whole = bdget_disk(disk, 0);
			ret = -ENOMEM;
			if (!whole)
				goto out_first;
			ret = blkdev_get(whole, file->f_mode, file->f_flags, BDEV_RAW);
			if (ret)
				goto out_first;
			bdev->bd_contains = whole;
			down(&whole->bd_sem);
			whole->bd_part_count++;
			p = disk->part[part - 1];
			bdev->bd_inode->i_data.backing_dev_info =
			   whole->bd_inode->i_data.backing_dev_info;
			if (!(disk->flags & GENHD_FL_UP) || !p || !p->nr_sects) {
				whole->bd_part_count--;
				up(&whole->bd_sem);
				ret = -ENXIO;
				goto out_first;
			}
			bdev->bd_offset = p->start_sect;
			bd_set_size(bdev, (loff_t) p->nr_sects << 9);
			up(&whole->bd_sem);
		}
	} else {
		put_disk(disk);
		module_put(owner);
		if (bdev->bd_contains == bdev) {
			if (bdev->bd_disk->fops->open) {
				ret = bdev->bd_disk->fops->open(inode, file);
				if (ret)
					goto out;
			}
			if (bdev->bd_invalidated)
				rescan_partitions(bdev->bd_disk, bdev);
		} else {
			down(&bdev->bd_contains->bd_sem);
			bdev->bd_contains->bd_part_count++;
			up(&bdev->bd_contains->bd_sem);
		}
	}
	bdev->bd_openers++;
	up(&bdev->bd_sem);
	unlock_kernel();
	return 0;

out_first:
	bdev->bd_disk = NULL;
	bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
	if (bdev != bdev->bd_contains)
		blkdev_put(bdev->bd_contains, BDEV_RAW);
	bdev->bd_contains = NULL;
	put_disk(disk);
	module_put(owner);
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
	struct gendisk *disk = bdev->bd_disk;

	down(&bdev->bd_sem);
	lock_kernel();
	if (!--bdev->bd_openers) {
		switch (kind) {
		case BDEV_FILE:
		case BDEV_FS:
			sync_blockdev(bd_inode->i_bdev);
			break;
		}
		kill_bdev(bdev);
	}
	if (bdev->bd_contains == bdev) {
		if (disk->fops->release)
			ret = disk->fops->release(bd_inode, NULL);
	} else {
		down(&bdev->bd_contains->bd_sem);
		bdev->bd_contains->bd_part_count--;
		up(&bdev->bd_contains->bd_sem);
	}
	if (!bdev->bd_openers) {
		struct module *owner = disk->fops->owner;

		put_disk(disk);
		module_put(owner);

		bdev->bd_disk = NULL;
		bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
		if (bdev != bdev->bd_contains) {
			blkdev_put(bdev->bd_contains, BDEV_RAW);
		}
		bdev->bd_contains = NULL;
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

static ssize_t blkdev_file_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct iovec local_iov = { .iov_base = (void __user *)buf, .iov_len = count };

	return generic_file_write_nolock(file, &local_iov, 1, ppos);
}

static ssize_t blkdev_file_aio_write(struct kiocb *iocb, const char __user *buf,
				   size_t count, loff_t pos)
{
	struct iovec local_iov = { .iov_base = (void __user *)buf, .iov_len = count };

	return generic_file_aio_write_nolock(iocb, &local_iov, 1, &iocb->ki_pos);
}


struct address_space_operations def_blk_aops = {
	.readpage	= blkdev_readpage,
	.writepage	= blkdev_writepage,
	.sync_page	= block_sync_page,
	.prepare_write	= blkdev_prepare_write,
	.commit_write	= blkdev_commit_write,
	.writepages	= generic_writepages,
	.direct_IO	= blkdev_direct_IO,
};

struct file_operations def_blk_fops = {
	.open		= blkdev_open,
	.release	= blkdev_close,
	.llseek		= block_llseek,
	.read		= generic_file_read,
	.write		= blkdev_file_write,
  	.aio_read	= generic_file_aio_read,
  	.aio_write	= blkdev_file_aio_write, 
	.mmap		= generic_file_mmap,
	.fsync		= block_fsync,
	.ioctl		= blkdev_ioctl,
	.readv		= generic_file_readv,
	.writev		= generic_file_writev,
	.sendfile	= generic_file_sendfile,
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

/**
 * lookup_bdev  - lookup a struct block_device by name
 *
 * @path:	special file representing the block device
 *
 * Get a reference to the blockdevice at @path in the current
 * namespace if possible and return it.  Return ERR_PTR(error)
 * otherwise.
 */
struct block_device *lookup_bdev(const char *path)
{
	struct block_device *bdev;
	struct inode *inode;
	struct nameidata nd;
	int error;

	if (!path || !*path)
		return ERR_PTR(-EINVAL);

	error = path_lookup(path, LOOKUP_FOLLOW, &nd);
	if (error)
		return ERR_PTR(error);

	inode = nd.dentry->d_inode;
	error = -ENOTBLK;
	if (!S_ISBLK(inode->i_mode))
		goto fail;
	error = -EACCES;
	if (nd.mnt->mnt_flags & MNT_NODEV)
		goto fail;
	error = bd_acquire(inode);
	if (error)
		goto fail;
	bdev = inode->i_bdev;

out:
	path_release(&nd);
	return bdev;
fail:
	bdev = ERR_PTR(error);
	goto out;
}

/**
 * open_bdev_excl  -  open a block device by name and set it up for use
 *
 * @path:	special file representing the block device
 * @flags:	%MS_RDONLY for opening read-only
 * @kind:	usage (same as the 4th paramter to blkdev_get)
 * @holder:	owner for exclusion
 *
 * Open the blockdevice described by the special file at @path, claim it
 * for the @holder and properly set it up for @kind usage.
 */
struct block_device *open_bdev_excl(const char *path, int flags,
				    int kind, void *holder)
{
	struct block_device *bdev;
	mode_t mode = FMODE_READ;
	int error = 0;

	bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return bdev;

	if (!(flags & MS_RDONLY))
		mode |= FMODE_WRITE;
	error = blkdev_get(bdev, mode, 0, kind);
	if (error)
		return ERR_PTR(error);
	error = -EACCES;
	if (!(flags & MS_RDONLY) && bdev_read_only(bdev))
		goto blkdev_put;
	error = bd_claim(bdev, holder);
	if (error)
		goto blkdev_put;

	return bdev;
	
blkdev_put:
	blkdev_put(bdev, BDEV_FS);
	return ERR_PTR(error);
}

/**
 * close_bdev_excl  -  release a blockdevice openen by open_bdev_excl()
 *
 * @bdev:	blockdevice to close
 * @kind:	usage (same as the 4th paramter to blkdev_get)
 *
 * This is the counterpart to open_bdev_excl().
 */
void close_bdev_excl(struct block_device *bdev, int kind)
{
	bd_release(bdev);
	blkdev_put(bdev, kind);
}

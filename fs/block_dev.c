/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2001  Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/locks.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/major.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>
#include <linux/iobuf.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>

#include <asm/uaccess.h>

static inline int blkdev_get_block(struct inode * inode, long iblock, struct buffer_head * bh_result)
{
	int err;

	err = -EIO;
	if (iblock >= buffered_blk_size(inode->i_rdev) >> (BUFFERED_BLOCKSIZE_BITS - BLOCK_SIZE_BITS))
		goto out;

	bh_result->b_blocknr = iblock;
	bh_result->b_state |= 1UL << BH_Mapped;
	err = 0;

 out:
	return err;
}

static int blkdev_direct_IO(int rw, struct inode * inode, struct kiobuf * iobuf, unsigned long blocknr, int blocksize)
{
	int i, nr_blocks, retval, dev = inode->i_rdev;
	unsigned long * blocks = iobuf->blocks;

	if (blocksize != BUFFERED_BLOCKSIZE)
		BUG();

	nr_blocks = iobuf->length >> BUFFERED_BLOCKSIZE_BITS;
	/* build the blocklist */
	for (i = 0; i < nr_blocks; i++, blocknr++) {
		struct buffer_head bh;

		retval = blkdev_get_block(inode, blocknr, &bh);
		if (retval)
			goto out;

		blocks[i] = bh.b_blocknr;
	}

	retval = brw_kiovec(rw, 1, &iobuf, dev, iobuf->blocks, blocksize);

 out:
	return retval;
}

static int blkdev_writepage(struct page * page)
{
	int err, i;
	unsigned long block;
	struct buffer_head *bh, *head;
	struct inode *inode = page->mapping->host;

	if (!PageLocked(page))
		BUG();

	if (!page->buffers)
		create_empty_buffers(page, inode->i_rdev, BUFFERED_BLOCKSIZE);
	head = page->buffers;

	block = page->index << (PAGE_CACHE_SHIFT - BUFFERED_BLOCKSIZE_BITS);

	bh = head;
	i = 0;

	/* Stage 1: make sure we have all the buffers mapped! */
	do {
		/*
		 * If the buffer isn't up-to-date, we can't be sure
		 * that the buffer has been initialized with the proper
		 * block number information etc..
		 *
		 * Leave it to the low-level FS to make all those
		 * decisions (block #0 may actually be a valid block)
		 */
		if (!buffer_mapped(bh)) {
			err = blkdev_get_block(inode, block, bh);
			if (err)
				goto out;
		}
		bh = bh->b_this_page;
		block++;
	} while (bh != head);

	/* Stage 2: lock the buffers, mark them clean */
	do {
		lock_buffer(bh);
		set_buffer_async_io(bh);
		set_bit(BH_Uptodate, &bh->b_state);
		clear_bit(BH_Dirty, &bh->b_state);
		bh = bh->b_this_page;
	} while (bh != head);

	/* Stage 3: submit the IO */
	do {
		submit_bh(WRITE, bh);
		bh = bh->b_this_page;
	} while (bh != head);

	/* Done - end_buffer_io_async will unlock */
	SetPageUptodate(page);
	return 0;

out:
	ClearPageUptodate(page);
	UnlockPage(page);
	return err;
}

static int blkdev_readpage(struct file * file, struct page * page)
{
	struct inode *inode = page->mapping->host;
	kdev_t dev = inode->i_rdev;
	unsigned long iblock, lblock;
	struct buffer_head *bh, *head, *arr[1 << (PAGE_CACHE_SHIFT - BUFFERED_BLOCKSIZE_BITS)];
	unsigned int blocks;
	int nr, i;

	if (!PageLocked(page))
		PAGE_BUG(page);
	if (!page->buffers)
		create_empty_buffers(page, dev, BUFFERED_BLOCKSIZE);
	head = page->buffers;

	blocks = PAGE_CACHE_SIZE >> BUFFERED_BLOCKSIZE_BITS;
	iblock = page->index << (PAGE_CACHE_SHIFT - BUFFERED_BLOCKSIZE_BITS);
	lblock = buffered_blk_size(dev) >> (BUFFERED_BLOCKSIZE_BITS - BLOCK_SIZE_BITS);
	bh = head;
	nr = 0;
	i = 0;

	do {
		if (buffer_uptodate(bh))
			continue;

		if (!buffer_mapped(bh)) {
			if (iblock <= lblock) {
				if (blkdev_get_block(inode, iblock, bh))
					continue;
			}
			if (!buffer_mapped(bh)) {
				memset(kmap(page) + i * BUFFERED_BLOCKSIZE, 0, BUFFERED_BLOCKSIZE);
				flush_dcache_page(page);
				kunmap(page);
				set_bit(BH_Uptodate, &bh->b_state);
				continue;
			}
			/* get_block() might have updated the buffer synchronously */
			if (buffer_uptodate(bh))
				continue;
		}

		arr[nr] = bh;
		nr++;
	} while (i++, iblock++, (bh = bh->b_this_page) != head);

	if (!nr) {
		/*
		 * all buffers are uptodate - we can set the page
		 * uptodate as well.
		 */
		SetPageUptodate(page);
		UnlockPage(page);
		return 0;
	}

	/* Stage two: lock the buffers */
	for (i = 0; i < nr; i++) {
		struct buffer_head * bh = arr[i];
		lock_buffer(bh);
		set_buffer_async_io(bh);
	}

	/* Stage 3: start the IO */
	for (i = 0; i < nr; i++)
		submit_bh(READ, arr[i]);

	return 0;
}

static int __blkdev_prepare_write(struct inode *inode, struct page *page,
				  unsigned from, unsigned to)
{
	kdev_t dev = inode->i_rdev;
	unsigned block_start, block_end;
	unsigned long block;
	int err = 0;
	struct buffer_head *bh, *head, *wait[2], **wait_bh=wait;
	kmap(page);

	if (!page->buffers)
		create_empty_buffers(page, dev, BUFFERED_BLOCKSIZE);
	head = page->buffers;

	block = page->index << (PAGE_CACHE_SHIFT - BUFFERED_BLOCKSIZE_BITS);

	for(bh = head, block_start = 0; bh != head || !block_start;
	    block++, block_start=block_end, bh = bh->b_this_page) {
		if (!bh)
			BUG();
		block_end = block_start + BUFFERED_BLOCKSIZE;
		if (block_end <= from)
			continue;
		if (block_start >= to)
			break;
		if (!buffer_mapped(bh)) {
			err = blkdev_get_block(inode, block, bh);
			if (err)
				goto out;
		}
		if (Page_Uptodate(page)) {
			set_bit(BH_Uptodate, &bh->b_state);
			continue; 
		}
		if (!buffer_uptodate(bh) &&
		     (block_start < from || block_end > to)) {
			ll_rw_block(READ, 1, &bh);
			*wait_bh++=bh;
		}
	}
	/*
	 * If we issued read requests - let them complete.
	 */
	while(wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		err = -EIO;
		if (!buffer_uptodate(*wait_bh))
			goto out;
	}
	return 0;
out:
	return err;
}

static int blkdev_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	int err = __blkdev_prepare_write(inode, page, from, to);
	if (err) {
		ClearPageUptodate(page);
		kunmap(page);
	}
	return err;
}

static int __blkdev_commit_write(struct inode *inode, struct page *page,
				 unsigned from, unsigned to)
{
	unsigned block_start, block_end;
	int partial = 0, need_balance_dirty = 0;
	struct buffer_head *bh, *head;

	for(bh = head = page->buffers, block_start = 0;
	    bh != head || !block_start;
	    block_start=block_end, bh = bh->b_this_page) {
		block_end = block_start + BUFFERED_BLOCKSIZE;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = 1;
		} else {
			set_bit(BH_Uptodate, &bh->b_state);
			if (!atomic_set_buffer_dirty(bh)) {
				__mark_dirty(bh);
				buffer_insert_inode_data_queue(bh, inode);
				need_balance_dirty = 1;
			}
		}
	}

	if (need_balance_dirty)
		balance_dirty();
	/*
	 * is this a partial write that happened to make all buffers
	 * uptodate then we can optimize away a bogus readpage() for
	 * the next read(). Here we 'discover' wether the page went
	 * uptodate as a result of this (potentially partial) write.
	 */
	if (!partial)
		SetPageUptodate(page);
	return 0;
}

static int blkdev_commit_write(struct file *file, struct page *page,
			       unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	__blkdev_commit_write(inode,page,from,to);
	kunmap(page);
	return 0;
}

/*
 * private llseek:
 * for a block special file file->f_dentry->d_inode->i_size is zero
 * so we compute the size by hand (just as in block_read/write above)
 */
static loff_t block_llseek(struct file *file, loff_t offset, int origin)
{
	long long retval;
	kdev_t dev;

	switch (origin) {
		case 2:
			dev = file->f_dentry->d_inode->i_rdev;
			if (blk_size[MAJOR(dev)])
				offset += (loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS;
			/* else?  return -EINVAL? */
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_reada = 0;
			file->f_version = ++event;
		}
		retval = offset;
	}
	return retval;
}
	

static int __block_fsync(struct inode * inode)
{
	int ret;

	filemap_fdatasync(inode->i_mapping);
	ret = sync_buffers(inode->i_rdev, 1);
	filemap_fdatawait(inode->i_mapping);

	return ret;
}

/*
 *	Filp may be NULL when we are called by an msync of a vma
 *	since the vma has no handle.
 */
 
static int block_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	struct inode * inode = dentry->d_inode;

	return __block_fsync(inode);
}

/*
 * bdev cache handling - shamelessly stolen from inode.c
 * We use smaller hashtable, though.
 */

#define HASH_BITS	6
#define HASH_SIZE	(1UL << HASH_BITS)
#define HASH_MASK	(HASH_SIZE-1)
static struct list_head bdev_hashtable[HASH_SIZE];
static spinlock_t bdev_lock = SPIN_LOCK_UNLOCKED;
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
	}
}

void __init bdev_cache_init(void)
{
	int i;
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
	for (p=head->next; p!=head; p=p->next) {
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
	if (!new_bdev)
		return NULL;
	atomic_set(&new_bdev->bd_count,1);
	new_bdev->bd_dev = dev;
	new_bdev->bd_op = NULL;
	new_bdev->bd_inode = NULL;
	spin_lock(&bdev_lock);
	bdev = bdfind(dev, head);
	if (!bdev) {
		list_add(&new_bdev->bd_hash, head);
		spin_unlock(&bdev_lock);
		return new_bdev;
	}
	spin_unlock(&bdev_lock);
	destroy_bdev(new_bdev);
	return bdev;
}

void bdput(struct block_device *bdev)
{
	if (atomic_dec_and_test(&bdev->bd_count)) {
		if (bdev->bd_openers)
			BUG();
		if (bdev->bd_cache_openers)
			BUG();
		spin_lock(&bdev_lock);
		list_del(&bdev->bd_hash);
		spin_unlock(&bdev_lock);
		destroy_bdev(bdev);
	}
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
const struct block_device_operations * get_blkfops(unsigned int major)
{
	const struct block_device_operations *ret = NULL;

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
	const struct block_device_operations * bdops = NULL;

	i = MAJOR(dev);
	if (i < MAX_BLKDEV)
		bdops = blkdevs[i].bdops;
	if (bdops == NULL) {
		devfs_handle_t de;

		de = devfs_find_handle (NULL, NULL, i, MINOR (dev),
					DEVFS_SPECIAL_BLK, 0);
		if (de) bdops = devfs_get_ops (de);
	}
	if (bdops == NULL)
		return 0;
	if (bdops->check_media_change == NULL)
		return 0;
	if (!bdops->check_media_change(dev))
		return 0;

	printk(KERN_DEBUG "VFS: Disk change detected on device %s\n",
		bdevname(dev));

	if (invalidate_device(dev, 0))
		printk("VFS: busy inodes on changed media.\n");

	if (bdops->revalidate)
		bdops->revalidate(dev);
	return 1;
}

int ioctl_by_bdev(struct block_device *bdev, unsigned cmd, unsigned long arg)
{
	struct inode inode_fake;
	int res;
	mm_segment_t old_fs = get_fs();

	if (!bdev->bd_op->ioctl)
		return -EINVAL;
	memset(&inode_fake, 0, sizeof(inode_fake));
	inode_fake.i_rdev = to_kdev_t(bdev->bd_dev);
	inode_fake.i_bdev = bdev;
	init_waitqueue_head(&inode_fake.i_wait);
	set_fs(KERNEL_DS);
	res = bdev->bd_op->ioctl(&inode_fake, NULL, cmd, arg);
	set_fs(old_fs);
	return res;
}

int blkdev_get(struct block_device *bdev, mode_t mode, unsigned flags, int kind)
{
	int ret = -ENODEV;
	kdev_t rdev = to_kdev_t(bdev->bd_dev); /* this should become bdev */
	down(&bdev->bd_sem);
	lock_kernel();
	if (!bdev->bd_op)
		bdev->bd_op = get_blkfops(MAJOR(rdev));
	if (bdev->bd_op) {
		/*
		 * This crockload is due to bad choice of ->open() type.
		 * It will go away.
		 * For now, block device ->open() routine must _not_
		 * examine anything in 'inode' argument except ->i_rdev.
		 */
		struct file fake_file = {};
		struct dentry fake_dentry = {};
		struct inode *fake_inode = get_empty_inode();
		ret = -ENOMEM;
		if (fake_inode) {
			fake_file.f_mode = mode;
			fake_file.f_flags = flags;
			fake_file.f_dentry = &fake_dentry;
			fake_dentry.d_inode = fake_inode;
			fake_inode->i_rdev = rdev;
			ret = 0;
			if (bdev->bd_op->open)
				ret = bdev->bd_op->open(fake_inode, &fake_file);
			if (!ret) {
				bdev->bd_openers++;
				atomic_inc(&bdev->bd_count);
			} else if (!bdev->bd_openers)
				bdev->bd_op = NULL;
			iput(fake_inode);
		}
	}
	unlock_kernel();
	up(&bdev->bd_sem);
	return ret;
}

int blkdev_open(struct inode * inode, struct file * filp)
{
	int ret = -ENXIO;
	struct block_device *bdev = inode->i_bdev;

	/*
	 * Preserve backwards compatibility and allow large file access
	 * even if userspace doesn't ask for it explicitly. Some mkfs
	 * binary needs it. We might want to drop this workaround
	 * during an unstable branch.
	 */
	filp->f_flags |= O_LARGEFILE;

	down(&bdev->bd_sem);
	lock_kernel();
	if (!bdev->bd_op)
		bdev->bd_op = get_blkfops(MAJOR(inode->i_rdev));
	if (bdev->bd_op) {
		ret = 0;
		if (bdev->bd_op->open)
			ret = bdev->bd_op->open(inode,filp);
		if (!ret) {
			bdev->bd_openers++;
			if (!bdev->bd_cache_openers && bdev->bd_inode)
				BUG();
			if (bdev->bd_cache_openers && !bdev->bd_inode)
				BUG();
			if (!bdev->bd_cache_openers++)
				bdev->bd_inode = inode;
			else {
				if (bdev->bd_inode != inode && !inode->i_mapping_overload++) {
					inode->i_mapping = bdev->bd_inode->i_mapping;
					atomic_inc(&bdev->bd_inode->i_count);
				}
			}
		} else if (!bdev->bd_openers)
			bdev->bd_op = NULL;
	}	
	unlock_kernel();
	up(&bdev->bd_sem);
	return ret;
}	

int blkdev_put(struct block_device *bdev, int kind)
{
	int ret = 0;
	kdev_t rdev = to_kdev_t(bdev->bd_dev); /* this should become bdev */
	down(&bdev->bd_sem);
	lock_kernel();
	if (kind == BDEV_FILE)
		fsync_dev(rdev);
	else if (kind == BDEV_FS)
		fsync_no_super(rdev);
	/* only filesystems uses buffer cache for the metadata these days */
	if (kind == BDEV_FS)
		invalidate_buffers(rdev);
	if (bdev->bd_op->release) {
		struct inode * fake_inode = get_empty_inode();
		ret = -ENOMEM;
		if (fake_inode) {
			fake_inode->i_rdev = rdev;
			ret = bdev->bd_op->release(fake_inode, NULL);
			iput(fake_inode);
		} else
			printk(KERN_WARNING "blkdev_put: ->release couldn't be run due -ENOMEM\n");
	}
	if (!--bdev->bd_openers)
		bdev->bd_op = NULL;	/* we can't rely on driver being */
					/* kind to stay around. */
	unlock_kernel();
	up(&bdev->bd_sem);
	bdput(bdev);
	return ret;
}

int blkdev_close(struct inode * inode, struct file * filp)
{
	struct block_device *bdev = inode->i_bdev;
	int ret = 0;
	struct inode * bd_inode = bdev->bd_inode;

	if (bd_inode->i_mapping != inode->i_mapping)
		BUG();
	down(&bdev->bd_sem);
	lock_kernel();
	/* cache coherency protocol */
	if (!--bdev->bd_cache_openers) {
		struct super_block * sb;

		/* flush the pagecache to disk */
		__block_fsync(inode);
		/* drop the pagecache, uptodate info is on disk by now */
		truncate_inode_pages(inode->i_mapping, 0);
		/* forget the bdev pagecache address space */
		bdev->bd_inode = NULL;

		/* if the fs was mounted ro just throw away most of its caches */
		sb = get_super(inode->i_rdev);
		if (sb) {
			if (sb->s_flags & MS_RDONLY) {
				/*
				 * This call is not destructive in terms of
				 * dirty cache, so it is safe to run it
				 * even if the fs gets mounted read write
				 * under us.
				 */
				invalidate_device(inode->i_rdev, 0);
			}

			/*
			 * Now only if an underlying fs is mounted ro we'll
			 * try to refill its pinned buffer cache from disk.
			 * The fs cannot go away under us because we hold
			 * the read semaphore of the superblock, but
			 * we must also serialize against ->remount_fs and
			 * ->read_super callbacks to avoid MS_RDONLY to go
			 * away under us.
			 */
			lock_super(sb);
			if (sb->s_flags & MS_RDONLY)
				/* now refill the obsolete pinned buffers from disk */
				update_buffers(inode->i_rdev);
			unlock_super(sb);

			drop_super(sb);
		}
	}
	if (inode != bd_inode && !--inode->i_mapping_overload) {
		inode->i_mapping = &inode->i_data;
		iput(bd_inode);
	}

	/* release the device driver */
	if (bdev->bd_op->release)
		ret = bdev->bd_op->release(inode, NULL);
	if (!--bdev->bd_openers)
		bdev->bd_op = NULL;
	unlock_kernel();
	up(&bdev->bd_sem);

	return ret;
}

static int blkdev_ioctl(struct inode *inode, struct file *file, unsigned cmd,
			unsigned long arg)
{
	if (inode->i_bdev->bd_op->ioctl)
		return inode->i_bdev->bd_op->ioctl(inode, file, cmd, arg);
	return -EINVAL;
}

struct address_space_operations def_blk_aops = {
	readpage: blkdev_readpage,
	writepage: blkdev_writepage,
	sync_page: block_sync_page,
	prepare_write: blkdev_prepare_write,
	commit_write: blkdev_commit_write,
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

const char * bdevname(kdev_t dev)
{
	static char buffer[32];
	const char * name = blkdevs[MAJOR(dev)].name;

	if (!name)
		name = "unknown-block";

	sprintf(buffer, "%s(%d,%d)", name, MAJOR(dev), MINOR(dev));
	return buffer;
}

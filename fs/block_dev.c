/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/locks.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>
#include <linux/kmod.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

extern int *blk_size[];
extern int *blksize_size[];

#define MAX_BUF_PER_PAGE (PAGE_SIZE / 512)
#define NBUF 64

ssize_t block_write(struct file * filp, const char * buf,
		    size_t count, loff_t *ppos)
{
	struct inode * inode = filp->f_dentry->d_inode;
	ssize_t blocksize, blocksize_bits, i, buffercount, write_error;
	ssize_t block, blocks;
	loff_t offset;
	ssize_t chars;
	ssize_t written;
	struct buffer_head * bhlist[NBUF];
	size_t size;
	kdev_t dev = inode->i_rdev;
	struct buffer_head * bh, *bufferlist[NBUF];
	register char * p;

	if (is_read_only(dev))
		return -EPERM;

	written = write_error = buffercount = 0;
	blocksize = BLOCK_SIZE;
	if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
		blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];

	i = blocksize;
	blocksize_bits = 0;
	while(i != 1) {
		blocksize_bits++;
		i >>= 1;
	}

	block = *ppos >> blocksize_bits;
	offset = *ppos & (blocksize-1);

	if (blk_size[MAJOR(dev)])
		size = ((loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS) >> blocksize_bits;
	else
		size = INT_MAX;
	while (count>0) {
		if (block >= size)
			return written ? written : -ENOSPC;
		chars = blocksize - offset;
		if (chars > count)
			chars=count;

#if 0
		/* get the buffer head */
		{
			struct buffer_head * (*fn)(kdev_t, int, int) = getblk;
			if (chars != blocksize)
				fn = bread;
			bh = fn(dev, block, blocksize);
			if (!bh)
				return written ? written : -EIO;
			if (!buffer_uptodate(bh))
				wait_on_buffer(bh);
		}
#else
		bh = getblk(dev, block, blocksize);
		if (!bh)
			return written ? written : -EIO;

		if (!buffer_uptodate(bh))
		{
		  if (chars == blocksize)
		    wait_on_buffer(bh);
		  else
		  {
		    bhlist[0] = bh;
		    if (!filp->f_reada || !read_ahead[MAJOR(dev)]) {
		      /* We do this to force the read of a single buffer */
		      blocks = 1;
		    } else {
		      /* Read-ahead before write */
		      blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9) / 2;
		      if (block + blocks > size) blocks = size - block;
		      if (blocks > NBUF) blocks=NBUF;
		      if (!blocks) blocks = 1;
		      for(i=1; i<blocks; i++)
		      {
		        bhlist[i] = getblk (dev, block+i, blocksize);
		        if (!bhlist[i])
			{
			  while(i >= 0) brelse(bhlist[i--]);
			  return written ? written : -EIO;
		        }
		      }
		    }
		    ll_rw_block(READ, blocks, bhlist);
		    for(i=1; i<blocks; i++) brelse(bhlist[i]);
		    wait_on_buffer(bh);
		    if (!buffer_uptodate(bh)) {
			  brelse(bh);
			  return written ? written : -EIO;
		    }
		  };
		};
#endif
		block++;
		p = offset + bh->b_data;
		offset = 0;
		*ppos += chars;
		written += chars;
		count -= chars;
		copy_from_user(p,buf,chars);
		p += chars;
		buf += chars;
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh);
		if (filp->f_flags & O_SYNC)
			bufferlist[buffercount++] = bh;
		else
			brelse(bh);
		if (buffercount == NBUF){
			ll_rw_block(WRITE, buffercount, bufferlist);
			for(i=0; i<buffercount; i++){
				wait_on_buffer(bufferlist[i]);
				if (!buffer_uptodate(bufferlist[i]))
					write_error=1;
				brelse(bufferlist[i]);
			}
			buffercount=0;
		}
		balance_dirty(dev);
		if (write_error)
			break;
	}
	if ( buffercount ){
		ll_rw_block(WRITE, buffercount, bufferlist);
		for(i=0; i<buffercount; i++){
			wait_on_buffer(bufferlist[i]);
			if (!buffer_uptodate(bufferlist[i]))
				write_error=1;
			brelse(bufferlist[i]);
		}
	}		
	filp->f_reada = 1;
	if(write_error)
		return -EIO;
	return written;
}

ssize_t block_read(struct file * filp, char * buf, size_t count, loff_t *ppos)
{
	struct inode * inode = filp->f_dentry->d_inode;
	size_t block;
	loff_t offset;
	ssize_t blocksize;
	ssize_t blocksize_bits, i;
	size_t blocks, rblocks, left;
	int bhrequest, uptodate;
	struct buffer_head ** bhb, ** bhe;
	struct buffer_head * buflist[NBUF];
	struct buffer_head * bhreq[NBUF];
	unsigned int chars;
	loff_t size;
	kdev_t dev;
	ssize_t read;

	dev = inode->i_rdev;
	blocksize = BLOCK_SIZE;
	if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
		blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];
	i = blocksize;
	blocksize_bits = 0;
	while (i != 1) {
		blocksize_bits++;
		i >>= 1;
	}

	offset = *ppos;
	if (blk_size[MAJOR(dev)])
		size = (loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS;
	else
		size = (loff_t) INT_MAX << BLOCK_SIZE_BITS;

	if (offset > size)
		left = 0;
	/* size - offset might not fit into left, so check explicitly. */
	else if (size - offset > INT_MAX)
		left = INT_MAX;
	else
		left = size - offset;
	if (left > count)
		left = count;
	if (left <= 0)
		return 0;
	read = 0;
	block = offset >> blocksize_bits;
	offset &= blocksize-1;
	size >>= blocksize_bits;
	rblocks = blocks = (left + offset + blocksize - 1) >> blocksize_bits;
	bhb = bhe = buflist;
	if (filp->f_reada) {
	        if (blocks < read_ahead[MAJOR(dev)] / (blocksize >> 9))
			blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9);
		if (rblocks > blocks)
			blocks = rblocks;
		
	}
	if (block + blocks > size) {
		blocks = size - block;
		if (blocks == 0)
			return 0;
	}

	/* We do this in a two stage process.  We first try to request
	   as many blocks as we can, then we wait for the first one to
	   complete, and then we try to wrap up as many as are actually
	   done.  This routine is rather generic, in that it can be used
	   in a filesystem by substituting the appropriate function in
	   for getblk.

	   This routine is optimized to make maximum use of the various
	   buffers and caches. */

	do {
		bhrequest = 0;
		uptodate = 1;
		while (blocks) {
			--blocks;
			*bhb = getblk(dev, block++, blocksize);
			if (*bhb && !buffer_uptodate(*bhb)) {
				uptodate = 0;
				bhreq[bhrequest++] = *bhb;
			}

			if (++bhb == &buflist[NBUF])
				bhb = buflist;

			/* If the block we have on hand is uptodate, go ahead
			   and complete processing. */
			if (uptodate)
				break;
			if (bhb == bhe)
				break;
		}

		/* Now request them all */
		if (bhrequest) {
			ll_rw_block(READ, bhrequest, bhreq);
		}

		do { /* Finish off all I/O that has actually completed */
			if (*bhe) {
				wait_on_buffer(*bhe);
				if (!buffer_uptodate(*bhe)) {	/* read error? */
				        brelse(*bhe);
					if (++bhe == &buflist[NBUF])
					  bhe = buflist;
					left = 0;
					break;
				}
			}			
			if (left < blocksize - offset)
				chars = left;
			else
				chars = blocksize - offset;
			*ppos += chars;
			left -= chars;
			read += chars;
			if (*bhe) {
				copy_to_user(buf,offset+(*bhe)->b_data,chars);
				brelse(*bhe);
				buf += chars;
			} else {
				while (chars-- > 0)
					put_user(0,buf++);
			}
			offset = 0;
			if (++bhe == &buflist[NBUF])
				bhe = buflist;
		} while (left > 0 && bhe != bhb && (!*bhe || !buffer_locked(*bhe)));
		if (bhe == bhb && !blocks)
			break;
	} while (left > 0);

/* Release the read-ahead blocks */
	while (bhe != bhb) {
		brelse(*bhe);
		if (++bhe == &buflist[NBUF])
			bhe = buflist;
	};
	if (!read)
		return -EIO;
	filp->f_reada = 1;
	return read;
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
	

/*
 *	Filp may be NULL when we are called by an msync of a vma
 *	since the vma has no handle.
 */
 
static int block_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	return fsync_dev(dentry->d_inode->i_rdev);
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

void __init bdev_init(void)
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
		spin_lock(&bdev_lock);
		if (atomic_read(&bdev->bd_openers))
			BUG();
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
	struct super_block * sb;

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

	sb = get_super(dev);
	if (sb && invalidate_inodes(sb))
		printk("VFS: busy inodes on changed media.\n");

	destroy_buffers(dev);

	if (bdops->revalidate)
		bdops->revalidate(dev);
	return 1;
}

int ioctl_by_bdev(struct block_device *bdev, unsigned cmd, unsigned long arg)
{
	kdev_t rdev = to_kdev_t(bdev->bd_dev);
	struct inode inode_fake;
	int res;
	mm_segment_t old_fs = get_fs();

	if (!bdev->bd_op->ioctl)
		return -EINVAL;
	inode_fake.i_rdev=rdev;
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
			if (!ret)
				atomic_inc(&bdev->bd_openers);
			else if (!atomic_read(&bdev->bd_openers))
				bdev->bd_op = NULL;
			iput(fake_inode);
		}
	}
	up(&bdev->bd_sem);
	return ret;
}

int blkdev_open(struct inode * inode, struct file * filp)
{
	int ret = -ENXIO;
	struct block_device *bdev = inode->i_bdev;
	down(&bdev->bd_sem);
	lock_kernel();
	if (!bdev->bd_op)
		bdev->bd_op = get_blkfops(MAJOR(inode->i_rdev));
	if (bdev->bd_op) {
		ret = 0;
		if (bdev->bd_op->open)
			ret = bdev->bd_op->open(inode,filp);
		if (!ret)
			atomic_inc(&bdev->bd_openers);
		else if (!atomic_read(&bdev->bd_openers))
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
	/* syncing will go here */
	lock_kernel();
	if (kind == BDEV_FILE || kind == BDEV_FS)
		fsync_dev(rdev);
	if (atomic_dec_and_test(&bdev->bd_openers)) {
		/* invalidating buffers will go here */
		invalidate_buffers(rdev);
	}
	if (bdev->bd_op->release) {
		struct inode * fake_inode = get_empty_inode();
		ret = -ENOMEM;
		if (fake_inode) {
			fake_inode->i_rdev = rdev;
			ret = bdev->bd_op->release(fake_inode, NULL);
			iput(fake_inode);
		}
	}
	if (!atomic_read(&bdev->bd_openers))
		bdev->bd_op = NULL;	/* we can't rely on driver being */
					/* kind to stay around. */
	unlock_kernel();
	up(&bdev->bd_sem);
	return ret;
}

static int blkdev_close(struct inode * inode, struct file * filp)
{
	return blkdev_put(inode->i_bdev, BDEV_FILE);
}

static int blkdev_ioctl(struct inode *inode, struct file *file, unsigned cmd,
			unsigned long arg)
{
	if (inode->i_bdev->bd_op->ioctl)
		return inode->i_bdev->bd_op->ioctl(inode, file, cmd, arg);
	return -EINVAL;
}

struct file_operations def_blk_fops = {
	open:		blkdev_open,
	release:	blkdev_close,
	llseek:		block_llseek,
	read:		block_read,
	write:		block_write,
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

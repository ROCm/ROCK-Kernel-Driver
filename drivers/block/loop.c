/*
 *  linux/drivers/block/loop.c
 *
 *  Written by Theodore Ts'o, 3/29/93
 *
 * Copyright 1993 by Theodore Ts'o.  Redistribution of this file is
 * permitted under the GNU General Public License.
 *
 * DES encryption plus some minor changes by Werner Almesberger, 30-MAY-1993
 * more DES encryption plus IDEA encryption by Nicholas J. Leon, June 20, 1996
 *
 * Modularized and updated for 1.1.16 kernel - Mitch Dsouza 28th May 1994
 * Adapted for 1.3.59 kernel - Andries Brouwer, 1 Feb 1996
 *
 * Fixed do_loop_request() re-entrancy - Vincent.Renardias@waw.com Mar 20, 1997
 *
 * Added devfs support - Richard Gooch <rgooch@atnf.csiro.au> 16-Jan-1998
 *
 * Handle sparse backing files correctly - Kenn Humborg, Jun 28, 1998
 *
 * Loadable modules and other fixes by AK, 1998
 *
 * Make real block number available to downstream transfer functions, enables
 * CBC (and relatives) mode encryption requiring unique IVs per data block.
 * Reed H. Petty, rhp@draper.net
 *
 * Maximum number of loop devices now dynamic via max_loop module parameter.
 * Russell Kroll <rkroll@exploits.org> 19990701
 *
 * Maximum number of loop devices when compiled-in now selectable by passing
 * max_loop=<1-255> to the kernel on boot.
 * Erik I. Bolsø, <eriki@himolde.no>, Oct 31, 1999
 *
 * Completely rewrite request handling to be make_request_fn style and
 * non blocking, pushing work to a helper thread. Lots of fixes from
 * Al Viro too.
 * Jens Axboe <axboe@suse.de>, Nov 2000
 *
 * Support up to 256 loop devices
 * Heinz Mauelshagen <mge@sistina.com>, Feb 2002
 *
 * Still To Fix:
 * - Advisory locking is ignored here.
 * - Should use an own CAP_* category instead of CAP_SYS_ADMIN
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <linux/blkpg.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/loop.h>
#include <linux/suspend.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>		/* for invalidate_bdev() */

#include <asm/uaccess.h>

static int max_loop = 8;
static struct loop_device *loop_dev;
static struct gendisk **disks;

/*
 * Transfer functions
 */
static int transfer_none(struct loop_device *lo, int cmd, char *raw_buf,
			 char *loop_buf, int size, sector_t real_block)
{
	if (raw_buf != loop_buf) {
		if (cmd == READ)
			memcpy(loop_buf, raw_buf, size);
		else
			memcpy(raw_buf, loop_buf, size);
	}

	return 0;
}

static int transfer_xor(struct loop_device *lo, int cmd, char *raw_buf,
			char *loop_buf, int size, sector_t real_block)
{
	char	*in, *out, *key;
	int	i, keysize;

	if (cmd == READ) {
		in = raw_buf;
		out = loop_buf;
	} else {
		in = loop_buf;
		out = raw_buf;
	}

	key = lo->lo_encrypt_key;
	keysize = lo->lo_encrypt_key_size;
	for (i = 0; i < size; i++)
		*out++ = *in++ ^ key[(i & 511) % keysize];
	return 0;
}

static int xor_init(struct loop_device *lo, const struct loop_info64 *info)
{
	if (info->lo_encrypt_key_size <= 0)
		return -EINVAL;
	return 0;
}

static struct loop_func_table none_funcs = {
	.number = LO_CRYPT_NONE,
	.transfer = transfer_none,
}; 	

static struct loop_func_table xor_funcs = {
	.number = LO_CRYPT_XOR,
	.transfer = transfer_xor,
	.init = xor_init
}; 	

/* xfer_funcs[0] is special - its release function is never called */
static struct loop_func_table *xfer_funcs[MAX_LO_CRYPT] = {
	&none_funcs,
	&xor_funcs
};

static int
figure_loop_size(struct loop_device *lo)
{
	loff_t size, offset, loopsize;
	sector_t x;

	/* Compute loopsize in bytes */
	size = i_size_read(lo->lo_backing_file->f_dentry->
				d_inode->i_mapping->host);
	offset = lo->lo_offset;
	loopsize = size - offset;
	if (lo->lo_sizelimit > 0 && lo->lo_sizelimit < loopsize)
		loopsize = lo->lo_sizelimit;

	/*
	 * Unfortunately, if we want to do I/O on the device,
	 * the number of 512-byte sectors has to fit into a sector_t.
	 */
	size = loopsize >> 9;
	x = (sector_t)size;

	if ((loff_t)x != size)
		return -EFBIG;

	set_capacity(disks[lo->lo_number], x);
	return 0;					
}

static inline int
lo_do_transfer(struct loop_device *lo, int cmd, char *rbuf,
	       char *lbuf, int size, sector_t rblock)
{
	if (!lo->transfer)
		return 0;

	return lo->transfer(lo, cmd, rbuf, lbuf, size, rblock);
}

static int
do_lo_send(struct loop_device *lo, struct bio_vec *bvec, int bsize, loff_t pos)
{
	struct file *file = lo->lo_backing_file; /* kudos to NFsckingS */
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct address_space_operations *aops = mapping->a_ops;
	struct page *page;
	char *kaddr, *data;
	pgoff_t index;
	unsigned size, offset;
	int len;
	int ret = 0;

	down(&mapping->host->i_sem);
	index = pos >> PAGE_CACHE_SHIFT;
	offset = pos & ((pgoff_t)PAGE_CACHE_SIZE - 1);
	data = kmap(bvec->bv_page) + bvec->bv_offset;
	len = bvec->bv_len;
	while (len > 0) {
		sector_t IV;
		int transfer_result;

		IV = ((sector_t)index << (PAGE_CACHE_SHIFT - 9))+(offset >> 9);

		size = PAGE_CACHE_SIZE - offset;
		if (size > len)
			size = len;

		page = grab_cache_page(mapping, index);
		if (!page)
			goto fail;
		if (aops->prepare_write(file, page, offset, offset+size))
			goto unlock;
		kaddr = kmap(page);
		transfer_result = lo_do_transfer(lo, WRITE, kaddr + offset,
						 data, size, IV);
		if (transfer_result) {
			/*
			 * The transfer failed, but we still write the data to
			 * keep prepare/commit calls balanced.
			 */
			printk(KERN_ERR "loop: transfer error block %llu\n",
			       (unsigned long long)index);
			memset(kaddr + offset, 0, size);
		}
		flush_dcache_page(page);
		kunmap(page);
		if (aops->commit_write(file, page, offset, offset+size))
			goto unlock;
		if (transfer_result)
			goto unlock;
		data += size;
		len -= size;
		offset = 0;
		index++;
		pos += size;
		unlock_page(page);
		page_cache_release(page);
	}
	up(&mapping->host->i_sem);
out:
	kunmap(bvec->bv_page);
	return ret;

unlock:
	unlock_page(page);
	page_cache_release(page);
fail:
	up(&mapping->host->i_sem);
	ret = -1;
	goto out;
}

static int
lo_send(struct loop_device *lo, struct bio *bio, int bsize, loff_t pos)
{
	unsigned vecnr;
	int ret = 0;

	for (vecnr = 0; vecnr < bio->bi_vcnt; vecnr++) {
		struct bio_vec *bvec = &bio->bi_io_vec[vecnr];

		ret = do_lo_send(lo, bvec, bsize, pos);
		if (ret < 0)
			break;
		pos += bvec->bv_len;
	}
	return ret;
}

struct lo_read_data {
	struct loop_device *lo;
	char *data;
	int bsize;
};

static int
lo_read_actor(read_descriptor_t *desc, struct page *page,
	      unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long count = desc->count;
	struct lo_read_data *p = (struct lo_read_data*)desc->buf;
	struct loop_device *lo = p->lo;
	sector_t IV;

	IV = ((sector_t) page->index << (PAGE_CACHE_SHIFT - 9))+(offset >> 9);

	if (size > count)
		size = count;

	kaddr = kmap(page);
	if (lo_do_transfer(lo, READ, kaddr + offset, p->data, size, IV)) {
		size = 0;
		printk(KERN_ERR "loop: transfer error block %ld\n",
		       page->index);
		desc->error = -EINVAL;
	}
	kunmap(page);
	
	desc->count = count - size;
	desc->written += size;
	p->data += size;
	return size;
}

static int
do_lo_receive(struct loop_device *lo,
	      struct bio_vec *bvec, int bsize, loff_t pos)
{
	struct lo_read_data cookie;
	struct file *file;
	int retval;

	cookie.lo = lo;
	cookie.data = kmap(bvec->bv_page) + bvec->bv_offset;
	cookie.bsize = bsize;
	file = lo->lo_backing_file;
	retval = file->f_op->sendfile(file, &pos, bvec->bv_len,
			lo_read_actor, &cookie);
	kunmap(bvec->bv_page);
	return (retval < 0)? retval: 0;
}

static int
lo_receive(struct loop_device *lo, struct bio *bio, int bsize, loff_t pos)
{
	unsigned vecnr;
	int ret = 0;

	for (vecnr = 0; vecnr < bio->bi_vcnt; vecnr++) {
		struct bio_vec *bvec = &bio->bi_io_vec[vecnr];

		ret = do_lo_receive(lo, bvec, bsize, pos);
		if (ret < 0)
			break;
		pos += bvec->bv_len;
	}
	return ret;
}

static int do_bio_filebacked(struct loop_device *lo, struct bio *bio)
{
	loff_t pos;
	int ret;

	pos = ((loff_t) bio->bi_sector << 9) + lo->lo_offset;
	if (bio_rw(bio) == WRITE)
		ret = lo_send(lo, bio, lo->lo_blocksize, pos);
	else
		ret = lo_receive(lo, bio, lo->lo_blocksize, pos);
	return ret;
}

static int loop_end_io_transfer(struct bio *, unsigned int, int);

static void loop_put_buffer(struct bio *bio)
{
	/*
	 * check bi_end_io, may just be a remapped bio
	 */
	if (bio && bio->bi_end_io == loop_end_io_transfer) {
		int i;

		for (i = 0; i < bio->bi_vcnt; i++)
			__free_page(bio->bi_io_vec[i].bv_page);

		bio_put(bio);
	}
}

/*
 * Add bio to back of pending list
 */
static void loop_add_bio(struct loop_device *lo, struct bio *bio)
{
	unsigned long flags;

	spin_lock_irqsave(&lo->lo_lock, flags);
	if (lo->lo_biotail) {
		lo->lo_biotail->bi_next = bio;
		lo->lo_biotail = bio;
	} else
		lo->lo_bio = lo->lo_biotail = bio;
	spin_unlock_irqrestore(&lo->lo_lock, flags);

	up(&lo->lo_bh_mutex);
}

/*
 * Grab first pending buffer
 */
static struct bio *loop_get_bio(struct loop_device *lo)
{
	struct bio *bio;

	spin_lock_irq(&lo->lo_lock);
	if ((bio = lo->lo_bio)) {
		if (bio == lo->lo_biotail)
			lo->lo_biotail = NULL;
		lo->lo_bio = bio->bi_next;
		bio->bi_next = NULL;
	}
	spin_unlock_irq(&lo->lo_lock);

	return bio;
}

/*
 * if this was a WRITE lo->transfer stuff has already been done. for READs,
 * queue it for the loop thread and let it do the transfer out of
 * bi_end_io context (we don't want to do decrypt of a page with irqs
 * disabled)
 */
static int loop_end_io_transfer(struct bio *bio, unsigned int bytes_done, int err)
{
	struct bio *rbh = bio->bi_private;
	struct loop_device *lo = rbh->bi_bdev->bd_disk->private_data;

	if (bio->bi_size)
		return 1;

	if (err || bio_rw(bio) == WRITE) {
		bio_endio(rbh, rbh->bi_size, err);
		if (atomic_dec_and_test(&lo->lo_pending))
			up(&lo->lo_bh_mutex);
		loop_put_buffer(bio);
	} else
		loop_add_bio(lo, bio);

	return 0;
}

static struct bio *loop_copy_bio(struct bio *rbh)
{
	struct bio *bio;
	struct bio_vec *bv;
	int i;

	bio = bio_alloc(__GFP_NOWARN, rbh->bi_vcnt);
	if (!bio)
		return NULL;

	/*
	 * iterate iovec list and alloc pages
	 */
	__bio_for_each_segment(bv, rbh, i, 0) {
		struct bio_vec *bbv = &bio->bi_io_vec[i];

		bbv->bv_page = alloc_page(__GFP_NOWARN|__GFP_HIGHMEM);
		if (bbv->bv_page == NULL)
			goto oom;

		bbv->bv_len = bv->bv_len;
		bbv->bv_offset = bv->bv_offset;
	}

	bio->bi_vcnt = rbh->bi_vcnt;
	bio->bi_size = rbh->bi_size;

	return bio;

oom:
	while (--i >= 0)
		__free_page(bio->bi_io_vec[i].bv_page);

	bio_put(bio);
	return NULL;
}

static struct bio *loop_get_buffer(struct loop_device *lo, struct bio *rbh)
{
	struct bio *bio;

	/*
	 * When called on the page reclaim -> writepage path, this code can
	 * trivially consume all memory.  So we drop PF_MEMALLOC to avoid
	 * stealing all the page reserves and throttle to the writeout rate.
	 * pdflush will have been woken by page reclaim.  Let it do its work.
	 */
	do {
		int flags = current->flags;

		current->flags &= ~PF_MEMALLOC;
		bio = loop_copy_bio(rbh);
		if (flags & PF_MEMALLOC)
			current->flags |= PF_MEMALLOC;

		if (bio == NULL)
			blk_congestion_wait(WRITE, HZ/10);
	} while (bio == NULL);

	bio->bi_end_io = loop_end_io_transfer;
	bio->bi_private = rbh;
	bio->bi_sector = rbh->bi_sector + (lo->lo_offset >> 9);
	bio->bi_rw = rbh->bi_rw;
	bio->bi_bdev = lo->lo_device;

	return bio;
}

static int loop_transfer_bio(struct loop_device *lo,
			     struct bio *to_bio, struct bio *from_bio)
{
	sector_t IV;
	struct bio_vec *from_bvec, *to_bvec;
	char *vto, *vfrom;
	int ret = 0, i;

	IV = from_bio->bi_sector + (lo->lo_offset >> 9);

	__bio_for_each_segment(from_bvec, from_bio, i, 0) {
		to_bvec = &to_bio->bi_io_vec[i];

		kmap(from_bvec->bv_page);
		kmap(to_bvec->bv_page);
		vfrom = page_address(from_bvec->bv_page) + from_bvec->bv_offset;
		vto = page_address(to_bvec->bv_page) + to_bvec->bv_offset;
		ret |= lo_do_transfer(lo, bio_data_dir(to_bio), vto, vfrom,
					from_bvec->bv_len, IV);
		kunmap(from_bvec->bv_page);
		kunmap(to_bvec->bv_page);
		IV += from_bvec->bv_len >> 9;
	}

	return ret;
}
		
static int loop_make_request(request_queue_t *q, struct bio *old_bio)
{
	struct bio *new_bio = NULL;
	struct loop_device *lo = q->queuedata;
	int rw = bio_rw(old_bio);

	if (!lo)
		goto out;

	spin_lock_irq(&lo->lo_lock);
	if (lo->lo_state != Lo_bound)
		goto inactive;
	atomic_inc(&lo->lo_pending);
	spin_unlock_irq(&lo->lo_lock);

	if (rw == WRITE) {
		if (lo->lo_flags & LO_FLAGS_READ_ONLY)
			goto err;
	} else if (rw == READA) {
		rw = READ;
	} else if (rw != READ) {
		printk(KERN_ERR "loop: unknown command (%x)\n", rw);
		goto err;
	}

	/*
	 * file backed, queue for loop_thread to handle
	 */
	if (lo->lo_flags & LO_FLAGS_DO_BMAP) {
		loop_add_bio(lo, old_bio);
		return 0;
	}

	/*
	 * piggy old buffer on original, and submit for I/O
	 */
	new_bio = loop_get_buffer(lo, old_bio);
	if (rw == WRITE) {
		if (loop_transfer_bio(lo, new_bio, old_bio))
			goto err;
	}

	generic_make_request(new_bio);
	return 0;

err:
	if (atomic_dec_and_test(&lo->lo_pending))
		up(&lo->lo_bh_mutex);
	loop_put_buffer(new_bio);
out:
	bio_io_error(old_bio, old_bio->bi_size);
	return 0;
inactive:
	spin_unlock_irq(&lo->lo_lock);
	goto out;
}

static inline void loop_handle_bio(struct loop_device *lo, struct bio *bio)
{
	int ret;

	/*
	 * For block backed loop, we know this is a READ
	 */
	if (lo->lo_flags & LO_FLAGS_DO_BMAP) {
		ret = do_bio_filebacked(lo, bio);
		bio_endio(bio, bio->bi_size, ret);
	} else {
		struct bio *rbh = bio->bi_private;

		ret = loop_transfer_bio(lo, bio, rbh);

		bio_endio(rbh, rbh->bi_size, ret);
		loop_put_buffer(bio);
	}
}

/*
 * worker thread that handles reads/writes to file backed loop devices,
 * to avoid blocking in our make_request_fn. it also does loop decrypting
 * on reads for block backed loop, as that is too heavy to do from
 * b_end_io context where irqs may be disabled.
 */
static int loop_thread(void *data)
{
	struct loop_device *lo = data;
	struct bio *bio;

	daemonize("loop%d", lo->lo_number);

	/*
	 * loop can be used in an encrypted device,
	 * hence, it mustn't be stopped at all
	 * because it could be indirectly used during suspension
	 */
	current->flags |= PF_IOTHREAD;

	set_user_nice(current, -20);

	lo->lo_state = Lo_bound;
	atomic_inc(&lo->lo_pending);

	/*
	 * up sem, we are running
	 */
	up(&lo->lo_sem);

	for (;;) {
		down_interruptible(&lo->lo_bh_mutex);
		/*
		 * could be upped because of tear-down, not because of
		 * pending work
		 */
		if (!atomic_read(&lo->lo_pending))
			break;

		bio = loop_get_bio(lo);
		if (!bio) {
			printk("loop: missing bio\n");
			continue;
		}
		loop_handle_bio(lo, bio);

		/*
		 * upped both for pending work and tear-down, lo_pending
		 * will hit zero then
		 */
		if (atomic_dec_and_test(&lo->lo_pending))
			break;
	}

	up(&lo->lo_sem);
	return 0;
}

static int loop_set_fd(struct loop_device *lo, struct file *lo_file,
		       struct block_device *bdev, unsigned int arg)
{
	struct file	*file;
	struct inode	*inode;
	struct block_device *lo_device = NULL;
	unsigned lo_blocksize;
	int		lo_flags = 0;
	int		error;

	/* This is safe, since we have a reference from open(). */
	__module_get(THIS_MODULE);

	error = -EBUSY;
	if (lo->lo_state != Lo_unbound)
		goto out;

	error = -EBADF;
	file = fget(arg);
	if (!file)
		goto out;

	error = -EINVAL;
	inode = file->f_dentry->d_inode;

	if (!(file->f_mode & FMODE_WRITE))
		lo_flags |= LO_FLAGS_READ_ONLY;

	if (S_ISBLK(inode->i_mode)) {
		lo_device = inode->i_bdev;
		if (lo_device == bdev) {
			error = -EBUSY;
			goto out;
		}
		lo_blocksize = block_size(lo_device);
		if (bdev_read_only(lo_device))
			lo_flags |= LO_FLAGS_READ_ONLY;
	} else if (S_ISREG(inode->i_mode)) {
		struct address_space_operations *aops = inode->i_mapping->a_ops;
		/*
		 * If we can't read - sorry. If we only can't write - well,
		 * it's going to be read-only.
		 */
		if (!inode->i_fop->sendfile)
			goto out_putf;

		if (!aops->prepare_write || !aops->commit_write)
			lo_flags |= LO_FLAGS_READ_ONLY;

		lo_blocksize = inode->i_blksize;
		lo_flags |= LO_FLAGS_DO_BMAP;
		error = 0;
	} else
		goto out_putf;

	get_file(file);

	if (!(lo_file->f_mode & FMODE_WRITE))
		lo_flags |= LO_FLAGS_READ_ONLY;

	set_device_ro(bdev, (lo_flags & LO_FLAGS_READ_ONLY) != 0);

	lo->lo_blocksize = lo_blocksize;
	lo->lo_device = lo_device;
	lo->lo_flags = lo_flags;
	lo->lo_backing_file = file;
	lo->transfer = NULL;
	lo->ioctl = NULL;
	lo->lo_sizelimit = 0;
	if (figure_loop_size(lo)) {
		error = -EFBIG;
		fput(file);
		goto out_putf;
	}
	lo->old_gfp_mask = mapping_gfp_mask(inode->i_mapping);
	mapping_set_gfp_mask(inode->i_mapping,
			     lo->old_gfp_mask & ~(__GFP_IO|__GFP_FS));

	set_blocksize(bdev, lo_blocksize);

	lo->lo_bio = lo->lo_biotail = NULL;

	/*
	 * set queue make_request_fn, and add limits based on lower level
	 * device
	 */
	blk_queue_make_request(lo->lo_queue, loop_make_request);
	lo->lo_queue->queuedata = lo;

	/*
	 * we remap to a block device, make sure we correctly stack limits
	 */
	if (S_ISBLK(inode->i_mode)) {
		request_queue_t *q = bdev_get_queue(lo_device);

		blk_queue_max_sectors(lo->lo_queue, q->max_sectors);
		blk_queue_max_phys_segments(lo->lo_queue,q->max_phys_segments);
		blk_queue_max_hw_segments(lo->lo_queue, q->max_hw_segments);
		blk_queue_max_segment_size(lo->lo_queue, q->max_segment_size);
		blk_queue_segment_boundary(lo->lo_queue, q->seg_boundary_mask);
		blk_queue_merge_bvec(lo->lo_queue, q->merge_bvec_fn);
	}

	kernel_thread(loop_thread, lo, CLONE_KERNEL);
	down(&lo->lo_sem);

	fput(file);
	return 0;

 out_putf:
	fput(file);
 out:
	/* This is safe: open() is still holding a reference. */
	module_put(THIS_MODULE);
	return error;
}

static int
loop_release_xfer(struct loop_device *lo)
{
	int err = 0;
	struct loop_func_table *xfer = lo->lo_encryption;

	if (xfer) {
		if (xfer->release)
			err = xfer->release(lo);
		lo->transfer = NULL;
		lo->lo_encryption = NULL;
		module_put(xfer->owner);
	}
	return err;
}

static int
loop_init_xfer(struct loop_device *lo, struct loop_func_table *xfer,
	       const struct loop_info64 *i)
{
	int err = 0;

	if (xfer) {
		struct module *owner = xfer->owner;

		if (!try_module_get(owner))
			return -EINVAL;
		if (xfer->init)
			err = xfer->init(lo, i);
		if (err)
			module_put(owner);
		else
			lo->lo_encryption = xfer;
	}
	return err;
}

static int loop_clr_fd(struct loop_device *lo, struct block_device *bdev)
{
	struct file *filp = lo->lo_backing_file;
	int gfp = lo->old_gfp_mask;

	if (lo->lo_state != Lo_bound)
		return -ENXIO;

	if (lo->lo_refcnt > 1)	/* we needed one fd for the ioctl */
		return -EBUSY;

	if (filp == NULL)
		return -EINVAL;

	spin_lock_irq(&lo->lo_lock);
	lo->lo_state = Lo_rundown;
	if (atomic_dec_and_test(&lo->lo_pending))
		up(&lo->lo_bh_mutex);
	spin_unlock_irq(&lo->lo_lock);

	down(&lo->lo_sem);

	lo->lo_backing_file = NULL;

	loop_release_xfer(lo);
	lo->transfer = NULL;
	lo->ioctl = NULL;
	lo->lo_device = NULL;
	lo->lo_encryption = NULL;
	lo->lo_offset = 0;
	lo->lo_sizelimit = 0;
	lo->lo_encrypt_key_size = 0;
	lo->lo_flags = 0;
	memset(lo->lo_encrypt_key, 0, LO_KEY_SIZE);
	memset(lo->lo_crypt_name, 0, LO_NAME_SIZE);
	memset(lo->lo_file_name, 0, LO_NAME_SIZE);
	invalidate_bdev(bdev, 0);
	set_capacity(disks[lo->lo_number], 0);
	mapping_set_gfp_mask(filp->f_dentry->d_inode->i_mapping, gfp);
	lo->lo_state = Lo_unbound;
	fput(filp);
	/* This is safe: open() is still holding a reference. */
	module_put(THIS_MODULE);
	return 0;
}

static int
loop_set_status(struct loop_device *lo, const struct loop_info64 *info)
{
	int err;
	struct loop_func_table *xfer;

	if (lo->lo_encrypt_key_size && lo->lo_key_owner != current->uid &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (lo->lo_state != Lo_bound)
		return -ENXIO;
	if ((unsigned int) info->lo_encrypt_key_size > LO_KEY_SIZE)
		return -EINVAL;

	err = loop_release_xfer(lo);
	if (err)
		return err;

	if (info->lo_encrypt_type) {
		unsigned int type = info->lo_encrypt_type;

		if (type >= MAX_LO_CRYPT)
			return -EINVAL;
		xfer = xfer_funcs[type];
		if (xfer == NULL)
			return -EINVAL;
	} else
		xfer = NULL;

	err = loop_init_xfer(lo, xfer, info);
	if (err)
		return err;

	if (lo->lo_offset != info->lo_offset ||
	    lo->lo_sizelimit != info->lo_sizelimit) {
		lo->lo_offset = info->lo_offset;
		lo->lo_sizelimit = info->lo_sizelimit;
		if (figure_loop_size(lo))
			return -EFBIG;
	}

	memcpy(lo->lo_file_name, info->lo_file_name, LO_NAME_SIZE);
	memcpy(lo->lo_crypt_name, info->lo_crypt_name, LO_NAME_SIZE);
	lo->lo_file_name[LO_NAME_SIZE-1] = 0;
	lo->lo_crypt_name[LO_NAME_SIZE-1] = 0;

	if (!xfer)
		xfer = &none_funcs;
	lo->transfer = xfer->transfer;
	lo->ioctl = xfer->ioctl;

	lo->lo_encrypt_key_size = info->lo_encrypt_key_size;
	lo->lo_init[0] = info->lo_init[0];
	lo->lo_init[1] = info->lo_init[1];
	if (info->lo_encrypt_key_size) {
		memcpy(lo->lo_encrypt_key, info->lo_encrypt_key,
		       info->lo_encrypt_key_size);
		lo->lo_key_owner = current->uid;
	}	

	return 0;
}

static int
loop_get_status(struct loop_device *lo, struct loop_info64 *info)
{
	struct file *file = lo->lo_backing_file;
	struct kstat stat;
	int error;

	if (lo->lo_state != Lo_bound)
		return -ENXIO;
	error = vfs_getattr(file->f_vfsmnt, file->f_dentry, &stat);
	if (error)
		return error;
	memset(info, 0, sizeof(*info));
	info->lo_number = lo->lo_number;
	info->lo_device = huge_encode_dev(stat.dev);
	info->lo_inode = stat.ino;
	info->lo_rdevice = huge_encode_dev(lo->lo_device ? stat.rdev : stat.dev);
	info->lo_offset = lo->lo_offset;
	info->lo_sizelimit = lo->lo_sizelimit;
	info->lo_flags = lo->lo_flags;
	memcpy(info->lo_file_name, lo->lo_file_name, LO_NAME_SIZE);
	memcpy(info->lo_crypt_name, lo->lo_crypt_name, LO_NAME_SIZE);
	info->lo_encrypt_type =
		lo->lo_encryption ? lo->lo_encryption->number : 0;
	if (lo->lo_encrypt_key_size && capable(CAP_SYS_ADMIN)) {
		info->lo_encrypt_key_size = lo->lo_encrypt_key_size;
		memcpy(info->lo_encrypt_key, lo->lo_encrypt_key,
		       lo->lo_encrypt_key_size);
	}
	return 0;
}

static void
loop_info64_from_old(const struct loop_info *info, struct loop_info64 *info64)
{
	memset(info64, 0, sizeof(*info64));
	info64->lo_number = info->lo_number;
	info64->lo_device = info->lo_device;
	info64->lo_inode = info->lo_inode;
	info64->lo_rdevice = info->lo_rdevice;
	info64->lo_offset = info->lo_offset;
	info64->lo_sizelimit = 0;
	info64->lo_encrypt_type = info->lo_encrypt_type;
	info64->lo_encrypt_key_size = info->lo_encrypt_key_size;
	info64->lo_flags = info->lo_flags;
	info64->lo_init[0] = info->lo_init[0];
	info64->lo_init[1] = info->lo_init[1];
	if (info->lo_encrypt_type == LO_CRYPT_CRYPTOAPI)
		memcpy(info64->lo_crypt_name, info->lo_name, LO_NAME_SIZE);
	else
		memcpy(info64->lo_file_name, info->lo_name, LO_NAME_SIZE);
	memcpy(info64->lo_encrypt_key, info->lo_encrypt_key, LO_KEY_SIZE);
}

static int
loop_info64_to_old(const struct loop_info64 *info64, struct loop_info *info)
{
	memset(info, 0, sizeof(*info));
	info->lo_number = info64->lo_number;
	info->lo_device = info64->lo_device;
	info->lo_inode = info64->lo_inode;
	info->lo_rdevice = info64->lo_rdevice;
	info->lo_offset = info64->lo_offset;
	info->lo_encrypt_type = info64->lo_encrypt_type;
	info->lo_encrypt_key_size = info64->lo_encrypt_key_size;
	info->lo_flags = info64->lo_flags;
	info->lo_init[0] = info64->lo_init[0];
	info->lo_init[1] = info64->lo_init[1];
	if (info->lo_encrypt_type == LO_CRYPT_CRYPTOAPI)
		memcpy(info->lo_name, info64->lo_crypt_name, LO_NAME_SIZE);
	else
		memcpy(info->lo_name, info64->lo_file_name, LO_NAME_SIZE);
	memcpy(info->lo_encrypt_key, info64->lo_encrypt_key, LO_KEY_SIZE);

	/* error in case values were truncated */
	if (info->lo_device != info64->lo_device ||
	    info->lo_rdevice != info64->lo_rdevice ||
	    info->lo_inode != info64->lo_inode ||
	    info->lo_offset != info64->lo_offset)
		return -EOVERFLOW;

	return 0;
}

static int
loop_set_status_old(struct loop_device *lo, const struct loop_info *arg)
{
	struct loop_info info;
	struct loop_info64 info64;

	if (copy_from_user(&info, arg, sizeof (struct loop_info)))
		return -EFAULT;
	loop_info64_from_old(&info, &info64);
	return loop_set_status(lo, &info64);
}

static int
loop_set_status64(struct loop_device *lo, const struct loop_info64 *arg)
{
	struct loop_info64 info64;

	if (copy_from_user(&info64, arg, sizeof (struct loop_info64)))
		return -EFAULT;
	return loop_set_status(lo, &info64);
}

static int
loop_get_status_old(struct loop_device *lo, struct loop_info *arg) {
	struct loop_info info;
	struct loop_info64 info64;
	int err = 0;

	if (!arg)
		err = -EINVAL;
	if (!err)
		err = loop_get_status(lo, &info64);
	if (!err)
		err = loop_info64_to_old(&info64, &info);
	if (!err && copy_to_user(arg, &info, sizeof(info)))
		err = -EFAULT;

	return err;
}

static int
loop_get_status64(struct loop_device *lo, struct loop_info64 *arg) {
	struct loop_info64 info64;
	int err = 0;

	if (!arg)
		err = -EINVAL;
	if (!err)
		err = loop_get_status(lo, &info64);
	if (!err && copy_to_user(arg, &info64, sizeof(info64)))
		err = -EFAULT;

	return err;
}

static int lo_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct loop_device *lo = inode->i_bdev->bd_disk->private_data;
	int err;

	down(&lo->lo_ctl_mutex);
	switch (cmd) {
	case LOOP_SET_FD:
		err = loop_set_fd(lo, file, inode->i_bdev, arg);
		break;
	case LOOP_CLR_FD:
		err = loop_clr_fd(lo, inode->i_bdev);
		break;
	case LOOP_SET_STATUS:
		err = loop_set_status_old(lo, (struct loop_info *) arg);
		break;
	case LOOP_GET_STATUS:
		err = loop_get_status_old(lo, (struct loop_info *) arg);
		break;
	case LOOP_SET_STATUS64:
		err = loop_set_status64(lo, (struct loop_info64 *) arg);
		break;
	case LOOP_GET_STATUS64:
		err = loop_get_status64(lo, (struct loop_info64 *) arg);
		break;
	default:
		err = lo->ioctl ? lo->ioctl(lo, cmd, arg) : -EINVAL;
	}
	up(&lo->lo_ctl_mutex);
	return err;
}

static int lo_open(struct inode *inode, struct file *file)
{
	struct loop_device *lo = inode->i_bdev->bd_disk->private_data;

	down(&lo->lo_ctl_mutex);
	lo->lo_refcnt++;
	up(&lo->lo_ctl_mutex);

	return 0;
}

static int lo_release(struct inode *inode, struct file *file)
{
	struct loop_device *lo = inode->i_bdev->bd_disk->private_data;

	down(&lo->lo_ctl_mutex);
	--lo->lo_refcnt;
	up(&lo->lo_ctl_mutex);

	return 0;
}

static struct block_device_operations lo_fops = {
	.owner =	THIS_MODULE,
	.open =		lo_open,
	.release =	lo_release,
	.ioctl =	lo_ioctl,
};

/*
 * And now the modules code and kernel interface.
 */
MODULE_PARM(max_loop, "i");
MODULE_PARM_DESC(max_loop, "Maximum number of loop devices (1-256)");
MODULE_LICENSE("GPL");

int loop_register_transfer(struct loop_func_table *funcs)
{
	unsigned int n = funcs->number;

	if (n >= MAX_LO_CRYPT || xfer_funcs[n])
		return -EINVAL;
	xfer_funcs[n] = funcs;
	return 0;
}

int loop_unregister_transfer(int number)
{
	unsigned int n = number;
	struct loop_device *lo;
	struct loop_func_table *xfer;

	if (n == 0 || n >= MAX_LO_CRYPT || (xfer = xfer_funcs[n]) == NULL)
		return -EINVAL;

	xfer_funcs[n] = NULL;

	for (lo = &loop_dev[0]; lo < &loop_dev[max_loop]; lo++) {
		down(&lo->lo_ctl_mutex);

		if (lo->lo_encryption == xfer)
			loop_release_xfer(lo);

		up(&lo->lo_ctl_mutex);
	}

	return 0;
}

EXPORT_SYMBOL(loop_register_transfer);
EXPORT_SYMBOL(loop_unregister_transfer);

int __init loop_init(void)
{
	int	i;

	if (max_loop < 1 || max_loop > 256) {
		printk(KERN_WARNING "loop: invalid max_loop (must be between"
				    " 1 and 256), using default (8)\n");
		max_loop = 8;
	}

	if (register_blkdev(LOOP_MAJOR, "loop"))
		return -EIO;

	loop_dev = kmalloc(max_loop * sizeof(struct loop_device), GFP_KERNEL);
	if (!loop_dev)
		goto out_mem1;
	memset(loop_dev, 0, max_loop * sizeof(struct loop_device));

	disks = kmalloc(max_loop * sizeof(struct gendisk *), GFP_KERNEL);
	if (!disks)
		goto out_mem2;

	for (i = 0; i < max_loop; i++) {
		disks[i] = alloc_disk(1);
		if (!disks[i])
			goto out_mem3;
	}

	devfs_mk_dir("loop");

	for (i = 0; i < max_loop; i++) {
		struct loop_device *lo = &loop_dev[i];
		struct gendisk *disk = disks[i];

		memset(lo, 0, sizeof(*lo));
		lo->lo_queue = blk_alloc_queue(GFP_KERNEL);
		if (!lo->lo_queue)
			goto out_mem4;
		disks[i]->queue = lo->lo_queue;
		init_MUTEX(&lo->lo_ctl_mutex);
		init_MUTEX_LOCKED(&lo->lo_sem);
		init_MUTEX_LOCKED(&lo->lo_bh_mutex);
		lo->lo_number = i;
		spin_lock_init(&lo->lo_lock);
		disk->major = LOOP_MAJOR;
		disk->first_minor = i;
		disk->fops = &lo_fops;
		sprintf(disk->disk_name, "loop%d", i);
		sprintf(disk->devfs_name, "loop/%d", i);
		disk->private_data = lo;
		disk->queue = lo->lo_queue;
		add_disk(disk);
	}
	printk(KERN_INFO "loop: loaded (max %d devices)\n", max_loop);
	return 0;

out_mem4:
	while (i--)
		blk_put_queue(loop_dev[i].lo_queue);
	i = max_loop;
out_mem3:
	while (i--)
		put_disk(disks[i]);
	kfree(disks);
out_mem2:
	kfree(loop_dev);
out_mem1:
	unregister_blkdev(LOOP_MAJOR, "loop");
	printk(KERN_ERR "loop: ran out of memory\n");
	return -ENOMEM;
}

void loop_exit(void)
{
	int i;

	for (i = 0; i < max_loop; i++) {
		del_gendisk(disks[i]);
		blk_put_queue(loop_dev[i].lo_queue);
		put_disk(disks[i]);
	}
	devfs_remove("loop");
	if (unregister_blkdev(LOOP_MAJOR, "loop"))
		printk(KERN_WARNING "loop: cannot unregister blkdev\n");

	kfree(disks);
	kfree(loop_dev);
}

module_init(loop_init);
module_exit(loop_exit);

#ifndef MODULE
static int __init max_loop_setup(char *str)
{
	max_loop = simple_strtol(str, NULL, 0);
	return 1;
}

__setup("max_loop=", max_loop_setup);
#endif

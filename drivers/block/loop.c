/*
 *  linux/drivers/block/loop.c
 *
 *  Written by Theodore Ts'o, 3/29/93
 * 
 * Copyright 1993 by Theodore Ts'o.  Redistribution of this file is
 * permitted under the GNU Public License.
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
 * Still To Fix:
 * - Advisory locking is ignored here. 
 * - Should use an own CAP_* category instead of CAP_SYS_ADMIN 
 * - Should use the underlying filesystems/devices read function if possible
 *   to support read ahead (and for write)
 *
 * WARNING/FIXME:
 * - The block number as IV passing to low level transfer functions is broken:
 *   it passes the underlying device's block number instead of the
 *   offset. This makes it change for a given block when the file is 
 *   moved/restored/copied and also doesn't work over NFS. 
 * AV, Feb 12, 2000: we pass the logical block number now. It fixes the
 *   problem above. Encryption modules that used to rely on the old scheme
 *   should just call ->i_mapping->bmap() to calculate the physical block
 *   number.
 */ 

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/major.h>

#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>

#include <asm/uaccess.h>

#include <linux/loop.h>		

#define MAJOR_NR LOOP_MAJOR

#define DEVICE_NAME "loop"
#define DEVICE_REQUEST do_lo_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)
#define DEVICE_NO_RANDOM
#define TIMEOUT_VALUE (6 * HZ)
#include <linux/blk.h>

#include <linux/malloc.h>
static int max_loop = 8;
static struct loop_device *loop_dev;
static int *loop_sizes;
static int *loop_blksizes;
static devfs_handle_t devfs_handle;      /*  For the directory */

#define FALSE 0
#define TRUE (!FALSE)

/*
 * Transfer functions
 */
static int transfer_none(struct loop_device *lo, int cmd, char *raw_buf,
		  char *loop_buf, int size, int real_block)
{
	if (cmd == READ)
		memcpy(loop_buf, raw_buf, size);
	else
		memcpy(raw_buf, loop_buf, size);
	return 0;
}

static int transfer_xor(struct loop_device *lo, int cmd, char *raw_buf,
		 char *loop_buf, int size, int real_block)
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
	for (i=0; i < size; i++)
		*out++ = *in++ ^ key[(i & 511) % keysize];
	return 0;
}

static int none_status(struct loop_device *lo, struct loop_info *info)
{
	return 0; 
} 

static int xor_status(struct loop_device *lo, struct loop_info *info)
{
	if (info->lo_encrypt_key_size <= 0)
		return -EINVAL;
	return 0;
}

struct loop_func_table none_funcs = { 
	number: LO_CRYPT_NONE,
	transfer: transfer_none,
	init: none_status
}; 	

struct loop_func_table xor_funcs = { 
	number: LO_CRYPT_XOR,
	transfer: transfer_xor,
	init: xor_status
}; 	

/* xfer_funcs[0] is special - its release function is never called */ 
struct loop_func_table *xfer_funcs[MAX_LO_CRYPT] = {
	&none_funcs,
	&xor_funcs  
};

#define MAX_DISK_SIZE 1024*1024*1024

static void figure_loop_size(struct loop_device *lo)
{
	int	size;

	if (S_ISREG(lo->lo_dentry->d_inode->i_mode))
		size = (lo->lo_dentry->d_inode->i_size - lo->lo_offset) >> BLOCK_SIZE_BITS;
	else {
		kdev_t lodev = lo->lo_device;
		if (blk_size[MAJOR(lodev)])
			size = blk_size[MAJOR(lodev)][MINOR(lodev)] -
                                (lo->lo_offset >> BLOCK_SIZE_BITS);
		else
			size = MAX_DISK_SIZE;
	}

	loop_sizes[lo->lo_number] = size;
}

static int lo_send(struct loop_device *lo, char *data, int len, loff_t pos,
	int blksize)
{
	struct file *file = lo->lo_backing_file; /* kudos to NFsckingS */
	struct address_space *mapping = lo->lo_dentry->d_inode->i_mapping;
	struct address_space_operations *aops = mapping->a_ops;
	struct page *page;
	char *kaddr;
	unsigned long index;
	unsigned size, offset;

	index = pos >> PAGE_CACHE_SHIFT;
	offset = pos & (PAGE_CACHE_SIZE - 1);
	while (len > 0) {
		int IV = index * (PAGE_CACHE_SIZE/blksize) + offset/blksize;
		size = PAGE_CACHE_SIZE - offset;
		if (size > len)
			size = len;

		page = grab_cache_page(mapping, index);
		if (!page)
			goto fail;
		if (aops->prepare_write(file, page, offset, offset+size))
			goto unlock;
		kaddr = page_address(page);
		if ((lo->transfer)(lo, WRITE, kaddr+offset, data, size, IV))
			goto write_fail;
		if (aops->commit_write(file, page, offset, offset+size))
			goto unlock;
		data += size;
		len -= size;
		offset = 0;
		index++;
		pos += size;
		UnlockPage(page);
		page_cache_release(page);
	}
	return 0;

write_fail:
	printk(KERN_ERR "loop: transfer error block %ld\n", index);
	ClearPageUptodate(page);
	kunmap(page);
unlock:
	UnlockPage(page);
	page_cache_release(page);
fail:
	return -1;
}

struct lo_read_data {
	struct loop_device *lo;
	char *data;
	int blksize;
};

static int lo_read_actor(read_descriptor_t * desc, struct page *page, unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long count = desc->count;
	struct lo_read_data *p = (struct lo_read_data*)desc->buf;
	struct loop_device *lo = p->lo;
	int IV = page->index * (PAGE_CACHE_SIZE/p->blksize) + offset/p->blksize;

	if (size > count)
		size = count;

	kaddr = kmap(page);
	if ((lo->transfer)(lo,READ,kaddr+offset,p->data,size,IV)) {
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

static int lo_receive(struct loop_device *lo, char *data, int len, loff_t pos,
	int blksize)
{
	struct file *file = lo->lo_backing_file;
	struct lo_read_data cookie;
	read_descriptor_t desc;

	cookie.lo = lo;
	cookie.data = data;
	cookie.blksize = blksize;
	desc.written = 0;
	desc.count = len;
	desc.buf = (char*)&cookie;
	desc.error = 0;
	do_generic_file_read(file, &pos, &desc, lo_read_actor);
	return desc.error;
}

static void do_lo_request(request_queue_t * q)
{
	int	block, offset, len, blksize, size;
	char	*dest_addr;
	struct loop_device *lo;
	struct buffer_head *bh;
	struct request *current_request;
	loff_t pos;

repeat:
	INIT_REQUEST;
	current_request=CURRENT;
	blkdev_dequeue_request(current_request);
	if (MINOR(current_request->rq_dev) >= max_loop)
		goto error_out;
	lo = &loop_dev[MINOR(current_request->rq_dev)];
	if (!lo->lo_dentry || !lo->transfer)
		goto error_out;
	if (current_request->cmd == WRITE) {
		if (lo->lo_flags & LO_FLAGS_READ_ONLY)
			goto error_out;
	} else if (current_request->cmd != READ) {
		printk(KERN_ERR "unknown loop device command (%d)?!?",
		       current_request->cmd);
		goto error_out;
	}

	dest_addr = current_request->buffer;
	len = current_request->current_nr_sectors << 9;

	blksize = BLOCK_SIZE;
	if (blksize_size[MAJOR(lo->lo_device)]) {
	    blksize = blksize_size[MAJOR(lo->lo_device)][MINOR(lo->lo_device)];
	    if (!blksize)
	      blksize = BLOCK_SIZE;
	}

	if (lo->lo_flags & LO_FLAGS_DO_BMAP)
		goto file_backed;

	if (blksize < 512) {
		block = current_request->sector * (512/blksize);
		offset = 0;
	} else {
		block = current_request->sector / (blksize >> 9);
		offset = (current_request->sector % (blksize >> 9)) << 9;
	}
	block += lo->lo_offset / blksize;
	offset += lo->lo_offset % blksize;
	if (offset >= blksize) {
		block++;
		offset -= blksize;
	}
	spin_unlock_irq(&io_request_lock);

	while (len > 0) {

		size = blksize - offset;
		if (size > len)
			size = len;

		bh = getblk(lo->lo_device, block, blksize);
		if (!bh) {
			printk(KERN_ERR "loop: device %s: getblk(-, %d, %d) returned NULL",
				kdevname(lo->lo_device),
				block, blksize);
			goto error_out_lock;
		}
		if (!buffer_uptodate(bh) && ((current_request->cmd == READ) ||
					(offset || (len < blksize)))) {
			ll_rw_block(READ, 1, &bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				brelse(bh);
				goto error_out_lock;
			}
		}

		if ((lo->transfer)(lo, current_request->cmd,
				   bh->b_data + offset,
				   dest_addr, size, block)) {
			printk(KERN_ERR "loop: transfer error block %d\n",
			       block);
			brelse(bh);
			goto error_out_lock;
		}

		if (current_request->cmd == WRITE) {
			mark_buffer_uptodate(bh, 1);
			mark_buffer_dirty(bh);
		}
		brelse(bh);
		dest_addr += size;
		len -= size;
		offset = 0;
		block++;
	}
	goto done;

file_backed:
	pos = ((loff_t)current_request->sector << 9) + lo->lo_offset;
	spin_unlock_irq(&io_request_lock);
	if (current_request->cmd == WRITE) {
		if (lo_send(lo, dest_addr, len, pos, blksize))
			goto error_out_lock;
	} else {
		if (lo_receive(lo, dest_addr, len, pos, blksize))
			goto error_out_lock;
	}
done:
	spin_lock_irq(&io_request_lock);
	current_request->sector += current_request->current_nr_sectors;
	current_request->nr_sectors -= current_request->current_nr_sectors;
	list_add(&current_request->queue, &q->queue_head);
	end_request(1);
	goto repeat;
error_out_lock:
	spin_lock_irq(&io_request_lock);
error_out:
	list_add(&current_request->queue, &q->queue_head);
	end_request(0);
	goto repeat;
}

static int loop_set_fd(struct loop_device *lo, kdev_t dev, unsigned int arg)
{
	struct file	*file;
	struct inode	*inode;
	int error;

	MOD_INC_USE_COUNT;

	error = -EBUSY;
	if (lo->lo_dentry)
		goto out;

	error = -EBADF;
	file = fget(arg);
	if (!file)
		goto out;

	error = -EINVAL;
	inode = file->f_dentry->d_inode;

	if (S_ISBLK(inode->i_mode)) {
		/* dentry will be wired, so... */
		error = blkdev_get(inode->i_bdev, file->f_mode,
				   file->f_flags, BDEV_FILE);

		lo->lo_device = inode->i_rdev;
		lo->lo_flags = 0;

		/* Backed by a block device - don't need to hold onto
		   a file structure */
		lo->lo_backing_file = NULL;

		if (error)
			goto out_putf;
	} else if (S_ISREG(inode->i_mode)) {
		struct address_space_operations *aops;

		aops = inode->i_mapping->a_ops;
		/*
		 * If we can't read - sorry. If we only can't write - well,
		 * it's going to be read-only.
		 */
		error = -EINVAL;
		if (!aops->readpage)
			goto out_putf;

		if (!aops->prepare_write || !aops->commit_write)
			lo->lo_flags |= LO_FLAGS_READ_ONLY;

		error = get_write_access(inode);
		if (error)
			goto out_putf;

		/* Backed by a regular file - we need to hold onto a file
		   structure for this file.  Friggin' NFS can't live without
		   it on write and for reading we use do_generic_file_read(),
		   so...  We create a new file structure based on the one
		   passed to us via 'arg'.  This is to avoid changing the file
		   structure that the caller is using */

		lo->lo_device = inode->i_dev;
		lo->lo_flags |= LO_FLAGS_DO_BMAP;

		error = -ENFILE;
		lo->lo_backing_file = get_empty_filp();
		if (lo->lo_backing_file == NULL) {
			put_write_access(inode);
			goto out_putf;
		}

		lo->lo_backing_file->f_mode = file->f_mode;
		lo->lo_backing_file->f_pos = file->f_pos;
		lo->lo_backing_file->f_flags = file->f_flags;
		lo->lo_backing_file->f_owner = file->f_owner;
		lo->lo_backing_file->f_dentry = file->f_dentry;
		lo->lo_backing_file->f_vfsmnt = mntget(file->f_vfsmnt);
		lo->lo_backing_file->f_op = fops_get(file->f_op);
		lo->lo_backing_file->private_data = file->private_data;
		file_moveto(lo->lo_backing_file, file);

		error = 0;
	}

	if (IS_RDONLY (inode) || is_read_only(lo->lo_device))
		lo->lo_flags |= LO_FLAGS_READ_ONLY;

	set_device_ro(dev, (lo->lo_flags & LO_FLAGS_READ_ONLY)!=0);

	lo->lo_dentry = dget(file->f_dentry);
	lo->transfer = NULL;
	lo->ioctl = NULL;
	figure_loop_size(lo);

 out_putf:
	fput(file);
 out:
	if (error)
		MOD_DEC_USE_COUNT;
	return error;
}

static int loop_release_xfer(struct loop_device *lo)
{
	int err = 0; 
	if (lo->lo_encrypt_type) {
		struct loop_func_table *xfer= xfer_funcs[lo->lo_encrypt_type]; 
		if (xfer && xfer->release)
			err = xfer->release(lo); 
		if (xfer && xfer->unlock)
			xfer->unlock(lo); 
		lo->lo_encrypt_type = 0;
	}
	return err;
}

static int loop_init_xfer(struct loop_device *lo, int type,struct loop_info *i)
{
	int err = 0; 
	if (type) {
		struct loop_func_table *xfer = xfer_funcs[type]; 
		if (xfer->init)
			err = xfer->init(lo, i);
		if (!err) { 
			lo->lo_encrypt_type = type;
			if (xfer->lock)
				xfer->lock(lo);
		}
	}
	return err;
}  

static int loop_clr_fd(struct loop_device *lo, kdev_t dev)
{
	struct dentry *dentry = lo->lo_dentry;

	if (!dentry)
		return -ENXIO;
	if (lo->lo_refcnt > 1)	/* we needed one fd for the ioctl */
		return -EBUSY;

	if (S_ISBLK(dentry->d_inode->i_mode))
		blkdev_put(dentry->d_inode->i_bdev, BDEV_FILE);

	lo->lo_dentry = NULL;

	if (lo->lo_backing_file != NULL) {
		struct file *filp = lo->lo_backing_file;
		if ((filp->f_mode & FMODE_WRITE) == 0)
			put_write_access(filp->f_dentry->d_inode);
		fput(filp);
		lo->lo_backing_file = NULL;
	} else {
		dput(dentry);
	}

	loop_release_xfer(lo);
	lo->transfer = NULL;
	lo->ioctl = NULL;
	lo->lo_device = 0;
	lo->lo_encrypt_type = 0;
	lo->lo_offset = 0;
	lo->lo_encrypt_key_size = 0;
	memset(lo->lo_encrypt_key, 0, LO_KEY_SIZE);
	memset(lo->lo_name, 0, LO_NAME_SIZE);
	loop_sizes[lo->lo_number] = 0;
	invalidate_buffers(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int loop_set_status(struct loop_device *lo, struct loop_info *arg)
{
	struct loop_info info; 
	int err;
	unsigned int type;

	if (lo->lo_encrypt_key_size && lo->lo_key_owner != current->uid && 
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!lo->lo_dentry)
		return -ENXIO;
	if (copy_from_user(&info, arg, sizeof (struct loop_info)))
		return -EFAULT; 
	if ((unsigned int) info.lo_encrypt_key_size > LO_KEY_SIZE)
		return -EINVAL;
	type = info.lo_encrypt_type; 
	if (type >= MAX_LO_CRYPT || xfer_funcs[type] == NULL)
		return -EINVAL;
	if (type == LO_CRYPT_XOR && info.lo_encrypt_key_size == 0)
		return -EINVAL;
	err = loop_release_xfer(lo);
	if (!err) 
		err = loop_init_xfer(lo, type, &info);
	if (err)
		return err;	

	lo->lo_offset = info.lo_offset;
	strncpy(lo->lo_name, info.lo_name, LO_NAME_SIZE);

	lo->transfer = xfer_funcs[type]->transfer;
	lo->ioctl = xfer_funcs[type]->ioctl;
	lo->lo_encrypt_key_size = info.lo_encrypt_key_size;
	lo->lo_init[0] = info.lo_init[0];
	lo->lo_init[1] = info.lo_init[1];
	if (info.lo_encrypt_key_size) {
		memcpy(lo->lo_encrypt_key, info.lo_encrypt_key, 
		       info.lo_encrypt_key_size);
		lo->lo_key_owner = current->uid; 
	}	
	figure_loop_size(lo);
	return 0;
}

static int loop_get_status(struct loop_device *lo, struct loop_info *arg)
{
	struct loop_info	info;

	if (!lo->lo_dentry)
		return -ENXIO;
	if (!arg)
		return -EINVAL;
	memset(&info, 0, sizeof(info));
	info.lo_number = lo->lo_number;
	info.lo_device = kdev_t_to_nr(lo->lo_dentry->d_inode->i_dev);
	info.lo_inode = lo->lo_dentry->d_inode->i_ino;
	info.lo_rdevice = kdev_t_to_nr(lo->lo_device);
	info.lo_offset = lo->lo_offset;
	info.lo_flags = lo->lo_flags;
	strncpy(info.lo_name, lo->lo_name, LO_NAME_SIZE);
	info.lo_encrypt_type = lo->lo_encrypt_type;
	if (lo->lo_encrypt_key_size && capable(CAP_SYS_ADMIN)) {
		info.lo_encrypt_key_size = lo->lo_encrypt_key_size;
		memcpy(info.lo_encrypt_key, lo->lo_encrypt_key,
		       lo->lo_encrypt_key_size);
	}
	return copy_to_user(arg, &info, sizeof(info)) ? -EFAULT : 0;
}

static int lo_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct loop_device *lo;
	int dev;

	if (!inode)
		return -EINVAL;
	if (MAJOR(inode->i_rdev) != MAJOR_NR) {
		printk(KERN_WARNING "lo_ioctl: pseudo-major != %d\n",
		       MAJOR_NR);
		return -ENODEV;
	}
	dev = MINOR(inode->i_rdev);
	if (dev >= max_loop)
		return -ENODEV;
	lo = &loop_dev[dev];
	switch (cmd) {
	case LOOP_SET_FD:
		return loop_set_fd(lo, inode->i_rdev, arg);
	case LOOP_CLR_FD:
		return loop_clr_fd(lo, inode->i_rdev);
	case LOOP_SET_STATUS:
		return loop_set_status(lo, (struct loop_info *) arg);
	case LOOP_GET_STATUS:
		return loop_get_status(lo, (struct loop_info *) arg);
	case BLKGETSIZE:   /* Return device size */
		if (!lo->lo_dentry)
			return -ENXIO;
		if (!arg)
			return -EINVAL;
		return put_user(loop_sizes[lo->lo_number] << 1, (long *) arg);
	default:
		return lo->ioctl ? lo->ioctl(lo, cmd, arg) : -EINVAL;
	}
	return 0;
}

static int lo_open(struct inode *inode, struct file *file)
{
	struct loop_device *lo;
	int	dev, type;


	if (!inode)
		return -EINVAL;
	if (MAJOR(inode->i_rdev) != MAJOR_NR) {
		printk(KERN_WARNING "lo_open: pseudo-major != %d\n", MAJOR_NR);
		return -ENODEV;
	}
	dev = MINOR(inode->i_rdev);
	if (dev >= max_loop) {
		return -ENODEV;
	}
	lo = &loop_dev[dev];

	type = lo->lo_encrypt_type; 
	if (type && xfer_funcs[type] && xfer_funcs[type]->lock)
		xfer_funcs[type]->lock(lo);
	lo->lo_refcnt++;
	MOD_INC_USE_COUNT;
	return 0;
}

static int lo_release(struct inode *inode, struct file *file)
{
	struct loop_device *lo;
	int	dev;

	if (!inode)
		return 0;
	if (MAJOR(inode->i_rdev) != MAJOR_NR) {
		printk(KERN_WARNING "lo_release: pseudo-major != %d\n",
		       MAJOR_NR);
		return 0;
	}
	dev = MINOR(inode->i_rdev);
	if (dev >= max_loop)
		return 0;
	lo = &loop_dev[dev];
	if (lo->lo_refcnt <= 0)
		printk(KERN_ERR "lo_release: refcount(%d) <= 0\n",
		       lo->lo_refcnt);
	else  {
		int type  = lo->lo_encrypt_type;
		--lo->lo_refcnt;
		if (xfer_funcs[type] && xfer_funcs[type]->unlock)
			xfer_funcs[type]->unlock(lo);
		MOD_DEC_USE_COUNT;
	}
	return 0;
}

static struct block_device_operations lo_fops = {
	open:		lo_open,
	release:	lo_release,
	ioctl:		lo_ioctl,
};

/*
 * And now the modules code and kernel interface.
 */
#ifdef MODULE
#define loop_init init_module
MODULE_PARM(max_loop, "i");
MODULE_PARM_DESC(max_loop, "Maximum number of loop devices (1-255)");
#endif

int loop_register_transfer(struct loop_func_table *funcs)
{
	if ((unsigned)funcs->number > MAX_LO_CRYPT || xfer_funcs[funcs->number])
		return -EINVAL;
	xfer_funcs[funcs->number] = funcs;
	return 0; 
}

int loop_unregister_transfer(int number)
{
	struct loop_device *lo; 

	if ((unsigned)number >= MAX_LO_CRYPT)
		return -EINVAL; 
	for (lo = &loop_dev[0]; lo < &loop_dev[max_loop]; lo++) { 
		int type = lo->lo_encrypt_type;
		if (type == number) { 
			xfer_funcs[type]->release(lo);
			lo->transfer = NULL; 
			lo->lo_encrypt_type = 0; 
		}
	}
	xfer_funcs[number] = NULL; 
	return 0; 
}

EXPORT_SYMBOL(loop_register_transfer);
EXPORT_SYMBOL(loop_unregister_transfer);

static void no_plug_device(request_queue_t *q, kdev_t device)
{
}

int __init loop_init(void) 
{
	int	i;

	if (devfs_register_blkdev(MAJOR_NR, "loop", &lo_fops)) {
		printk(KERN_WARNING "Unable to get major number %d for loop device\n",
		       MAJOR_NR);
		return -EIO;
	}
	devfs_handle = devfs_mk_dir (NULL, "loop", NULL);
	devfs_register_series (devfs_handle, "%u", max_loop, DEVFS_FL_DEFAULT,
			       MAJOR_NR, 0,
			       S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP,
			       &lo_fops, NULL);

	if ((max_loop < 1) || (max_loop > 255)) {
		printk (KERN_WARNING "loop: invalid max_loop (must be between 1 and 255), using default (8)\n");
		max_loop = 8;
	}

	printk(KERN_INFO "loop: enabling %d loop devices\n", max_loop);

	loop_dev = kmalloc (max_loop * sizeof(struct loop_device), GFP_KERNEL);
	if (!loop_dev) {
		printk (KERN_ERR "loop: Unable to create loop_dev\n");
		return -ENOMEM;
	}

	loop_sizes = kmalloc(max_loop * sizeof(int), GFP_KERNEL);
	if (!loop_sizes) {
		printk (KERN_ERR "loop: Unable to create loop_sizes\n");
		kfree (loop_dev);
		return -ENOMEM;
	}

	loop_blksizes = kmalloc (max_loop * sizeof(int), GFP_KERNEL);
	if (!loop_blksizes) {
		printk (KERN_ERR "loop: Unable to create loop_blksizes\n");
		kfree (loop_dev);
		kfree (loop_sizes);
		return -ENOMEM;
	}		

	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), DEVICE_REQUEST);
	blk_queue_pluggable(BLK_DEFAULT_QUEUE(MAJOR_NR), no_plug_device);
	blk_queue_headactive(BLK_DEFAULT_QUEUE(MAJOR_NR), 0);
	for (i=0; i < max_loop; i++) {
		memset(&loop_dev[i], 0, sizeof(struct loop_device));
		loop_dev[i].lo_number = i;
	}
	memset(loop_sizes, 0, max_loop * sizeof(int));
	memset(loop_blksizes, 0, max_loop * sizeof(int));
	blk_size[MAJOR_NR] = loop_sizes;
	blksize_size[MAJOR_NR] = loop_blksizes;
	for (i=0; i < max_loop; i++)
		register_disk(NULL, MKDEV(MAJOR_NR,i), 1, &lo_fops, 0);

	return 0;
}

#ifdef MODULE
void cleanup_module(void) 
{
	devfs_unregister (devfs_handle);
	if (devfs_unregister_blkdev(MAJOR_NR, "loop") != 0)
		printk(KERN_WARNING "loop: cannot unregister blkdev\n");

	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
	kfree (loop_dev);
	kfree (loop_sizes);
	kfree (loop_blksizes);
}
#endif

#ifndef MODULE
static int __init max_loop_setup(char *str)
{
	max_loop = simple_strtol(str,NULL,0);
	return 1;
}

__setup("max_loop=", max_loop_setup);
#endif

/*
 *  drivers/s390/char/tape_char.c
 *    character device frontend for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Michael Holzheu <holzheu@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/mtio.h>

#include <asm/uaccess.h>

#include "tape.h"

#define PRINTK_HEADER "TCHAR:"

#define TAPECHAR_MAJOR		0	/* get dynamic major */

/*
 * file operation structure for tape character frontend
 */
static ssize_t tapechar_read(struct file *, char *, size_t, loff_t *);
static ssize_t tapechar_write(struct file *, const char *, size_t, loff_t *);
static int tapechar_open(struct inode *,struct file *);
static int tapechar_release(struct inode *,struct file *);
static int tapechar_ioctl(struct inode *, struct file *, unsigned int,
			  unsigned long);

static struct file_operations tape_fops =
{
	.owner = THIS_MODULE,
	.read = tapechar_read,
	.write = tapechar_write,
	.ioctl = tapechar_ioctl,
	.open = tapechar_open,
	.release = tapechar_release,
};

static int tapechar_major = TAPECHAR_MAJOR;

/*
 * This function is called for every new tapedevice
 */
int
tapechar_setup_device(struct tape_device * device)
{
	return 0;
}

void
tapechar_cleanup_device(struct tape_device *device)
{
}

/*
 * Terminate write command (we write two TMs and skip backward over last)
 * This ensures that the tape is always correctly terminated.
 * When the user writes afterwards a new file, he will overwrite the
 * second TM and therefore one TM will remain to separate the
 * two files on the tape...
 */
static inline void
tapechar_terminate_write(struct tape_device *device)
{
	if (tape_mtop(device, MTWEOF, 1) == 0 &&
	    tape_mtop(device, MTWEOF, 1) == 0)
		tape_mtop(device, MTBSR, 1);
}

static inline int
tapechar_check_idalbuffer(struct tape_device *device, size_t block_size)
{
	struct idal_buffer *new;

	if (device->char_data.idal_buf != NULL &&
	    device->char_data.idal_buf->size >= block_size)
		return 0;
	/* The current idal buffer is not big enough. Allocate a new one. */
	new = idal_buffer_alloc(block_size, 0);
	if (new == NULL)
		return -ENOMEM;
	if (device->char_data.idal_buf != NULL)
		idal_buffer_free(device->char_data.idal_buf);
	device->char_data.idal_buf = new;
	return 0;
}

/*
 * Tape device read function
 */
ssize_t
tapechar_read (struct file *filp, char *data, size_t count, loff_t *ppos)
{
	struct tape_device *device;
	struct tape_request *request;
	size_t block_size;
	int rc;

	DBF_EVENT(6, "TCHAR:read\n");
	device = (struct tape_device *) filp->private_data;
	/* Check position. */
	if (ppos != &filp->f_pos) {
		/*
		 * "A request was outside the capabilities of the device."
		 * This check uses internal knowledge about how pread and
		 * read work...
		 */
		DBF_EVENT(6, "TCHAR:ppos wrong\n");
		return -EOVERFLOW;
	}
	/* Find out block size to use */
	if (device->char_data.block_size != 0) {
		if (count < device->char_data.block_size) {
			DBF_EVENT(3, "TCHAR:read smaller than block "
				  "size was requested\n");
			return -EINVAL;
		}
		block_size = device->char_data.block_size;
	} else {
		block_size = count;
		rc = tapechar_check_idalbuffer(device, block_size);
		if (rc)
			return rc;
	}
	DBF_EVENT(6, "TCHAR:nbytes: %lx\n", block_size);
	/* Let the discipline build the ccw chain. */
	request = device->discipline->read_block(device, block_size);
	if (IS_ERR(request))
		return PTR_ERR(request);
	/* Execute it. */
	rc = tape_do_io(device, request);
	if (rc == 0) {
		rc = block_size - request->rescnt;
		DBF_EVENT(6, "TCHAR:rbytes:  %x\n", rc);
		filp->f_pos += rc;
		/* Copy data from idal buffer to user space. */
		if (idal_buffer_to_user(device->char_data.idal_buf,
					data, rc) != 0)
			rc = -EFAULT;
	}
	tape_free_request(request);
	return rc;
}

/*
 * Tape device write function
 */
ssize_t
tapechar_write(struct file *filp, const char *data, size_t count, loff_t *ppos)
{
	struct tape_device *device;
	struct tape_request *request;
	size_t block_size;
	size_t written;
	int nblocks;
	int i, rc;

	DBF_EVENT(6, "TCHAR:write\n");
	device = (struct tape_device *) filp->private_data;
	/* Check position */
	if (ppos != &filp->f_pos) {
		/* "A request was outside the capabilities of the device." */
		DBF_EVENT(6, "TCHAR:ppos wrong\n");
		return -EOVERFLOW;
	}
	/* Find out block size and number of blocks */
	if (device->char_data.block_size != 0) {
		if (count < device->char_data.block_size) {
			DBF_EVENT(3, "TCHAR:write smaller than block "
				  "size was requested\n");
			return -EINVAL;
		}
		block_size = device->char_data.block_size;
		nblocks = count / block_size;
	} else {
		block_size = count;
		rc = tapechar_check_idalbuffer(device, block_size);
		if (rc)
			return rc;
		nblocks = 1;
	}
	DBF_EVENT(6,"TCHAR:nbytes: %lx\n", block_size);
	DBF_EVENT(6, "TCHAR:nblocks: %x\n", nblocks);
	/* Let the discipline build the ccw chain. */
	request = device->discipline->write_block(device, block_size);
	if (IS_ERR(request))
		return PTR_ERR(request);
	rc = 0;
	written = 0;
	for (i = 0; i < nblocks; i++) {
		/* Copy data from user space to idal buffer. */
		if (idal_buffer_from_user(device->char_data.idal_buf,
					  data, block_size)) {
			rc = -EFAULT;
			break;
		}
		rc = tape_do_io(device, request);
		if (rc)
			break;
		DBF_EVENT(6, "TCHAR:wbytes: %lx\n",
			  block_size - request->rescnt);
		filp->f_pos += block_size - request->rescnt;
		written += block_size - request->rescnt;
		if (request->rescnt != 0)
			break;
		data += block_size;
	}
	tape_free_request(request);
	if (rc == -ENOSPC) {
		/*
		 * Ok, the device has no more space. It has NOT written
		 * the block.
		 */
		if (device->discipline->process_eov)
			device->discipline->process_eov(device);
		if (written > 0)
			rc = 0;

	}
	return rc ? rc : written;
}

/*
 * Character frontend tape device open function.
 */
int
tapechar_open (struct inode *inode, struct file *filp)
{
	struct tape_device *device;
	int minor, rc;

	if (imajor(filp->f_dentry->d_inode) != tapechar_major)
		return -ENODEV;
	minor = iminor(filp->f_dentry->d_inode);
	device = tape_get_device(minor / TAPE_MINORS_PER_DEV);
	if (IS_ERR(device)) {
		return PTR_ERR(device);
	}
	DBF_EVENT(6, "TCHAR:open: %x\n", iminor(inode));
	rc = tape_open(device);
	if (rc == 0) {
		rc = tape_assign(device);
		if (rc == 0) {
			filp->private_data = device;
			return 0;
		}
		tape_release(device);
	}
	tape_put_device(device);
	return rc;
}

/*
 * Character frontend tape device release function.
 */

int
tapechar_release(struct inode *inode, struct file *filp)
{
	struct tape_device *device;

	device = (struct tape_device *) filp->private_data;
	DBF_EVENT(6, "TCHAR:release: %x\n", iminor(inode));
#if 0
	// FIXME: this is broken. Either MTWEOF/MTWEOF/MTBSR is done
	// EVERYTIME the user switches from write to something different
	// or it is not done at all. The second is IMHO better because
	// we should NEVER do something the user didn't request.
	if (device->last_op == TO_WRI)
		tapechar_terminate_write(device);
#endif
	/*
	 * If this is the rewinding tape minor then rewind.
	 */
	if ((iminor(inode) & 1) != 0)
		tape_mtop(device, MTREW, 1);
	if (device->char_data.idal_buf != NULL) {
		idal_buffer_free(device->char_data.idal_buf);
		device->char_data.idal_buf = NULL;
	}
	device->char_data.block_size = 0;
	tape_release(device);
	tape_unassign(device);
	tape_put_device(device);
	return 0;
}

/*
 * Tape device io controls.
 */
static int
tapechar_ioctl(struct inode *inp, struct file *filp,
	       unsigned int no, unsigned long data)
{
	struct tape_device *device;
	int rc;

	DBF_EVENT(6, "TCHAR:ioct\n");

	device = (struct tape_device *) filp->private_data;

	if (no == MTIOCTOP) {
		struct mtop op;

		if (copy_from_user(&op, (char *) data, sizeof(op)) != 0)
			return -EFAULT;
		if (op.mt_count < 0)
			return -EINVAL;
		return tape_mtop(device, op.mt_op, op.mt_count);
	}
	if (no == MTIOCPOS) {
		/* MTIOCPOS: query the tape position. */
		struct mtpos pos;

		rc = tape_mtop(device, MTTELL, 1);
		if (rc < 0)
			return rc;
		pos.mt_blkno = rc;
		if (copy_to_user((char *) data, &pos, sizeof(pos)) != 0)
			return -EFAULT;
		return 0;
	}
	if (no == MTIOCGET) {
		/* MTIOCGET: query the tape drive status. */
		struct mtget get;

		memset(&get, 0, sizeof(get));
		rc = tape_mtop(device, MTTELL, 1);
		if (rc < 0)
			return rc;
		get.mt_type = MT_ISUNKNOWN;
		get.mt_dsreg = device->tape_state;
		/* FIXME: mt_gstat, mt_erreg, mt_fileno */
		get.mt_resid = 0 /* device->devstat.rescnt */;
		get.mt_gstat = 0;
		get.mt_erreg = 0;
		get.mt_fileno = 0;
		get.mt_blkno = rc;
		if (copy_to_user((char *) data, &get, sizeof(get)) != 0)
			return -EFAULT;
		return 0;
	}
	/* Try the discipline ioctl function. */
	if (device->discipline->ioctl_fn == NULL)
		return -EINVAL;
	return device->discipline->ioctl_fn(device, no, data);
}

/*
 * Initialize character device frontend.
 */
int
tapechar_init (void)
{
	int rc;

	/* Register the tape major number to the kernel */
	rc = register_chrdev(tapechar_major, "tape", &tape_fops);
	if (rc < 0) {
		PRINT_ERR("can't get major %d\n", tapechar_major);
		DBF_EVENT(3, "TCHAR:initfail\n");
		return rc;
	}
	if (tapechar_major == 0)
		tapechar_major = rc;  /* accept dynamic major number */
	PRINT_ERR("Tape gets major %d for char device\n", tapechar_major);
	DBF_EVENT(3, "Tape gets major %d for char device\n", rc);
	DBF_EVENT(3, "TCHAR:init ok\n");
	return 0;
}

/*
 * cleanup
 */
void
tapechar_exit(void)
{
	unregister_chrdev (tapechar_major, "tape");
}

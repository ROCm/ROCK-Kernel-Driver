/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * /proc/ppc64/rtas/firmware_flash interface
 *
 * This file implements a firmware_flash interface to pump a firmware
 * image into the kernel.  At reboot time rtas_restart() will see the
 * firmware image and flash it as it reboots (see rtas.c).
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/rtas.h>

#define MODULE_VERSION "1.0"
#define MODULE_NAME "rtas_flash"

#define FIRMWARE_FLASH_NAME "firmware_flash"

/* Local copy of the flash block list.
 * We only allow one open of the flash proc file and create this
 * list as we go.  This list will be put in the kernel's
 * rtas_firmware_flash_list global var once it is fully read.
 *
 * For convenience as we build the list we use virtual addrs,
 * we do not fill in the version number, and the length field
 * is treated as the number of entries currently in the block
 * (i.e. not a byte count).  This is all fixed on release.
 */
static struct flash_block_list *flist;
static char *flash_msg;
static int flash_possible;

static int rtas_flash_open(struct inode *inode, struct file *file)
{
	if ((file->f_mode & FMODE_WRITE) && flash_possible) {
		if (flist)
			return -EBUSY;
		flist = (struct flash_block_list *)get_zeroed_page(GFP_KERNEL);
		if (!flist)
			return -ENOMEM;
	}
	return 0;
}

/* Do simple sanity checks on the flash image. */
static int flash_list_valid(struct flash_block_list *flist)
{
	struct flash_block_list *f;
	int i;
	unsigned long block_size, image_size;

	flash_msg = NULL;
	/* Paranoid self test here.  We also collect the image size. */
	image_size = 0;
	for (f = flist; f; f = f->next) {
		for (i = 0; i < f->num_blocks; i++) {
			if (f->blocks[i].data == NULL) {
				flash_msg = "error: internal error null data\n";
				return 0;
			}
			block_size = f->blocks[i].length;
			if (block_size <= 0 || block_size > PAGE_SIZE) {
				flash_msg = "error: internal error bad length\n";
				return 0;
			}
			image_size += block_size;
		}
	}
	if (image_size < (256 << 10)) {
		if (image_size < 2)
			flash_msg = NULL;	/* allow "clear" of image */
		else
			flash_msg = "error: flash image short\n";
		return 0;
	}
	printk(KERN_INFO "FLASH: flash image with %ld bytes stored for hardware flash on reboot\n", image_size);
	return 1;
}

static void free_flash_list(struct flash_block_list *f)
{
	struct flash_block_list *next;
	int i;

	while (f) {
		for (i = 0; i < f->num_blocks; i++)
			free_page((unsigned long)(f->blocks[i].data));
		next = f->next;
		free_page((unsigned long)f);
		f = next;
	}
}

static int rtas_flash_release(struct inode *inode, struct file *file)
{
	if (flist) {
		/* Always clear saved list on a new attempt. */
		if (rtas_firmware_flash_list.next) {
			free_flash_list(rtas_firmware_flash_list.next);
			rtas_firmware_flash_list.next = NULL;
		}

		if (flash_list_valid(flist))
			rtas_firmware_flash_list.next = flist;
		else
			free_flash_list(flist);
		flist = NULL;
	}
	return 0;
}

/* Reading the proc file will show status (not the firmware contents) */
static ssize_t rtas_flash_read(struct file *file, char *buf,
			       size_t count, loff_t *ppos)
{
	int error;
	char *msg;
	int msglen;

	if (!flash_possible) {
		msg = "error: this partition does not have service authority\n";
	} else if (flist) {
		msg = "info: this file is busy for write by some process\n";
	} else if (flash_msg) {
		msg = flash_msg;	/* message from last flash attempt */
	} else if (rtas_firmware_flash_list.next) {
		msg = "ready: firmware image ready for flash on reboot\n";
	} else {
		msg = "info: no firmware image for flash\n";
	}
	msglen = strlen(msg);
	if (msglen > count)
		msglen = count;

	if (ppos && *ppos != 0)
		return 0;	/* be cheap */

	error = verify_area(VERIFY_WRITE, buf, msglen);
	if (error)
		return -EINVAL;

	copy_to_user(buf, msg, msglen);

	if (ppos)
		*ppos = msglen;
	return msglen;
}

/* We could be much more efficient here.  But to keep this function
 * simple we allocate a page to the block list no matter how small the
 * count is.  If the system is low on memory it will be just as well
 * that we fail....
 */
static ssize_t rtas_flash_write(struct file *file, const char *buffer,
				size_t count, loff_t *off)
{
	size_t len = count;
	char *p;
	int next_free;
	struct flash_block_list *fl = flist;

	if (!flash_possible || len == 0)
		return len;	/* discard data */

	while (fl->next)
		fl = fl->next; /* seek to last block_list for append */
	next_free = fl->num_blocks;
	if (next_free == FLASH_BLOCKS_PER_NODE) {
		/* Need to allocate another block_list */
		fl->next = (struct flash_block_list *)get_zeroed_page(GFP_KERNEL);
		if (!fl->next)
			return -ENOMEM;
		fl = fl->next;
		next_free = 0;
	}

	if (len > PAGE_SIZE)
		len = PAGE_SIZE;
	p = (char *)get_zeroed_page(GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	if(copy_from_user(p, buffer, len)) {
		free_page((unsigned long)p);
		return -EFAULT;
	}
	fl->blocks[next_free].data = p;
	fl->blocks[next_free].length = len;
	fl->num_blocks++;

	return len;
}

static struct file_operations rtas_flash_operations = {
	.read		= rtas_flash_read,
	.write		= rtas_flash_write,
	.open		= rtas_flash_open,
	.release	= rtas_flash_release,
};


int __init rtas_flash_init(void)
{
	struct proc_dir_entry *ent = NULL;

	if (!proc_ppc64.rtas) {
		printk(KERN_WARNING "rtas proc dir does not already exist");
		return -ENOENT;
	}

	if (rtas_token("ibm,update-flash-64-and-reboot") != RTAS_UNKNOWN_SERVICE)
		flash_possible = 1;

	if ((ent = create_proc_entry(FIRMWARE_FLASH_NAME, S_IRUSR | S_IWUSR, proc_ppc64.rtas)) != NULL) {
		ent->nlink = 1;
		ent->proc_fops = &rtas_flash_operations;
		ent->owner = THIS_MODULE;
	}
	return 0;
}

void __exit rtas_flash_cleanup(void)
{
	if (!proc_ppc64.rtas)
		return;
	remove_proc_entry(FIRMWARE_FLASH_NAME, proc_ppc64.rtas);
}

module_init(rtas_flash_init);
module_exit(rtas_flash_cleanup);
MODULE_LICENSE("GPL");

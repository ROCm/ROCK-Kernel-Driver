/* $Id: divamnt.c,v 1.32 2004/01/15 09:48:13 armin Exp $
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * Maint module
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/devfs_fs_kernel.h>

#include "platform.h"
#include "di_defs.h"
#include "divasync.h"
#include "debug_if.h"

static char *main_revision = "$Revision: 1.32 $";

static int major;

MODULE_DESCRIPTION("Maint driver for Eicon DIVA Server cards");
MODULE_AUTHOR("Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE("DIVA card driver");
MODULE_LICENSE("GPL");

int buffer_length = 128;
MODULE_PARM(buffer_length, "i");
unsigned long diva_dbg_mem = 0;
MODULE_PARM(diva_dbg_mem, "l");

static char *DRIVERNAME =
    "Eicon DIVA - MAINT module (http://www.melware.net)";
static char *DRIVERLNAME = "diva_mnt";
static char *DEVNAME = "DivasMAINT";
char *DRIVERRELEASE_MNT = "2.0";

static wait_queue_head_t msgwaitq;
static DECLARE_MUTEX(opened_sem);
static int opened;
static struct timeval start_time;

extern int mntfunc_init(int *, void **, unsigned long);
extern void mntfunc_finit(void);
extern int maint_read_write(void __user *buf, int count);

/*
 *  helper functions
 */
static char *getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "1.0";

	return rev;
}

/*
 * buffer alloc
 */
void *diva_os_malloc_tbuffer(unsigned long flags, unsigned long size)
{
	return (kmalloc(size, GFP_KERNEL));
}
void diva_os_free_tbuffer(unsigned long flags, void *ptr)
{
	if (ptr) {
		kfree(ptr);
	}
}

/*
 * kernel/user space copy functions
 */
int diva_os_copy_to_user(void *os_handle, void __user *dst, const void *src,
			 int length)
{
	return (copy_to_user(dst, src, length));
}
int diva_os_copy_from_user(void *os_handle, void *dst, const void __user *src,
			   int length)
{
	return (copy_from_user(dst, src, length));
}

/*
 * get time
 */
void diva_os_get_time(dword * sec, dword * usec)
{
	struct timeval tv;

	do_gettimeofday(&tv);

	if (tv.tv_sec > start_time.tv_sec) {
		if (start_time.tv_usec > tv.tv_usec) {
			tv.tv_sec--;
			tv.tv_usec += 1000000;
		}
		*sec = (dword) (tv.tv_sec - start_time.tv_sec);
		*usec = (dword) (tv.tv_usec - start_time.tv_usec);
	} else if (tv.tv_sec == start_time.tv_sec) {
		*sec = 0;
		if (start_time.tv_usec < tv.tv_usec) {
			*usec = (dword) (tv.tv_usec - start_time.tv_usec);
		} else {
			*usec = 0;
		}
	} else {
		*sec = (dword) tv.tv_sec;
		*usec = (dword) tv.tv_usec;
	}
}

/*
 * /proc entries
 */

extern struct proc_dir_entry *proc_net_eicon;
static struct proc_dir_entry *maint_proc_entry = NULL;

/*
	Read function is provided for compatibility reason - this allows
  to read unstructured traces, formated as ascii string only
  */
static ssize_t
maint_read(struct file *file, char __user *buf, size_t count, loff_t * off)
{
	diva_dbg_entry_head_t *pmsg = NULL;
	diva_os_spin_lock_magic_t old_irql;
	word size;
	char *pstr, *dli_label = "UNK";
	int str_length;
	int *str_msg;

	if (!file->private_data) {
		for (;;) {
			while (
			       (pmsg =
				diva_maint_get_message(&size,
						       &old_irql))) {
				if (!(pmsg->facility == MSG_TYPE_STRING)) {
					diva_maint_ack_message(1,
							       &old_irql);
				} else {
					break;
				}
			}

			if (!pmsg) {
				if (file->f_flags & O_NONBLOCK) {
					return (-EAGAIN);
				}
				interruptible_sleep_on(&msgwaitq);
				if (signal_pending(current)) {
					return (-ERESTARTSYS);
				}
			} else {
				break;
			}
		}
		/*
		   The length of message that shoule be read is:
		   pmsg->data_length + label(25) + DrvID(2) + byte CR + trailing zero
		 */
		if (!
		    (str_msg =
		     (int *) diva_os_malloc_tbuffer(0,
						    pmsg->data_length +
						    29 + 2 * sizeof(int)))) {
			diva_maint_ack_message(0, &old_irql);
			return (-ENOMEM);
		}
		pstr = (char *) &str_msg[2];

		switch (pmsg->dli) {
		case DLI_LOG:
			dli_label = "LOG";
			break;
		case DLI_FTL:
			dli_label = "FTL";
			break;
		case DLI_ERR:
			dli_label = "ERR";
			break;
		case DLI_TRC:
			dli_label = "TRC";
			break;
		case DLI_REG:
			dli_label = "REG";
			break;
		case DLI_MEM:
			dli_label = "MEM";
			break;
		case DLI_SPL:
			dli_label = "SPL";
			break;
		case DLI_IRP:
			dli_label = "IRP";
			break;
		case DLI_TIM:
			dli_label = "TIM";
			break;
		case DLI_TAPI:
			dli_label = "TAPI";
			break;
		case DLI_NDIS:
			dli_label = "NDIS";
			break;
		case DLI_CONN:
			dli_label = "CONN";
			break;
		case DLI_STAT:
			dli_label = "STAT";
			break;
		case DLI_PRV0:
			dli_label = "PRV0";
			break;
		case DLI_PRV1:
			dli_label = "PRV1";
			break;
		case DLI_PRV2:
			dli_label = "PRV2";
			break;
		case DLI_PRV3:
			dli_label = "PRV3";
			break;
		}
		str_length = sprintf(pstr, "%s %02x %s\n",
				     dli_label, (byte) pmsg->drv_id,
				     (char *) &pmsg[1]);
		str_msg[0] = str_length;
		str_msg[1] = 0;
		file->private_data = str_msg;
		diva_maint_ack_message(1, &old_irql);
	} else {
		str_msg = (int *) file->private_data;
		pstr = (char *) &str_msg[2];
		pstr += str_msg[1];	/* head + offset */
		str_length = str_msg[0] - str_msg[1];	/* length - offset */
	}
	str_length = MIN(str_length, count);

	if (diva_os_copy_to_user(NULL, buf, pstr, str_length)) {
		diva_os_free_tbuffer(0, str_msg);
		file->private_data = NULL;
		return (-EFAULT);
	}
	str_msg[1] += str_length;
	if ((str_msg[0] - str_msg[1]) <= 0) {
		diva_os_free_tbuffer(0, str_msg);
		file->private_data = NULL;
	}

	return (str_length);
}

static ssize_t
maint_write(struct file *file, const char __user *buf, size_t count, loff_t * off)
{
	return (-ENODEV);
}

static unsigned int maint_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;

	poll_wait(file, &msgwaitq, wait);
	mask = POLLOUT | POLLWRNORM;
	if (file->private_data || diva_dbg_q_length()) {
		mask |= POLLIN | POLLRDNORM;
	}
	return (mask);
}

static int maint_open(struct inode *ino, struct file *filep)
{
	down(&opened_sem);
	if (opened) {
		up(&opened_sem);
		return (-EBUSY);
	}
	opened++;
	up(&opened_sem);

	filep->private_data = NULL;

	return nonseekable_open(ino, filep);
}

static int maint_close(struct inode *ino, struct file *filep)
{
	if (filep->private_data) {
		diva_os_free_tbuffer(0, filep->private_data);
		filep->private_data = NULL;
	}

	down(&opened_sem);
	opened--;
	up(&opened_sem);
	return (0);
}

/*
 * fops
 */
static struct file_operations maint_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = maint_read,
	.write   = maint_write,
	.poll    = maint_poll,
	.open    = maint_open,
	.release = maint_close
};

static int DIVA_INIT_FUNCTION create_maint_proc(void)
{
	maint_proc_entry =
	    create_proc_entry("maint", S_IFREG | S_IRUGO | S_IWUSR,
			      proc_net_eicon);
	if (!maint_proc_entry)
		return (0);

	maint_proc_entry->proc_fops = &maint_fops;
	maint_proc_entry->owner = THIS_MODULE;

	return (1);
}

static void remove_maint_proc(void)
{
	if (maint_proc_entry) {
		remove_proc_entry("maint", proc_net_eicon);
		maint_proc_entry = NULL;
	}
}

/*
 * device node operations
 */
static ssize_t divas_maint_write(struct file *file, const char __user *buf,
				 size_t count, loff_t * ppos)
{
	return (maint_read_write((char __user *) buf, (int) count));
}

static ssize_t divas_maint_read(struct file *file, char __user *buf,
				size_t count, loff_t * ppos)
{
	return (maint_read_write(buf, (int) count));
}

static struct file_operations divas_maint_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = divas_maint_read,
	.write   = divas_maint_write,
	.poll    = maint_poll,
	.open    = maint_open,
	.release = maint_close
};

static void divas_maint_unregister_chrdev(void)
{
	devfs_remove(DEVNAME);
	unregister_chrdev(major, DEVNAME);
}

static int DIVA_INIT_FUNCTION divas_maint_register_chrdev(void)
{
	if ((major = register_chrdev(0, DEVNAME, &divas_maint_fops)) < 0)
	{
		printk(KERN_ERR "%s: failed to create /dev entry.\n",
		       DRIVERLNAME);
		return (0);
	}
	devfs_mk_cdev(MKDEV(major, 0), S_IFCHR|S_IRUSR|S_IWUSR, DEVNAME);

	return (1);
}

/*
 * wake up reader
 */
void diva_maint_wakeup_read(void)
{
	wake_up_interruptible(&msgwaitq);
}

/*
 *  Driver Load
 */
static int DIVA_INIT_FUNCTION maint_init(void)
{
	char tmprev[50];
	int ret = 0;
	void *buffer = NULL;

	do_gettimeofday(&start_time);
	init_waitqueue_head(&msgwaitq);

	printk(KERN_INFO "%s\n", DRIVERNAME);
	printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE_MNT);
	strcpy(tmprev, main_revision);
	printk("%s  Build: %s \n", getrev(tmprev), DIVA_BUILD);

	if (!divas_maint_register_chrdev()) {
		ret = -EIO;
		goto out;
	}
	if (!create_maint_proc()) {
		printk(KERN_ERR "%s: failed to create proc entry.\n",
		       DRIVERLNAME);
		divas_maint_unregister_chrdev();
		ret = -EIO;
		goto out;
	}

	if (!(mntfunc_init(&buffer_length, &buffer, diva_dbg_mem))) {
		printk(KERN_ERR "%s: failed to connect to DIDD.\n",
		       DRIVERLNAME);
		remove_maint_proc();
		divas_maint_unregister_chrdev();
		ret = -EIO;
		goto out;
	}

	printk(KERN_INFO "%s: trace buffer = %p - %d kBytes, %s (Major: %d)\n",
	       DRIVERLNAME, buffer, (buffer_length / 1024),
	       (diva_dbg_mem == 0) ? "internal" : "external", major);

      out:
	return (ret);
}

/*
**  Driver Unload
*/
static void DIVA_EXIT_FUNCTION maint_exit(void)
{
	remove_maint_proc();
	divas_maint_unregister_chrdev();
	mntfunc_finit();

	printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(maint_init);
module_exit(maint_exit);

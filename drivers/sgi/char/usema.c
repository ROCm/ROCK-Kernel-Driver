/*
 * usema.c: software semaphore driver (see IRIX's usema(7M))
 * written 1997 Mike Shaver (shaver@neon.ingenia.ca)
 *         1997 Miguel de Icaza (miguel@kernel.org)
 *
 * This file contains the implementation of /dev/usemaclone,
 * the devices used by IRIX's us* semaphore routines.
 *
 * /dev/usemaclone is used to create a new semaphore device, and then
 * the semaphore is manipulated via ioctls.
 *
 * At least for the zero-contention case, lock set and unset as well
 * as semaphore P and V are done in userland, which makes things a
 * little bit better.  I suspect that the ioctls are used to register
 * the process as blocking, etc.
 *
 * Much inspiration and structure stolen from Miguel's shmiq work.
 *
 * For more information:
 * usema(7m), usinit(3p), usnewsema(3p)
 * /usr/include/sys/usioctl.h 
 *
 */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/major.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/dcache.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include "usema.h"

#include <asm/usioctl.h>
#include <asm/mman.h>
#include <asm/uaccess.h>

struct irix_usema {
	struct file *filp;
	wait_queue_head_t proc_list;
};


static int
sgi_usema_attach (usattach_t * attach, struct irix_usema *usema)
{
	int newfd;
	newfd = get_unused_fd();
	if (newfd < 0)
		return newfd;
	
	get_file(usema->filp);
	fd_install(newfd, usema->filp);
	/* Is that it? */
	printk("UIOCATTACHSEMA: new usema fd is %d", newfd);
	return newfd;
}

static int
sgi_usemaclone_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct irix_usema *usema = file->private_data;
	int retval;
	
	printk("[%s:%d] wants ioctl 0x%xd (arg 0x%lx)",
	       current->comm, current->pid, cmd, arg);

	switch(cmd) {
	case UIOCATTACHSEMA: {
		/* They pass us information about the semaphore to
		   which they wish to be attached, and we create&return
		   a new fd corresponding to the appropriate semaphore.
		   */
		usattach_t *attach = (usattach_t *)arg;
		retval = verify_area(VERIFY_READ, attach, sizeof(usattach_t));
		if (retval) {
			printk("[%s:%d] sgi_usema_ioctl(UIOCATTACHSEMA): "
			       "verify_area failure",
			       current->comm, current->pid);
			return retval;
		}
		if (usema == 0)
			return -EINVAL;

		printk("UIOCATTACHSEMA: attaching usema %p to process %d\n",
		       usema, current->pid);
		/* XXX what is attach->us_handle for? */
		return sgi_usema_attach(attach, usema);
		break;
	}
	case UIOCABLOCK:	/* XXX make `async' */
	case UIOCNOIBLOCK:	/* XXX maybe? */
	case UIOCBLOCK: {
		/* Block this process on the semaphore */
		usattach_t *attach = (usattach_t *)arg;

		retval = verify_area(VERIFY_READ, attach, sizeof(usattach_t));
		if (retval) {
			printk("[%s:%d] sgi_usema_ioctl(UIOC*BLOCK): "
			       "verify_area failure",
			       current->comm, current->pid);
			return retval;
		}
		printk("UIOC*BLOCK: putting process %d to sleep on usema %p",
		       current->pid, usema);
		if (cmd == UIOCNOIBLOCK)
			interruptible_sleep_on(&usema->proc_list);
		else
			sleep_on(&usema->proc_list);
		return 0;
	}
	case UIOCAUNBLOCK:	/* XXX make `async' */
	case UIOCUNBLOCK: {
		/* Wake up all process waiting on this semaphore */
		usattach_t *attach = (usattach_t *)arg;

		retval = verify_area(VERIFY_READ, attach, sizeof(usattach_t));
		if (retval) {
			printk("[%s:%d] sgi_usema_ioctl(UIOC*BLOCK): "
			       "verify_area failure",
			       current->comm, current->pid);
			return retval;
		}

		printk("[%s:%d] releasing usema %p",
		       current->comm, current->pid, usema);
		wake_up(&usema->proc_list);
		return 0;
	}
	}
	return -ENOSYS;
}

static unsigned int
sgi_usemaclone_poll(struct file *filp, poll_table *wait)
{
	struct irix_usema *usema = filp->private_data;
	
	printk("[%s:%d] wants to poll usema %p",
	       current->comm, current->pid, usema);
	
	return 0;
}

static int
sgi_usemaclone_open(struct inode *inode, struct file *filp)
{
	struct irix_usema *usema;

	usema = kmalloc (sizeof (struct irix_usema), GFP_KERNEL);
	if (!usema)
		return -ENOMEM;
	
	usema->filp        = filp;
	init_waitqueue_head(&usema->proc_list);
	filp->private_data = usema;

	return 0;
}

struct file_operations sgi_usemaclone_fops = {
	poll:		sgi_usemaclone_poll,
	ioctl:		sgi_usemaclone_ioctl,
	open:		sgi_usemaclone_open,
};

static struct miscdevice dev_usemaclone = {
	SGI_USEMACLONE, "usemaclone", &sgi_usemaclone_fops
};

void
usema_init(void)
{
	printk("usemaclone misc device registered (minor: %d)\n",
	       SGI_USEMACLONE);
	misc_register(&dev_usemaclone);
}

EXPORT_SYMBOL(usema_init);

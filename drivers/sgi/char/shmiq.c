/* $Id: shmiq.c,v 1.19 2000/02/23 00:41:21 ralf Exp $
 *
 * shmiq.c: shared memory input queue driver
 * written 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * We implement /dev/shmiq, /dev/qcntlN here
 * this is different from IRIX that has shmiq as a misc
 * streams device and the and qcntl devices as a major device.
 *
 * minor number 0 implements /dev/shmiq,
 * any other number implements /dev/qcntl${minor-1}
 *
 * /dev/shmiq is used by the X server for two things:
 * 
 *    1. for I_LINK()ing trough ioctl the file handle of a
 *       STREAMS device.
 *
 *    2. To send STREAMS-commands to the devices with the
 *       QIO ioctl interface.
 *
 * I have not yet figured how to make multiple X servers share
 * /dev/shmiq for having different servers running.  So, for now
 * I keep a kernel-global array of inodes that are pushed into
 * /dev/shmiq.
 *
 * /dev/qcntlN is used by the X server for two things:
 *
 *    1. Issuing the QIOCATTACH for mapping the shared input
 *       queue into the address space of the X server (yeah, yeah,
 *       I did not invent this interface).
 *
 *    2. used by select.  I bet it is used for checking for events on
 *       the queue.
 *
 * Now the problem is that there does not seem anything that
 * establishes a connection between /dev/shmiq and the qcntlN file.  I
 * need an strace from an X server that runs on a machine with more
 * than one keyboard.  And this is a problem since the file handles
 * are pushed in /dev/shmiq, while the events should be dispatched to
 * the /dev/qcntlN device. 
 *
 * Until then, I just allow for 1 qcntl device.
 *
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/major.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>

#include <asm/shmiq.h>
#include <asm/gfx.h>
#include <asm/mman.h>
#include <asm/uaccess.h>
#include <asm/poll.h>
#include "graphics.h"

/* we are not really getting any more than a few files in the shmiq */
#define MAX_SHMIQ_DEVS 10

/*
 * One per X server running, not going to get very big.
 * Even if we have this we now assume just 1 /dev/qcntl can be
 * active, I need to find how this works on multi-headed machines.
 */
#define MAX_SHMI_QUEUES 4

static struct {
	int                 used;
	struct file         *filp;
	struct shmiqsetcpos cpos;
} shmiq_pushed_devices [MAX_SHMIQ_DEVS];

/* /dev/qcntlN attached memory regions, location and size of the event queue */
static struct {
	int    opened;		/* if this device has been opened */
	void   *shmiq_vaddr;	/* mapping in kernel-land */
	int    tail;		/* our copy of the shmiq->tail */
	int    events;
	int    mapped;
	
	wait_queue_head_t    proc_list;
	struct fasync_struct *fasync;
} shmiqs [MAX_SHMI_QUEUES];

void
shmiq_push_event (struct shmqevent *e)
{
	struct sharedMemoryInputQueue *s;
	int    device = 0;	/* FIXME: here is the assumption /dev/shmiq == /dev/qcntl0 */
	int    tail_next;

	if (!shmiqs [device].mapped)
		return;
	s = shmiqs [device].shmiq_vaddr;

	s->flags = 0;
	if (s->tail != shmiqs [device].tail){
		s->flags |= SHMIQ_CORRUPTED;
		return;
	}
	tail_next = (s->tail + 1) % (shmiqs [device].events);
	
	if (tail_next == s->head){
		s->flags |= SHMIQ_OVERFLOW;
		return;
	}
	
	e->un.time = jiffies;
	s->events [s->tail] = *e;
	printk ("KERNEL: dev=%d which=%d type=%d flags=%d\n",
		e->data.device, e->data.which, e->data.type, e->data.flags);
	s->tail = tail_next;
	shmiqs [device].tail = tail_next;
	kill_fasync (&shmiqs [device].fasync, SIGIO, POLL_IN);
	wake_up_interruptible (&shmiqs [device].proc_list);
}

static int
shmiq_manage_file (struct file *filp)
{
	int i;

	if (!filp->f_op || !filp->f_op->ioctl)
		return -ENOSR;

	for (i = 0; i < MAX_SHMIQ_DEVS; i++){
		if (shmiq_pushed_devices [i].used)
			continue;
		if ((*filp->f_op->ioctl)(filp->f_dentry->d_inode, filp, SHMIQ_ON, i) != 0)
			return -ENOSR;
		shmiq_pushed_devices [i].used = 1;
		shmiq_pushed_devices [i].filp = filp;
		shmiq_pushed_devices [i].cpos.x = 0;
		shmiq_pushed_devices [i].cpos.y = 0;
		return i;
	}
	return -ENOSR;
}

static int
shmiq_forget_file (unsigned long fdes)
{
	struct file *filp;

	if (fdes > MAX_SHMIQ_DEVS)
		return -EINVAL;
	
	if (!shmiq_pushed_devices [fdes].used)
		return -EINVAL;

	filp = shmiq_pushed_devices [fdes].filp;
	if (filp){
		(*filp->f_op->ioctl)(filp->f_dentry->d_inode, filp, SHMIQ_OFF, 0);
		shmiq_pushed_devices [fdes].filp = 0;
		fput (filp);
	}
	shmiq_pushed_devices [fdes].used = 0;

	return 0;
}

static int
shmiq_sioc (int device, int cmd, struct strioctl *s)
{
	switch (cmd){
	case QIOCGETINDX:
		/*
		 * Ok, we just return the index they are providing us
		 */
		printk ("QIOCGETINDX: returning %d\n", *(int *)s->ic_dp);
		return 0;

	case QIOCIISTR: {
		struct muxioctl *mux = (struct muxioctl *) s->ic_dp;
		
		printk ("Double indirect ioctl: [%d, %x\n", mux->index, mux->realcmd);
		return -EINVAL;
	}

	case QIOCSETCPOS: {
		if (copy_from_user (&shmiq_pushed_devices [device].cpos, s->ic_dp,
				    sizeof (struct shmiqsetcpos)))
			return -EFAULT;
		return 0;
	}
	}
	printk ("Unknown I_STR request for shmiq device: 0x%x\n", cmd);
	return -EINVAL;
}

static int
shmiq_ioctl (struct inode *inode, struct file *f, unsigned int cmd, unsigned long arg)
{
	struct file *file;
	struct strioctl sioc;
	int v;

	switch (cmd){
		/*
		 * They are giving us the file descriptor for one
		 * of their streams devices
		 */

	case I_LINK:
		file = fget (arg);
		if (!file)
			goto bad_file;

		v = shmiq_manage_file (file);
		if (v<0)
			fput(file);
		return v;

		/*
		 * Remove a device from our list of managed
		 * stream devices
		 */
	case I_UNLINK:
		v = shmiq_forget_file (arg);
		return v;
		
	case I_STR:
		v = get_sioc (&sioc, arg);
		if (v)
			return v;
		
		/* FIXME: This forces device = 0 */
		return shmiq_sioc (0, sioc.ic_cmd, &sioc);
	}

	return -EINVAL;

bad_file:
	return -EBADF;
}

extern long sys_munmap(unsigned long addr, size_t len);

static int
qcntl_ioctl (struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, int minor)
{
	struct shmiqreq req;
	struct vm_area_struct *vma;
	int v;
	
	switch (cmd) {
		/*
		 * The address space is already mapped as a /dev/zero
		 * mapping.  FIXME: check that /dev/zero is what the user
		 * had mapped before :-)
		 */
		case QIOCATTACH: {
			unsigned long vaddr;
			int s;
	
			v = verify_area (VERIFY_READ, (void *) arg,
			                 sizeof (struct shmiqreq));
			if (v)
				return v;
			if (copy_from_user(&req, (void *) arg, sizeof (req)))
				return -EFAULT;
			/*
			 * Do not allow to attach to another region if it has
			 * already been attached
			 */
			if (shmiqs [minor].mapped) {
				printk("SHMIQ:The thingie is already mapped\n");
				return -EINVAL;
			}

			vaddr = (unsigned long) req.user_vaddr;
			vma = find_vma (current->mm, vaddr);
			if (!vma) {
				printk ("SHMIQ: could not find %lx the vma\n",
				        vaddr);
				return -EINVAL;
			}
			s = req.arg * sizeof (struct shmqevent) +
			    sizeof (struct sharedMemoryInputQueue);
			v = sys_munmap (vaddr, s);
			down(&current->mm->mmap_sem);
			do_munmap(current->mm, vaddr, s);
			do_mmap(filp, vaddr, s, PROT_READ | PROT_WRITE,
			        MAP_PRIVATE|MAP_FIXED, 0);
			up(&current->mm->mmap_sem);
			shmiqs[minor].events = req.arg;
			shmiqs[minor].mapped = 1;

			return 0;
		}
	}

	return -EINVAL;
}

struct page *
shmiq_nopage (struct vm_area_struct *vma, unsigned long address,
              int write_access)
{
	/* Do not allow for mremap to expand us */
	return NULL;
}

static struct vm_operations_struct qcntl_mmap = {
	nopage:	shmiq_nopage,		/* our magic no-page fault handler */
};

static int
shmiq_qcntl_mmap (struct file *file, struct vm_area_struct *vma)
{
	int           minor = MINOR (file->f_dentry->d_inode->i_rdev), error;
	unsigned int  size;
	unsigned long mem, start;
	
	/* mmap is only supported on the qcntl devices */
	if (minor-- == 0)
		return -EINVAL;

	if (vma->vm_pgoff != 0)
		return -EINVAL;

	size  = vma->vm_end - vma->vm_start;
	start = vma->vm_start; 
	lock_kernel();
	mem = (unsigned long) shmiqs [minor].shmiq_vaddr =  vmalloc_uncached (size);
	if (!mem) {
		unlock_kernel();
		return -EINVAL;
	}

	/* Prevent the swapper from considering these pages for swap and touching them */
	vma->vm_flags    |= (VM_SHM  | VM_LOCKED | VM_IO);
	vma->vm_ops = &qcntl_mmap;
	
	/* Uncache the pages */
	vma->vm_page_prot = PAGE_USERIO;

	error = vmap_page_range (vma->vm_start, size, mem);

	shmiqs [minor].tail = 0;
	/* Init the shared memory input queue */
	memset (shmiqs [minor].shmiq_vaddr, 0, size);
	unlock_kernel();
	
	return error;
}

static int
shmiq_qcntl_ioctl (struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int minor = MINOR (inode->i_rdev);

	if (minor-- == 0)
		return shmiq_ioctl (inode, filp, cmd, arg);

	return qcntl_ioctl (inode, filp, cmd, arg, minor);
}

static unsigned int
shmiq_qcntl_poll (struct file *filp, poll_table *wait)
{
	struct sharedMemoryInputQueue *s;
	int minor = MINOR (filp->f_dentry->d_inode->i_rdev);

	if (minor-- == 0)
		return 0;

	if (!shmiqs [minor].mapped)
		return 0;
	
	poll_wait (filp, &shmiqs [minor].proc_list, wait);
	s = shmiqs [minor].shmiq_vaddr;
	if (s->head != s->tail)
		return POLLIN | POLLRDNORM;
	return 0;
}

static int
shmiq_qcntl_open (struct inode *inode, struct file *filp)
{
	int minor = MINOR (inode->i_rdev);

	if (minor == 0)
		return 0;

	minor--;
	if (minor > MAX_SHMI_QUEUES)
		return -EINVAL;
	if (shmiqs [minor].opened)
		return -EBUSY;

	lock_kernel ();
	shmiqs [minor].opened      = 1;
	shmiqs [minor].shmiq_vaddr = 0;
	unlock_kernel ();

	return 0;
}

static int
shmiq_qcntl_fasync (int fd, struct file *file, int on)
{
	int retval;
	int minor = MINOR (file->f_dentry->d_inode->i_rdev);

	retval = fasync_helper (fd, file, on, &shmiqs [minor].fasync);
	if (retval < 0)
		return retval;
	return 0;
}

static int
shmiq_qcntl_close (struct inode *inode, struct file *filp)
{
	int minor = MINOR (inode->i_rdev);
	int j;
	
	if (minor-- == 0){
		for (j = 0; j < MAX_SHMIQ_DEVS; j++)
			shmiq_forget_file (j);
	}

	if (minor > MAX_SHMI_QUEUES)
		return -EINVAL;
	if (shmiqs [minor].opened == 0)
		return -EINVAL;

	lock_kernel ();
	shmiq_qcntl_fasync (-1, filp, 0);
	shmiqs [minor].opened      = 0;
	shmiqs [minor].mapped      = 0;
	shmiqs [minor].events      = 0;
	shmiqs [minor].fasync      = 0;
	vfree (shmiqs [minor].shmiq_vaddr);
	shmiqs [minor].shmiq_vaddr = 0;
	unlock_kernel ();

	return 0;
}


static struct file_operations shmiq_fops =
{
	poll:		shmiq_qcntl_poll,
	ioctl:		shmiq_qcntl_ioctl,
	mmap:		shmiq_qcntl_mmap,
	open:		shmiq_qcntl_open,
	release:	shmiq_qcntl_close,
	fasync:		shmiq_qcntl_fasync,
};

void
shmiq_init (void)
{
	printk ("SHMIQ setup\n");
	devfs_register_chrdev(SHMIQ_MAJOR, "shmiq", &shmiq_fops);
	devfs_register (NULL, "shmiq", DEVFS_FL_DEFAULT,
			SHMIQ_MAJOR, 0, S_IFCHR | S_IRUSR | S_IWUSR,
			&shmiq_fops, NULL);
	devfs_register_series (NULL, "qcntl%u", 2, DEVFS_FL_DEFAULT,
			       SHMIQ_MAJOR, 1,
			       S_IFCHR | S_IRUSR | S_IWUSR,
			       &shmiq_fops, NULL);
}

/*
 *  IBM/3270 Driver -- Copyright (C) UTS Global LLC
 *
 *  tubfs.c -- Fullscreen driver
 *
 *
 *
 *
 *
 *  Author:  Richard Hitt
 */
#include "tubio.h"

int fs3270_major = -1;			/* init to impossible -1 */

static int fs3270_open(struct inode *, struct file *);
static int fs3270_close(struct inode *, struct file *);
static int fs3270_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static ssize_t fs3270_read(struct file *, char *, size_t, loff_t *);
static ssize_t fs3270_write(struct file *, const char *, size_t, loff_t *);
static int fs3270_wait(tub_t *, long *);
static void fs3270_int(tub_t *tubp, devstat_t *dsp);
extern void tty3270_refresh(tub_t *);

static struct file_operations fs3270_fops = {
	.owner = THIS_MODULE,		/* owner */
	.read 	= fs3270_read,	/* read */
	.write	= fs3270_write,	/* write */
	.ioctl	= fs3270_ioctl,	/* ioctl */
	.open 	= fs3270_open,	/* open */
	.release = fs3270_close,	/* release */
};

/*
 * fs3270_init() -- Initialize fullscreen tubes
 */
int
fs3270_init(void)
{
	int rc;

	rc = register_chrdev(IBM_FS3270_MAJOR, "fs3270", &fs3270_fops);
	if (rc) {
		printk(KERN_ERR "tubmod can't get major nbr %d: error %d\n",
			IBM_FS3270_MAJOR, rc);
		return -1;
	}
	devfs_mk_dir("3270");
	devfs_mk_cdev(MKDEV(IBM_FS3270_MAJOR, 0),
			S_IFCHR|S_IRUGO|S_IWUGO, "3270/tub");
	fs3270_major = IBM_FS3270_MAJOR;
	return 0;
}

/*
 * fs3270_fini() -- Uninitialize fullscreen tubes
 */
void
fs3270_fini(void)
{
	if (fs3270_major != -1) {
		devfs_remove("3270");
		devfs_remove("3270/tub");
		unregister_chrdev(fs3270_major, "fs3270");
		fs3270_major = -1;
	}
}

/*
 * fs3270_open
 */
static int
fs3270_open(struct inode *ip, struct file *fp)
{
	tub_t *tubp;
	long flags;

	/* See INODE2TUB(ip) for handling of "/dev/3270/tub" */
	if ((tubp = INODE2TUB(ip)) == NULL)
		return -ENOENT;

	TUBLOCK(tubp->irq, flags);
	if (tubp->mode == TBM_FS || tubp->mode == TBM_FSLN) {
		TUBUNLOCK(tubp->irq, flags);
		return -EBUSY;
	}

	fp->private_data = ip;
	tubp->mode = TBM_FS;
	tubp->intv = fs3270_int;
	tubp->dstat = 0;
	tubp->fs_pid = current->pid;
	tubp->fsopen = 1;
	TUBUNLOCK(tubp->irq, flags);
	return 0;
}

/*
 * fs3270_close aka release:  free the irq
 */
static int
fs3270_close(struct inode *ip, struct file *fp)
{
	tub_t *tubp;
	long flags;

	if ((tubp = INODE2TUB(ip)) == NULL)
		return -ENODEV;

	fs3270_wait(tubp, &flags);
	tubp->fsopen = 0;
	tubp->fs_pid = 0;
	tubp->intv = NULL;
	tubp->mode = 0;
	tty3270_refresh(tubp);
	TUBUNLOCK(tubp->irq, flags);
	return 0;
}

/*
 * fs3270_release() called from tty3270_hangup()
 */
void
fs3270_release(tub_t *tubp)
{
	long flags;

	if (tubp->mode != TBM_FS)
		return;
	fs3270_wait(tubp, &flags);
	tubp->fsopen = 0;
	tubp->fs_pid = 0;
	tubp->intv = NULL;
	tubp->mode = 0;
	/*tty3270_refresh(tubp);*/
	TUBUNLOCK(tubp->irq, flags);
}

/*
 * fs3270_wait(tub_t *tubp, int *flags) -- Wait to use tube
 * Entered without irq lock
 * On return:
 *      * Lock is held
 *      * Value is 0 or -ERESTARTSYS
 */
static int
fs3270_wait(tub_t *tubp, long *flags)
{
	DECLARE_WAITQUEUE(wait, current);

	TUBLOCK(tubp->irq, *flags);
	add_wait_queue(&tubp->waitq, &wait);
	while (!signal_pending(current) &&
	    ((tubp->mode != TBM_FS) ||
	     (tubp->flags & (TUB_WORKING | TUB_RDPENDING)) != 0)) {
#warning FIXME: [kj] use set_current_state instead of current->state=
		current->state = TASK_INTERRUPTIBLE;
		TUBUNLOCK(tubp->irq, *flags);
		schedule();
#warning FIXME: [kj] use set_current_state instead of current->state=
		current->state = TASK_RUNNING;
		TUBLOCK(tubp->irq, *flags);
	}
	remove_wait_queue(&tubp->waitq, &wait);
	return signal_pending(current)? -ERESTARTSYS: 0;
}

/*
 * fs3270_io(tubp, ccw1_t*) -- start I/O on the tube
 * Entered with irq lock held, WORKING off
 */
static int
fs3270_io(tub_t *tubp, ccw1_t *ccwp)
{
	int rc;

	rc = do_IO(tubp->irq, ccwp, tubp->irq, 0, 0);
	tubp->flags |= TUB_WORKING;
	tubp->dstat = 0;
	return rc;
}

/*
 * fs3270_tasklet(tubp) -- Perform back-half processing
 */
static void
fs3270_tasklet(unsigned long data)
{
	long flags;
	tub_t *tubp;
	addr_t *ip;

	tubp = (tub_t *) data;
	TUBLOCK(tubp->irq, flags);
	tubp->flags &= ~TUB_BHPENDING;

	if (tubp->wbuf) {       /* if we were writing */
		for (ip = tubp->wbuf; ip < tubp->wbuf+33; ip++) {
			if (*ip == 0)
				break;
			kfree(phys_to_virt(*ip));
		}
		kfree(tubp->wbuf);
		tubp->wbuf = NULL;
	}

	if ((tubp->flags & (TUB_ATTN | TUB_RDPENDING)) ==
	    (TUB_ATTN | TUB_RDPENDING)) {
		fs3270_io(tubp, &tubp->rccw);
		tubp->flags &= ~(TUB_ATTN | TUB_RDPENDING);
	}

	if ((tubp->flags & TUB_WORKING) == 0)
		wake_up_interruptible(&tubp->waitq);

	TUBUNLOCK(tubp->irq, flags);
}

/*
 * fs3270_sched_tasklet(tubp) -- Schedule the back half
 * Irq lock must be held on entry and remains held on exit.
 */
static void
fs3270_sched_tasklet(tub_t *tubp)
{
	if (tubp->flags & TUB_BHPENDING)
		return;
	tubp->flags |= TUB_BHPENDING;
	tasklet_init(&tubp->tasklet, fs3270_tasklet,
		     (unsigned long) tubp);
	tasklet_schedule(&tubp->tasklet);
}

/*
 * fs3270_int(tubp, prp) -- Process interrupt from tube in FS mode
 * This routine is entered with irq lock held (see do_IRQ in s390io.c)
 */
static void
fs3270_int(tub_t *tubp, devstat_t *dsp)
{
#define	DEV_UE_BUSY \
	(DEV_STAT_CHN_END | DEV_STAT_DEV_END | DEV_STAT_UNIT_EXCEP)

#ifdef RBHNOTYET
	/* XXX needs more work; must save 2d arg to fs370_io() */
	/* Handle CE-DE-UE and subsequent UDE */
	if (dsp->dstat == DEV_UE_BUSY) {
		tubp->flags |= TUB_UE_BUSY;
		return;
	} else if (tubp->flags & TUB_UE_BUSY) {
		tubp->flags &= ~TUB_UE_BUSY;
		if (dsp->dstat == DEV_STAT_DEV_END &&
		    (tubp->flags & TUB_WORKING) != 0) {
			fs3270_io(tubp);
			return;
		}
	}
#endif

	/* Handle ATTN */
	if (dsp->dstat & DEV_STAT_ATTENTION)
		tubp->flags |= TUB_ATTN;

	if (dsp->dstat & DEV_STAT_CHN_END) {
		tubp->cswl = dsp->rescnt;
		if ((dsp->dstat & DEV_STAT_DEV_END) == 0)
			tubp->flags |= TUB_EXPECT_DE;
		else
			tubp->flags &= ~TUB_EXPECT_DE;
	} else if (dsp->dstat & DEV_STAT_DEV_END) {
		if ((tubp->flags & TUB_EXPECT_DE) == 0)
			tubp->flags |= TUB_UNSOL_DE;
		tubp->flags &= ~TUB_EXPECT_DE;
	}
	if (dsp->dstat & DEV_STAT_DEV_END)
		tubp->flags &= ~TUB_WORKING;

	if ((tubp->flags & TUB_WORKING) == 0)
		fs3270_sched_tasklet(tubp);
}

/*
 * process ioctl commands for the tube driver
 */
static int
fs3270_ioctl(struct inode *ip, struct file *fp,
	unsigned int cmd, unsigned long arg)
{
	tub_t *tubp;
	int rc = 0;
	long flags;

	if ((tubp = INODE2TUB(ip)) == NULL)
		return -ENODEV;
	if ((rc = fs3270_wait(tubp, &flags))) {
		TUBUNLOCK(tubp->irq, flags);
		return rc;
	}

	switch(cmd) {
	case TUBICMD: tubp->icmd = arg; break;
	case TUBOCMD: tubp->ocmd = arg; break;
	case TUBGETI: put_user(tubp->icmd, (char *)arg); break;
	case TUBGETO: put_user(tubp->ocmd, (char *)arg); break;
	case TUBGETMOD:
		if (copy_to_user((char *)arg, &tubp->tubiocb,
		    sizeof tubp->tubiocb))
			rc = -EFAULT;
		break;
	}
	TUBUNLOCK(tubp->irq, flags);
	return rc;
}

/*
 * process read commands for the tube driver
 */
static ssize_t
fs3270_read(struct file *fp, char *dp, size_t len, loff_t *off)
{
	tub_t *tubp;
	char *kp;
	ccw1_t *cp;
	int rc;
	long flags;
	addr_t *idalp, *ip;
	char *tp;
	int count, piece;
	int size;

	if (len == 0 || len > 65535) {
		return -EINVAL;
	}

	if ((tubp = INODE2TUB((struct inode *)fp->private_data)) == NULL)
		return -ENODEV;

	ip = idalp = kmalloc(33*sizeof(addr_t), GFP_ATOMIC|GFP_DMA);
	if (idalp == NULL)
		return -EFAULT;
	memset(idalp, 0, 33 * sizeof *idalp);
	count = len;
	while (count) {
		piece = MIN(count, 0x800);
		size = count == len? piece: 0x800;
		if ((kp = kmalloc(size, GFP_KERNEL|GFP_DMA)) == NULL) {
			len = -ENOMEM;
			goto do_cleanup;
		}
		*ip++ = virt_to_phys(kp);
		count -= piece;
	}

	if ((rc = fs3270_wait(tubp, &flags)) != 0) {
		TUBUNLOCK(tubp->irq, flags);
		len = rc;
		goto do_cleanup;
	}
	cp = &tubp->rccw;
	if (tubp->icmd == 0 && tubp->ocmd != 0)  tubp->icmd = 6;
	cp->cmd_code = tubp->icmd?:2;
	cp->flags = CCW_FLAG_SLI | CCW_FLAG_IDA;
	cp->count = len;
	cp->cda = virt_to_phys(idalp);
	tubp->flags |= TUB_RDPENDING;
	TUBUNLOCK(tubp->irq, flags);

	if ((rc = fs3270_wait(tubp, &flags)) != 0) {
		tubp->flags &= ~TUB_RDPENDING;
		len = rc;
		TUBUNLOCK(tubp->irq, flags);
		goto do_cleanup;
	}
	TUBUNLOCK(tubp->irq, flags);

	len -= tubp->cswl;
	count = len;
	tp = dp;
	ip = idalp;
	while (count) {
		piece = MIN(count, 0x800);
		if (copy_to_user(tp, phys_to_virt(*ip), piece) != 0) {
			len = -EFAULT;
			goto do_cleanup;
		}
		count -= piece;
		tp += piece;
		ip++;
	}

do_cleanup:
	for (ip = idalp; ip < idalp+33; ip++) {
		if (*ip == 0)
			break;
		kfree(phys_to_virt(*ip));
	}
	kfree(idalp);
	return len;
}

/*
 * process write commands for the tube driver
 */
static ssize_t
fs3270_write(struct file *fp, const char *dp, size_t len, loff_t *off)
{
	tub_t *tubp;
	ccw1_t *cp;
	int rc;
	long flags;
	void *kb;
	addr_t *idalp, *ip;
	int count, piece;
	int index;
	int size;

	if (len > 65535 || len == 0)
		return -EINVAL;

	/* Locate the tube */
	if ((tubp = INODE2TUB((struct inode *)fp->private_data)) == NULL)
		return -ENODEV;

	ip = idalp = kmalloc(33*sizeof(addr_t), GFP_ATOMIC|GFP_DMA);
	if (idalp == NULL)
		return -EFAULT;
	memset(idalp, 0, 33 * sizeof *idalp);

	count = len;
	index = 0;
	while (count) {
		piece = MIN(count, 0x800);
		size = count == len? piece: 0x800;
		if ((kb = kmalloc(size, GFP_KERNEL|GFP_DMA)) == NULL) {
			len = -ENOMEM;
			goto do_cleanup;
		}
		*ip++ = virt_to_phys(kb);
		if (copy_from_user(kb, &dp[index], piece) != 0) {
			len = -EFAULT;
			goto do_cleanup;
		}
		count -= piece;
		index += piece;
	}

	/* Wait till tube's not working or signal is pending */
	if ((rc = fs3270_wait(tubp, &flags))) {
		len = rc;
		TUBUNLOCK(tubp->irq, flags);
		goto do_cleanup;
	}

	/* Make CCW and start I/O.  Back end will free buffers & idal. */
	tubp->wbuf = idalp;
	cp = &tubp->wccw;
	cp->cmd_code = tubp->ocmd? tubp->ocmd == 5? 13: tubp->ocmd: 1;
	cp->flags = CCW_FLAG_SLI | CCW_FLAG_IDA;
	cp->count = len;
	cp->cda = virt_to_phys(tubp->wbuf);
	fs3270_io(tubp, cp);
	TUBUNLOCK(tubp->irq, flags);
	return len;

do_cleanup:
	for (ip = idalp; ip < idalp+33; ip++) {
		if (*ip == 0)
			break;
		kfree(phys_to_virt(*ip));
	}
	kfree(idalp);
	return len;
}

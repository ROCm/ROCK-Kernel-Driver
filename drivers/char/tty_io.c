/*
 *  linux/drivers/char/tty_io.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl, who also corrected VMIN = VTIME = 0.
 *
 * Modified by Theodore Ts'o, 9/14/92, to dynamically allocate the
 * tty_struct and tty_queue structures.  Previously there was an array
 * of 256 tty_struct's which was statically allocated, and the
 * tty_queue structures were allocated at boot time.  Both are now
 * dynamically allocated only when the tty is open.
 *
 * Also restructured routines so that there is more of a separation
 * between the high-level tty routines (tty_io.c and tty_ioctl.c) and
 * the low-level tty routines (serial.c, pty.c, console.c).  This
 * makes for cleaner and more compact code.  -TYT, 9/17/92 
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 *
 * NOTE: pay no attention to the line discipline code (yet); its
 * interface is still subject to change in this version...
 * -- TYT, 1/31/92
 *
 * Added functionality to the OPOST tty handling.  No delays, but all
 * other bits should be there.
 *	-- Nick Holloway <alfie@dcs.warwick.ac.uk>, 27th May 1993.
 *
 * Rewrote canonical mode and added more termios flags.
 * 	-- julian@uhunix.uhcc.hawaii.edu (J. Cowley), 13Jan94
 *
 * Reorganized FASYNC support so mouse code can share it.
 *	-- ctm@ardi.com, 9Sep95
 *
 * New TIOCLINUX variants added.
 *	-- mj@k332.feld.cvut.cz, 19-Nov-95
 * 
 * Restrict vt switching via ioctl()
 *      -- grif@cs.ucr.edu, 5-Dec-95
 *
 * Move console and virtual terminal code to more appropriate files,
 * implement CONFIG_VT and generalize console device interface.
 *	-- Marko Kohtala <Marko.Kohtala@hut.fi>, March 97
 *
 * Rewrote init_dev and release_dev to eliminate races.
 *	-- Bill Hawes <whawes@star.net>, June 97
 *
 * Added devfs support.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 13-Jan-1998
 *
 * Added support for a Unix98-style ptmx device.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 14-Jan-1998
 *
 * Reduced memory usage for older ARM systems
 *      -- Russell King <rmk@arm.linux.org.uk>
 *
 * Move do_SAK() into process context.  Less stack use in devfs functions.
 * alloc_tty_struct() always uses kmalloc() -- Andrew Morton <andrewm@uow.edu.eu> 17Mar01
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/devpts_fs.h>
#include <linux/file.h>
#include <linux/console.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/device.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/devfs_fs_kernel.h>

#include <linux/kmod.h>

#define IS_CONSOLE_DEV(dev)	(kdev_val(dev) == __mkdev(TTY_MAJOR,0))
#define IS_TTY_DEV(dev)		(kdev_val(dev) == __mkdev(TTYAUX_MAJOR,0))
#define IS_SYSCONS_DEV(dev)	(kdev_val(dev) == __mkdev(TTYAUX_MAJOR,1))
#define IS_PTMX_DEV(dev)	(kdev_val(dev) == __mkdev(TTYAUX_MAJOR,2))

#undef TTY_DEBUG_HANGUP

#define TTY_PARANOIA_CHECK 1
#define CHECK_TTY_COUNT 1

struct termios tty_std_termios = {	/* for the benefit of tty drivers  */
	.c_iflag = ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK |
		   ECHOCTL | ECHOKE | IEXTEN,
	.c_cc = INIT_C_CC
};

LIST_HEAD(tty_drivers);			/* linked list of tty drivers */
struct tty_ldisc ldiscs[NR_LDISCS];	/* line disc dispatch table	*/

#ifdef CONFIG_UNIX98_PTYS
extern struct tty_driver ptm_driver[];	/* Unix98 pty masters; for /dev/ptmx */
extern struct tty_driver pts_driver[];	/* Unix98 pty slaves;  for /dev/ptmx */
#endif

extern void disable_early_printk(void);

/*
 * redirect is the pseudo-tty that console output
 * is redirected to if asked by TIOCCONS.
 */
static struct tty_struct *redirect;

static void initialize_tty_struct(struct tty_struct *tty);

static ssize_t tty_read(struct file *, char *, size_t, loff_t *);
static ssize_t tty_write(struct file *, const char *, size_t, loff_t *);
static unsigned int tty_poll(struct file *, poll_table *);
static int tty_open(struct inode *, struct file *);
static int tty_release(struct inode *, struct file *);
int tty_ioctl(struct inode * inode, struct file * file,
	      unsigned int cmd, unsigned long arg);
static int tty_fasync(int fd, struct file * filp, int on);
extern int vme_scc_init (void);
extern int serial167_init(void);
extern int rs_8xx_init(void);
extern void sclp_tty_init(void);
extern void tty3215_init(void);
extern void tub3270_init(void);
extern void rs_360_init(void);
extern void tx3912_rs_init(void);

static struct tty_struct *alloc_tty_struct(void)
{
	struct tty_struct *tty;

	tty = kmalloc(sizeof(struct tty_struct), GFP_KERNEL);
	if (tty)
		memset(tty, 0, sizeof(struct tty_struct));
	return tty;
}

static inline void free_tty_struct(struct tty_struct *tty)
{
	kfree(tty);
}

/*
 * This routine returns the name of tty.
 */
static char *
_tty_make_name(struct tty_struct *tty, const char *name, char *buf)
{
	int idx = (tty)? minor(tty->device) - tty->driver.minor_start:0;

	if (!tty) /* Hmm.  NULL pointer.  That's fun. */
		strcpy(buf, "NULL tty");
	else
		sprintf(buf, name,
			idx + tty->driver.name_base);
		
	return buf;
}

#define TTY_NUMBER(tty) (minor((tty)->device) - (tty)->driver.minor_start + \
			 (tty)->driver.name_base)

char *tty_name(struct tty_struct *tty, char *buf)
{
	return _tty_make_name(tty, (tty)?tty->driver.name:NULL, buf);
}

EXPORT_SYMBOL(tty_name);

inline int tty_paranoia_check(struct tty_struct *tty, kdev_t device,
			      const char *routine)
{
#ifdef TTY_PARANOIA_CHECK
	static const char badmagic[] = KERN_WARNING
		"Warning: bad magic number for tty struct (%s) in %s\n";
	static const char badtty[] = KERN_WARNING
		"Warning: null TTY for (%s) in %s\n";

	if (!tty) {
		printk(badtty, cdevname(device), routine);
		return 1;
	}
	if (tty->magic != TTY_MAGIC) {
		printk(badmagic, cdevname(device), routine);
		return 1;
	}
#endif
	return 0;
}

static int check_tty_count(struct tty_struct *tty, const char *routine)
{
#ifdef CHECK_TTY_COUNT
	struct list_head *p;
	int count = 0;
	
	file_list_lock();
	list_for_each(p, &tty->tty_files) {
		count++;
	}
	file_list_unlock();
	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_SLAVE &&
	    tty->link && tty->link->count)
		count++;
	if (tty->count != count) {
		printk(KERN_WARNING "Warning: dev (%s) tty->count(%d) "
				    "!= #fd's(%d) in %s\n",
		       cdevname(tty->device), tty->count, count, routine);
		return count;
       }	
#endif
	return 0;
}

int tty_register_ldisc(int disc, struct tty_ldisc *new_ldisc)
{
	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;
	
	if (new_ldisc) {
		ldiscs[disc] = *new_ldisc;
		ldiscs[disc].flags |= LDISC_FLAG_DEFINED;
		ldiscs[disc].num = disc;
	} else
		memset(&ldiscs[disc], 0, sizeof(struct tty_ldisc));
	
	return 0;
}

EXPORT_SYMBOL(tty_register_ldisc);

/* Set the discipline of a tty line. */
static int tty_set_ldisc(struct tty_struct *tty, int ldisc)
{
	int	retval = 0;
	struct	tty_ldisc o_ldisc;
	char buf[64];

	if ((ldisc < N_TTY) || (ldisc >= NR_LDISCS))
		return -EINVAL;
	/* Eduardo Blanco <ejbs@cs.cs.com.uy> */
	/* Cyrus Durgin <cider@speakeasy.org> */
	if (!(ldiscs[ldisc].flags & LDISC_FLAG_DEFINED)) {
		char modname [20];
		sprintf(modname, "tty-ldisc-%d", ldisc);
		request_module (modname);
	}
	if (!(ldiscs[ldisc].flags & LDISC_FLAG_DEFINED))
		return -EINVAL;

	if (tty->ldisc.num == ldisc)
		return 0;	/* We are already in the desired discipline */

	if (!try_module_get(ldiscs[ldisc].owner))
	       	return -EINVAL;
	
	o_ldisc = tty->ldisc;

	tty_wait_until_sent(tty, 0);
	
	/* Shutdown the current discipline. */
	if (tty->ldisc.close)
		(tty->ldisc.close)(tty);

	/* Now set up the new line discipline. */
	tty->ldisc = ldiscs[ldisc];
	tty->termios->c_line = ldisc;
	if (tty->ldisc.open)
		retval = (tty->ldisc.open)(tty);
	if (retval < 0) {
		tty->ldisc = o_ldisc;
		tty->termios->c_line = tty->ldisc.num;
		if (tty->ldisc.open && (tty->ldisc.open(tty) < 0)) {
			tty->ldisc = ldiscs[N_TTY];
			tty->termios->c_line = N_TTY;
			if (tty->ldisc.open) {
				int r = tty->ldisc.open(tty);

				if (r < 0)
					panic("Couldn't open N_TTY ldisc for "
					      "%s --- error %d.",
					      tty_name(tty, buf), r);
			}
		}
	} else {
		module_put(o_ldisc.owner);
	}
	
	if (tty->ldisc.num != o_ldisc.num && tty->driver.set_ldisc)
		tty->driver.set_ldisc(tty);
	return retval;
}

/*
 * This routine returns a tty driver structure, given a device number
 */
struct tty_driver *get_tty_driver(kdev_t device)
{
	int	major, minor;
	struct tty_driver *p;
	
	minor = minor(device);
	major = major(device);

	list_for_each_entry(p, &tty_drivers, tty_drivers) {
		if (p->major != major)
			continue;
		if (minor < p->minor_start)
			continue;
		if (minor >= p->minor_start + p->num)
			continue;
		return p;
	}
	return NULL;
}

/*
 * If we try to write to, or set the state of, a terminal and we're
 * not in the foreground, send a SIGTTOU.  If the signal is blocked or
 * ignored, go ahead and perform the operation.  (POSIX 7.2)
 */
int tty_check_change(struct tty_struct * tty)
{
	if (current->tty != tty)
		return 0;
	if (tty->pgrp <= 0) {
		printk(KERN_WARNING "tty_check_change: tty->pgrp <= 0!\n");
		return 0;
	}
	if (current->pgrp == tty->pgrp)
		return 0;
	if (is_ignored(SIGTTOU))
		return 0;
	if (is_orphaned_pgrp(current->pgrp))
		return -EIO;
	(void) kill_pg(current->pgrp,SIGTTOU,1);
	return -ERESTARTSYS;
}

EXPORT_SYMBOL(tty_check_change);

static ssize_t hung_up_tty_read(struct file * file, char * buf,
				size_t count, loff_t *ppos)
{
	/* Can't seek (pread) on ttys.  */
	if (ppos != &file->f_pos)
		return -ESPIPE;
	return 0;
}

static ssize_t hung_up_tty_write(struct file * file, const char * buf,
				 size_t count, loff_t *ppos)
{
	/* Can't seek (pwrite) on ttys.  */
	if (ppos != &file->f_pos)
		return -ESPIPE;
	return -EIO;
}

/* No kernel lock held - none needed ;) */
static unsigned int hung_up_tty_poll(struct file * filp, poll_table * wait)
{
	return POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDNORM | POLLWRNORM;
}

static int hung_up_tty_ioctl(struct inode * inode, struct file * file,
			     unsigned int cmd, unsigned long arg)
{
	return cmd == TIOCSPGRP ? -ENOTTY : -EIO;
}

static struct file_operations tty_fops = {
	.llseek		= no_llseek,
	.read		= tty_read,
	.write		= tty_write,
	.poll		= tty_poll,
	.ioctl		= tty_ioctl,
	.open		= tty_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
};

static struct file_operations hung_up_tty_fops = {
	.llseek		= no_llseek,
	.read		= hung_up_tty_read,
	.write		= hung_up_tty_write,
	.poll		= hung_up_tty_poll,
	.ioctl		= hung_up_tty_ioctl,
	.release	= tty_release,
};

/*
 * This can be called by the "eventd" kernel thread.  That is process synchronous,
 * but doesn't hold any locks, so we need to make sure we have the appropriate
 * locks for what we're doing..
 */
void do_tty_hangup(void *data)
{
	struct tty_struct *tty = (struct tty_struct *) data;
	struct file * cons_filp = NULL;
	struct file *filp;
	struct task_struct *p;
	struct pid *pid;
	int    closecount = 0, n;

	if (!tty)
		return;

	/* inuse_filps is protected by the single kernel lock */
	lock_kernel();
	
	check_tty_count(tty, "do_tty_hangup");
	file_list_lock();
	list_for_each_entry(filp, &tty->tty_files, f_list) {
		if (IS_CONSOLE_DEV(filp->f_dentry->d_inode->i_rdev) ||
		    IS_SYSCONS_DEV(filp->f_dentry->d_inode->i_rdev)) {
			cons_filp = filp;
			continue;
		}
		if (filp->f_op != &tty_fops)
			continue;
		closecount++;
		tty_fasync(-1, filp, 0);	/* can't block */
		filp->f_op = &hung_up_tty_fops;
	}
	file_list_unlock();
	
	/* FIXME! What are the locking issues here? This may me overdoing things..
	* this question is especially important now that we've removed the irqlock. */
	{
		unsigned long flags;

		local_irq_save(flags); // FIXME: is this safe?
		if (tty->ldisc.flush_buffer)
			tty->ldisc.flush_buffer(tty);
		if (tty->driver.flush_buffer)
			tty->driver.flush_buffer(tty);
		if ((test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		local_irq_restore(flags); // FIXME: is this safe?
	}

	wake_up_interruptible(&tty->write_wait);
	wake_up_interruptible(&tty->read_wait);

	/*
	 * Shutdown the current line discipline, and reset it to
	 * N_TTY.
	 */
	if (tty->driver.flags & TTY_DRIVER_RESET_TERMIOS)
		*tty->termios = tty->driver.init_termios;
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close)(tty);
		module_put(tty->ldisc.owner);
		
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open) {
			int i = (tty->ldisc.open)(tty);
			if (i < 0)
				printk(KERN_ERR "do_tty_hangup: N_TTY open: "
						"error %d\n", -i);
		}
	}
	
	read_lock(&tasklist_lock);
	if (tty->session > 0) {
		struct list_head *l;
		for_each_task_pid(tty->session, PIDTYPE_SID, p, l, pid) {
			if (p->tty == tty)
				p->tty = NULL;
			if (!p->leader)
				continue;
			send_group_sig_info(SIGHUP, SEND_SIG_PRIV, p);
			send_group_sig_info(SIGCONT, SEND_SIG_PRIV, p);
			if (tty->pgrp > 0)
				p->tty_old_pgrp = tty->pgrp;
		}
	}
	read_unlock(&tasklist_lock);

	tty->flags = 0;
	tty->session = 0;
	tty->pgrp = -1;
	tty->ctrl_status = 0;
	/*
	 *	If one of the devices matches a console pointer, we
	 *	cannot just call hangup() because that will cause
	 *	tty->count and state->count to go out of sync.
	 *	So we just call close() the right number of times.
	 */
	if (cons_filp) {
		if (tty->driver.close)
			for (n = 0; n < closecount; n++)
				tty->driver.close(tty, cons_filp);
	} else if (tty->driver.hangup)
		(tty->driver.hangup)(tty);
	unlock_kernel();
}

void tty_hangup(struct tty_struct * tty)
{
#ifdef TTY_DEBUG_HANGUP
	char	buf[64];
	
	printk(KERN_DEBUG "%s hangup...\n", tty_name(tty, buf));
#endif
	schedule_work(&tty->hangup_work);
}

EXPORT_SYMBOL(tty_hangup);

void tty_vhangup(struct tty_struct * tty)
{
#ifdef TTY_DEBUG_HANGUP
	char	buf[64];

	printk(KERN_DEBUG "%s vhangup...\n", tty_name(tty, buf));
#endif
	do_tty_hangup((void *) tty);
}
EXPORT_SYMBOL(tty_vhangup);

int tty_hung_up_p(struct file * filp)
{
	return (filp->f_op == &hung_up_tty_fops);
}

EXPORT_SYMBOL(tty_hung_up_p);

/*
 * This function is typically called only by the session leader, when
 * it wants to disassociate itself from its controlling tty.
 *
 * It performs the following functions:
 * 	(1)  Sends a SIGHUP and SIGCONT to the foreground process group
 * 	(2)  Clears the tty from being controlling the session
 * 	(3)  Clears the controlling tty for all processes in the
 * 		session group.
 *
 * The argument on_exit is set to 1 if called when a process is
 * exiting; it is 0 if called by the ioctl TIOCNOTTY.
 */
void disassociate_ctty(int on_exit)
{
	struct tty_struct *tty;
	struct task_struct *p;
	struct list_head *l;
	struct pid *pid;
	int tty_pgrp = -1;

	lock_kernel();

	tty = current->tty;
	if (tty) {
		tty_pgrp = tty->pgrp;
		if (on_exit && tty->driver.type != TTY_DRIVER_TYPE_PTY)
			tty_vhangup(tty);
	} else {
		if (current->tty_old_pgrp) {
			kill_pg(current->tty_old_pgrp, SIGHUP, on_exit);
			kill_pg(current->tty_old_pgrp, SIGCONT, on_exit);
		}
		unlock_kernel();	
		return;
	}
	if (tty_pgrp > 0) {
		kill_pg(tty_pgrp, SIGHUP, on_exit);
		if (!on_exit)
			kill_pg(tty_pgrp, SIGCONT, on_exit);
	}

	current->tty_old_pgrp = 0;
	tty->session = 0;
	tty->pgrp = -1;

	read_lock(&tasklist_lock);
	for_each_task_pid(current->session, PIDTYPE_SID, p, l, pid)
		p->tty = NULL;
	read_unlock(&tasklist_lock);
	unlock_kernel();
}

void stop_tty(struct tty_struct *tty)
{
	if (tty->stopped)
		return;
	tty->stopped = 1;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_START;
		tty->ctrl_status |= TIOCPKT_STOP;
		wake_up_interruptible(&tty->link->read_wait);
	}
	if (tty->driver.stop)
		(tty->driver.stop)(tty);
}

void start_tty(struct tty_struct *tty)
{
	if (!tty->stopped || tty->flow_stopped)
		return;
	tty->stopped = 0;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_STOP;
		tty->ctrl_status |= TIOCPKT_START;
		wake_up_interruptible(&tty->link->read_wait);
	}
	if (tty->driver.start)
		(tty->driver.start)(tty);
	if ((test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
}

static ssize_t tty_read(struct file * file, char * buf, size_t count, 
			loff_t *ppos)
{
	int i;
	struct tty_struct * tty;
	struct inode *inode;

	/* Can't seek (pread) on ttys.  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	tty = (struct tty_struct *)file->private_data;
	inode = file->f_dentry->d_inode;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_read"))
		return -EIO;
	if (!tty || (test_bit(TTY_IO_ERROR, &tty->flags)))
		return -EIO;

	/* This check not only needs to be done before reading, but also
	   whenever read_chan() gets woken up after sleeping, so I've
	   moved it to there.  This should only be done for the N_TTY
	   line discipline, anyway.  Same goes for write_chan(). -- jlc. */
#if 0
	if (!IS_CONSOLE_DEV(inode->i_rdev) && /* don't stop on /dev/console */
	    (tty->pgrp > 0) &&
	    (current->tty == tty) &&
	    (tty->pgrp != current->pgrp))
		if (is_ignored(SIGTTIN) || is_orphaned_pgrp(current->pgrp))
			return -EIO;
		else {
			(void) kill_pg(current->pgrp, SIGTTIN, 1);
			return -ERESTARTSYS;
		}
#endif
	lock_kernel();
	if (tty->ldisc.read)
		i = (tty->ldisc.read)(tty,file,buf,count);
	else
		i = -EIO;
	unlock_kernel();
	if (i > 0)
		inode->i_atime = CURRENT_TIME;
	return i;
}

/*
 * Split writes up in sane blocksizes to avoid
 * denial-of-service type attacks
 */
static inline ssize_t do_tty_write(
	ssize_t (*write)(struct tty_struct *, struct file *, const unsigned char *, size_t),
	struct tty_struct *tty,
	struct file *file,
	const unsigned char *buf,
	size_t count)
{
	ssize_t ret = 0, written = 0;
	
	if (down_interruptible(&tty->atomic_write)) {
		return -ERESTARTSYS;
	}
	if ( test_bit(TTY_NO_WRITE_SPLIT, &tty->flags) ) {
		lock_kernel();
		written = write(tty, file, buf, count);
		unlock_kernel();
	} else {
		for (;;) {
			unsigned long size = max((unsigned long)PAGE_SIZE*2, 16384UL);
			if (size > count)
				size = count;
			lock_kernel();
			ret = write(tty, file, buf, size);
			unlock_kernel();
			if (ret <= 0)
				break;
			written += ret;
			buf += ret;
			count -= ret;
			if (!count)
				break;
			ret = -ERESTARTSYS;
			if (signal_pending(current))
				break;
			cond_resched();
		}
	}
	if (written) {
		file->f_dentry->d_inode->i_mtime = CURRENT_TIME;
		ret = written;
	}
	up(&tty->atomic_write);
	return ret;
}


static ssize_t tty_write(struct file * file, const char * buf, size_t count,
			 loff_t *ppos)
{
	int is_console;
	struct tty_struct * tty;
	struct inode *inode;

	/* Can't seek (pwrite) on ttys.  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	/*
	 *      For now, we redirect writes from /dev/console as
	 *      well as /dev/tty0.
	 */
	inode = file->f_dentry->d_inode;
	is_console = IS_SYSCONS_DEV(inode->i_rdev) ||
		     IS_CONSOLE_DEV(inode->i_rdev);

	if (is_console && redirect)
		tty = redirect;
	else
		tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_write"))
		return -EIO;
	if (!tty || !tty->driver.write || (test_bit(TTY_IO_ERROR, &tty->flags)))
		return -EIO;
#if 0
	if (!is_console && L_TOSTOP(tty) && (tty->pgrp > 0) &&
	    (current->tty == tty) && (tty->pgrp != current->pgrp)) {
		if (is_orphaned_pgrp(current->pgrp))
			return -EIO;
		if (!is_ignored(SIGTTOU)) {
			(void) kill_pg(current->pgrp, SIGTTOU, 1);
			return -ERESTARTSYS;
		}
	}
#endif
	if (!tty->ldisc.write)
		return -EIO;
	return do_tty_write(tty->ldisc.write, tty, file,
			    (const unsigned char *)buf, count);
}

/* Semaphore to protect creating and releasing a tty */
static DECLARE_MUTEX(tty_sem);

static void down_tty_sem(int index)
{
	down(&tty_sem);
}

static void up_tty_sem(int index)
{
	up(&tty_sem);
}

static void release_mem(struct tty_struct *tty, int idx);

/*
 * WSH 06/09/97: Rewritten to remove races and properly clean up after a
 * failed open.  The new code protects the open with a semaphore, so it's
 * really quite straightforward.  The semaphore locking can probably be
 * relaxed for the (most common) case of reopening a tty.
 */
static int init_dev(kdev_t device, struct tty_struct **ret_tty)
{
	struct tty_struct *tty, *o_tty;
	struct termios *tp, **tp_loc, *o_tp, **o_tp_loc;
	struct termios *ltp, **ltp_loc, *o_ltp, **o_ltp_loc;
	struct tty_driver *driver;	
	int retval=0;
	int idx;

	driver = get_tty_driver(device);
	if (!driver)
		return -ENODEV;

	idx = minor(device) - driver->minor_start;

	/* 
	 * Check whether we need to acquire the tty semaphore to avoid
	 * race conditions.  For now, play it safe.
	 */
	down_tty_sem(idx);

	/* check whether we're reopening an existing tty */
	tty = driver->table[idx];
	if (tty) goto fast_track;

	/*
	 * First time open is complex, especially for PTY devices.
	 * This code guarantees that either everything succeeds and the
	 * TTY is ready for operation, or else the table slots are vacated
	 * and the allocated memory released.  (Except that the termios 
	 * and locked termios may be retained.)
	 */

	if (!try_module_get(driver->owner)) {
		retval = -ENODEV;
		goto end_init;
	}

	o_tty = NULL;
	tp = o_tp = NULL;
	ltp = o_ltp = NULL;

	tty = alloc_tty_struct();
	if(!tty)
		goto fail_no_mem;
	initialize_tty_struct(tty);
	tty->device = device;
	tty->driver = *driver;

	tp_loc = &driver->termios[idx];
	if (!*tp_loc) {
		tp = (struct termios *) kmalloc(sizeof(struct termios),
						GFP_KERNEL);
		if (!tp)
			goto free_mem_out;
		*tp = driver->init_termios;
	}

	ltp_loc = &driver->termios_locked[idx];
	if (!*ltp_loc) {
		ltp = (struct termios *) kmalloc(sizeof(struct termios),
						 GFP_KERNEL);
		if (!ltp)
			goto free_mem_out;
		memset(ltp, 0, sizeof(struct termios));
	}

	if (driver->type == TTY_DRIVER_TYPE_PTY) {
		o_tty = alloc_tty_struct();
		if (!o_tty)
			goto free_mem_out;
		initialize_tty_struct(o_tty);
		o_tty->device = mk_kdev(driver->other->major,
					driver->other->minor_start + idx);
		o_tty->driver = *driver->other;

		o_tp_loc  = &driver->other->termios[idx];
		if (!*o_tp_loc) {
			o_tp = (struct termios *)
				kmalloc(sizeof(struct termios), GFP_KERNEL);
			if (!o_tp)
				goto free_mem_out;
			*o_tp = driver->other->init_termios;
		}

		o_ltp_loc = &driver->other->termios_locked[idx];
		if (!*o_ltp_loc) {
			o_ltp = (struct termios *)
				kmalloc(sizeof(struct termios), GFP_KERNEL);
			if (!o_ltp)
				goto free_mem_out;
			memset(o_ltp, 0, sizeof(struct termios));
		}

		/*
		 * Everything allocated ... set up the o_tty structure.
		 */
		driver->other->table[idx] = o_tty;
		if (!*o_tp_loc)
			*o_tp_loc = o_tp;
		if (!*o_ltp_loc)
			*o_ltp_loc = o_ltp;
		o_tty->termios = *o_tp_loc;
		o_tty->termios_locked = *o_ltp_loc;
		(*driver->other->refcount)++;
		if (driver->subtype == PTY_TYPE_MASTER)
			o_tty->count++;

		/* Establish the links in both directions */
		tty->link   = o_tty;
		o_tty->link = tty;
	}

	/* 
	 * All structures have been allocated, so now we install them.
	 * Failures after this point use release_mem to clean up, so 
	 * there's no need to null out the local pointers.
	 */
	driver->table[idx] = tty;
	
	if (!*tp_loc)
		*tp_loc = tp;
	if (!*ltp_loc)
		*ltp_loc = ltp;
	tty->termios = *tp_loc;
	tty->termios_locked = *ltp_loc;
	(*driver->refcount)++;
	tty->count++;

	/* 
	 * Structures all installed ... call the ldisc open routines.
	 * If we fail here just call release_mem to clean up.  No need
	 * to decrement the use counts, as release_mem doesn't care.
	 */
	if (tty->ldisc.open) {
		retval = (tty->ldisc.open)(tty);
		if (retval)
			goto release_mem_out;
	}
	if (o_tty && o_tty->ldisc.open) {
		retval = (o_tty->ldisc.open)(o_tty);
		if (retval) {
			if (tty->ldisc.close)
				(tty->ldisc.close)(tty);
			goto release_mem_out;
		}
	}
	goto success;

	/*
	 * This fast open can be used if the tty is already open.
	 * No memory is allocated, and the only failures are from
	 * attempting to open a closing tty or attempting multiple
	 * opens on a pty master.
	 */
fast_track:
	if (test_bit(TTY_CLOSING, &tty->flags)) {
		retval = -EIO;
		goto end_init;
	}
	if (driver->type == TTY_DRIVER_TYPE_PTY &&
	    driver->subtype == PTY_TYPE_MASTER) {
		/*
		 * special case for PTY masters: only one open permitted, 
		 * and the slave side open count is incremented as well.
		 */
		if (tty->count) {
			retval = -EIO;
			goto end_init;
		}
		tty->link->count++;
	}
	tty->count++;
	tty->driver = *driver; /* N.B. why do this every time?? */

success:
	*ret_tty = tty;
	
	/* All paths come through here to release the semaphore */
end_init:
	up_tty_sem(idx);
	return retval;

	/* Release locally allocated memory ... nothing placed in slots */
free_mem_out:
	if (o_tp)
		kfree(o_tp);
	if (o_tty)
		free_tty_struct(o_tty);
	if (ltp)
		kfree(ltp);
	if (tp)
		kfree(tp);
	free_tty_struct(tty);

fail_no_mem:
	module_put(driver->owner);
	retval = -ENOMEM;
	goto end_init;

	/* call the tty release_mem routine to clean out this slot */
release_mem_out:
	printk(KERN_INFO "init_dev: ldisc open failed, "
			 "clearing slot %d\n", idx);
	release_mem(tty, idx);
	goto end_init;
}

/*
 * Releases memory associated with a tty structure, and clears out the
 * driver table slots.
 */
static void release_mem(struct tty_struct *tty, int idx)
{
	struct tty_struct *o_tty;
	struct termios *tp;

	if ((o_tty = tty->link) != NULL) {
		o_tty->driver.table[idx] = NULL;
		if (o_tty->driver.flags & TTY_DRIVER_RESET_TERMIOS) {
			tp = o_tty->driver.termios[idx];
			o_tty->driver.termios[idx] = NULL;
			kfree(tp);
		}
		o_tty->magic = 0;
		(*o_tty->driver.refcount)--;
		file_list_lock();
		list_del(&o_tty->tty_files);
		file_list_unlock();
		free_tty_struct(o_tty);
	}

	tty->driver.table[idx] = NULL;
	if (tty->driver.flags & TTY_DRIVER_RESET_TERMIOS) {
		tp = tty->driver.termios[idx];
		tty->driver.termios[idx] = NULL;
		kfree(tp);
	}
	tty->magic = 0;
	(*tty->driver.refcount)--;
	file_list_lock();
	list_del(&tty->tty_files);
	file_list_unlock();
	module_put(tty->driver.owner);
	free_tty_struct(tty);
}

/*
 * Even releasing the tty structures is a tricky business.. We have
 * to be very careful that the structures are all released at the
 * same time, as interrupts might otherwise get the wrong pointers.
 *
 * WSH 09/09/97: rewritten to avoid some nasty race conditions that could
 * lead to double frees or releasing memory still in use.
 */
static void release_dev(struct file * filp)
{
	struct tty_struct *tty, *o_tty;
	int	pty_master, tty_closing, o_tty_closing, do_sleep;
	int	idx;
	char	buf[64];
	
	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_dentry->d_inode->i_rdev, "release_dev"))
		return;

	check_tty_count(tty, "release_dev");

	tty_fasync(-1, filp, 0);

	idx = minor(tty->device) - tty->driver.minor_start;
	pty_master = (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
		      tty->driver.subtype == PTY_TYPE_MASTER);
	o_tty = tty->link;

#ifdef TTY_PARANOIA_CHECK
	if (idx < 0 || idx >= tty->driver.num) {
		printk(KERN_DEBUG "release_dev: bad idx when trying to "
				  "free (%s)\n", cdevname(tty->device));
		return;
	}
	if (tty != tty->driver.table[idx]) {
		printk(KERN_DEBUG "release_dev: driver.table[%d] not tty "
				  "for (%s)\n", idx, cdevname(tty->device));
		return;
	}
	if (tty->termios != tty->driver.termios[idx]) {
		printk(KERN_DEBUG "release_dev: driver.termios[%d] not termios "
		       "for (%s)\n",
		       idx, cdevname(tty->device));
		return;
	}
	if (tty->termios_locked != tty->driver.termios_locked[idx]) {
		printk(KERN_DEBUG "release_dev: driver.termios_locked[%d] not "
		       "termios_locked for (%s)\n",
		       idx, cdevname(tty->device));
		return;
	}
#endif

#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "release_dev of %s (tty count=%d)...",
	       tty_name(tty, buf), tty->count);
#endif

#ifdef TTY_PARANOIA_CHECK
	if (tty->driver.other) {
		if (o_tty != tty->driver.other->table[idx]) {
			printk(KERN_DEBUG "release_dev: other->table[%d] "
					  "not o_tty for (%s)\n",
			       idx, cdevname(tty->device));
			return;
		}
		if (o_tty->termios != tty->driver.other->termios[idx]) {
			printk(KERN_DEBUG "release_dev: other->termios[%d] "
					  "not o_termios for (%s)\n",
			       idx, cdevname(tty->device));
			return;
		}
		if (o_tty->termios_locked != 
		      tty->driver.other->termios_locked[idx]) {
			printk(KERN_DEBUG "release_dev: other->termios_locked["
					  "%d] not o_termios_locked for (%s)\n",
			       idx, cdevname(tty->device));
			return;
		}
		if (o_tty->link != tty) {
			printk(KERN_DEBUG "release_dev: bad pty pointers\n");
			return;
		}
	}
#endif

	if (tty->driver.close)
		tty->driver.close(tty, filp);

	/*
	 * Sanity check: if tty->count is going to zero, there shouldn't be
	 * any waiters on tty->read_wait or tty->write_wait.  We test the
	 * wait queues and kick everyone out _before_ actually starting to
	 * close.  This ensures that we won't block while releasing the tty
	 * structure.
	 *
	 * The test for the o_tty closing is necessary, since the master and
	 * slave sides may close in any order.  If the slave side closes out
	 * first, its count will be one, since the master side holds an open.
	 * Thus this test wouldn't be triggered at the time the slave closes,
	 * so we do it now.
	 *
	 * Note that it's possible for the tty to be opened again while we're
	 * flushing out waiters.  By recalculating the closing flags before
	 * each iteration we avoid any problems.
	 */
	while (1) {
		tty_closing = tty->count <= 1;
		o_tty_closing = o_tty &&
			(o_tty->count <= (pty_master ? 1 : 0));
		do_sleep = 0;

		if (tty_closing) {
			if (waitqueue_active(&tty->read_wait)) {
				wake_up(&tty->read_wait);
				do_sleep++;
			}
			if (waitqueue_active(&tty->write_wait)) {
				wake_up(&tty->write_wait);
				do_sleep++;
			}
		}
		if (o_tty_closing) {
			if (waitqueue_active(&o_tty->read_wait)) {
				wake_up(&o_tty->read_wait);
				do_sleep++;
			}
			if (waitqueue_active(&o_tty->write_wait)) {
				wake_up(&o_tty->write_wait);
				do_sleep++;
			}
		}
		if (!do_sleep)
			break;

		printk(KERN_WARNING "release_dev: %s: read/write wait queue "
				    "active!\n", tty_name(tty, buf));
		schedule();
	}	

	/*
	 * The closing flags are now consistent with the open counts on 
	 * both sides, and we've completed the last operation that could 
	 * block, so it's safe to proceed with closing.
	 */
	if (pty_master) {
		if (--o_tty->count < 0) {
			printk(KERN_WARNING "release_dev: bad pty slave count "
					    "(%d) for %s\n",
			       o_tty->count, tty_name(o_tty, buf));
			o_tty->count = 0;
		}
	}
	if (--tty->count < 0) {
		printk(KERN_WARNING "release_dev: bad tty->count (%d) for %s\n",
		       tty->count, tty_name(tty, buf));
		tty->count = 0;
	}

	/*
	 * We've decremented tty->count, so we need to remove this file
	 * descriptor off the tty->tty_files list; this serves two
	 * purposes:
	 *  - check_tty_count sees the correct number of file descriptors
	 *    associated with this tty.
	 *  - do_tty_hangup no longer sees this file descriptor as
	 *    something that needs to be handled for hangups.
	 */
	file_kill(filp);
	filp->private_data = NULL;

	/*
	 * Perform some housekeeping before deciding whether to return.
	 *
	 * Set the TTY_CLOSING flag if this was the last open.  In the
	 * case of a pty we may have to wait around for the other side
	 * to close, and TTY_CLOSING makes sure we can't be reopened.
	 */
	if(tty_closing)
		set_bit(TTY_CLOSING, &tty->flags);
	if(o_tty_closing)
		set_bit(TTY_CLOSING, &o_tty->flags);

	/*
	 * If _either_ side is closing, make sure there aren't any
	 * processes that still think tty or o_tty is their controlling
	 * tty.  Also, clear redirect if it points to either tty.
	 */
	if (tty_closing || o_tty_closing) {
		struct task_struct *p;
		struct list_head *l;
		struct pid *pid;

		read_lock(&tasklist_lock);
		for_each_task_pid(tty->session, PIDTYPE_SID, p, l, pid)
			p->tty = NULL;
		if (o_tty)
			for_each_task_pid(o_tty->session, PIDTYPE_SID, p,l, pid)
				p->tty = NULL;
		read_unlock(&tasklist_lock);

		if (redirect == tty || (o_tty && redirect == o_tty))
			redirect = NULL;
	}

	/* check whether both sides are closing ... */
	if (!tty_closing || (o_tty && !o_tty_closing))
		return;
	
#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "freeing tty structure...");
#endif

	/*
	 * Shutdown the current line discipline, and reset it to N_TTY.
	 * N.B. why reset ldisc when we're releasing the memory??
	 */
	if (tty->ldisc.close)
		(tty->ldisc.close)(tty);
	module_put(tty->ldisc.owner);
	
	tty->ldisc = ldiscs[N_TTY];
	tty->termios->c_line = N_TTY;
	if (o_tty) {
		if (o_tty->ldisc.close)
			(o_tty->ldisc.close)(o_tty);
		module_put(o_tty->ldisc.owner);
		o_tty->ldisc = ldiscs[N_TTY];
	}
	
	/*
	 * Prevent flush_to_ldisc() from rescheduling the work for later.  Then
	 * kill any delayed work.
	 */
	clear_bit(TTY_DONT_FLIP, &tty->flags);
	cancel_delayed_work(&tty->flip.work);

	/*
	 * Wait for ->hangup_work and ->flip.work handlers to terminate
	 */
	flush_scheduled_work();

	/* 
	 * The release_mem function takes care of the details of clearing
	 * the slots and preserving the termios structure.
	 */
	release_mem(tty, idx);
}

/*
 * tty_open and tty_release keep up the tty count that contains the
 * number of opens done on a tty. We cannot use the inode-count, as
 * different inodes might point to the same tty.
 *
 * Open-counting is needed for pty masters, as well as for keeping
 * track of serial lines: DTR is dropped when the last close happens.
 * (This is not done solely through tty->count, now.  - Ted 1/27/92)
 *
 * The termios state of a pty is reset on first open so that
 * settings don't persist across reuse.
 */
static int tty_open(struct inode * inode, struct file * filp)
{
	struct tty_struct *tty;
	int noctty, retval;
	kdev_t device;
	unsigned short saved_flags;
	char	buf[64];

	saved_flags = filp->f_flags;
retry_open:
	noctty = filp->f_flags & O_NOCTTY;
	device = inode->i_rdev;
	if (IS_TTY_DEV(device)) {
		if (!current->tty)
			return -ENXIO;
		device = current->tty->device;
		filp->f_flags |= O_NONBLOCK; /* Don't let /dev/tty block */
		/* noctty = 1; */
	}
#ifdef CONFIG_VT
	if (IS_CONSOLE_DEV(device)) {
		extern int fg_console;
		device = mk_kdev(TTY_MAJOR, fg_console + 1);
		noctty = 1;
	}
#endif
	if (IS_SYSCONS_DEV(device)) {
		struct console *c = console_drivers;
		while(c && !c->device)
			c = c->next;
		if (!c)
                        return -ENODEV;
                device = c->device(c);
		filp->f_flags |= O_NONBLOCK; /* Don't let /dev/console block */
		noctty = 1;
	}

	if (IS_PTMX_DEV(device)) {
#ifdef CONFIG_UNIX98_PTYS
		/* find a free pty. */
		int major, minor;
		struct tty_driver *driver;

		/* find a device that is not in use. */
		retval = -1;
		for (major = 0 ; major < UNIX98_NR_MAJORS ; major++) {
			driver = &ptm_driver[major];
			for (minor = driver->minor_start;
			     minor < driver->minor_start + driver->num;
			     minor++) {
				device = mk_kdev(driver->major, minor);
				if (!init_dev(device, &tty))
					goto ptmx_found; /* ok! */
			}
		}
		return -EIO; /* no free ptys */

	ptmx_found:
		set_bit(TTY_PTY_LOCK, &tty->flags); /* LOCK THE SLAVE */
		minor -= driver->minor_start;
		devpts_pty_new(driver->other->name_base + minor, MKDEV(driver->other->major, minor + driver->other->minor_start));
		noctty = 1;
#else
		return -ENODEV;
#endif  /* CONFIG_UNIX_98_PTYS */
	} else {
		retval = init_dev(device, &tty);
		if (retval)
			return retval;
	}

	filp->private_data = tty;
	file_move(filp, &tty->tty_files);
	check_tty_count(tty, "tty_open");
	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_MASTER)
		noctty = 1;
#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "opening %s...", tty_name(tty, buf));
#endif
	if (tty->driver.open)
		retval = tty->driver.open(tty, filp);
	else
		retval = -ENODEV;
	filp->f_flags = saved_flags;

	if (!retval && test_bit(TTY_EXCLUSIVE, &tty->flags) && !capable(CAP_SYS_ADMIN))
		retval = -EBUSY;

	if (retval) {
#ifdef TTY_DEBUG_HANGUP
		printk(KERN_DEBUG "error %d in opening %s...", retval,
		       tty_name(tty, buf));
#endif

		release_dev(filp);
		if (retval != -ERESTARTSYS)
			return retval;
		if (signal_pending(current))
			return retval;
		schedule();
		/*
		 * Need to reset f_op in case a hangup happened.
		 */
		filp->f_op = &tty_fops;
		goto retry_open;
	}
	if (!noctty &&
	    current->leader &&
	    !current->tty &&
	    tty->session == 0) {
	    	task_lock(current);
		current->tty = tty;
		task_unlock(current);
		current->tty_old_pgrp = 0;
		tty->session = current->session;
		tty->pgrp = current->pgrp;
	}
	if ((tty->driver.type == TTY_DRIVER_TYPE_SERIAL) &&
	    (tty->driver.subtype == SERIAL_TYPE_CALLOUT) &&
	    (tty->count == 1)) {
		static int nr_warns;
		if (nr_warns < 5) {
			printk(KERN_WARNING "tty_io.c: "
				"process %d (%s) used obsolete /dev/%s - "
				"update software to use /dev/ttyS%d\n",
				current->pid, current->comm,
				tty_name(tty, buf), TTY_NUMBER(tty));
			nr_warns++;
		}
	}
	return 0;
}

static int tty_release(struct inode * inode, struct file * filp)
{
	lock_kernel();
	release_dev(filp);
	unlock_kernel();
	return 0;
}

/* No kernel lock held - fine */
static unsigned int tty_poll(struct file * filp, poll_table * wait)
{
	struct tty_struct * tty;

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_dentry->d_inode->i_rdev, "tty_poll"))
		return 0;

	if (tty->ldisc.poll)
		return (tty->ldisc.poll)(tty, filp, wait);
	return 0;
}

static int tty_fasync(int fd, struct file * filp, int on)
{
	struct tty_struct * tty;
	int retval;

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_dentry->d_inode->i_rdev, "tty_fasync"))
		return 0;
	
	retval = fasync_helper(fd, filp, on, &tty->fasync);
	if (retval <= 0)
		return retval;

	if (on) {
		if (!waitqueue_active(&tty->read_wait))
			tty->minimum_to_wake = 1;
		retval = f_setown(filp, (-tty->pgrp) ? : current->pid, 0);
		if (retval)
			return retval;
	} else {
		if (!tty->fasync && !waitqueue_active(&tty->read_wait))
			tty->minimum_to_wake = N_TTY_BUF_SIZE;
	}
	return 0;
}

static int tiocsti(struct tty_struct *tty, char * arg)
{
	char ch, mbz = 0;

	if ((current->tty != tty) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (get_user(ch, arg))
		return -EFAULT;
	tty->ldisc.receive_buf(tty, &ch, &mbz, 1);
	return 0;
}

static int tiocgwinsz(struct tty_struct *tty, struct winsize * arg)
{
	if (copy_to_user(arg, &tty->winsize, sizeof(*arg)))
		return -EFAULT;
	return 0;
}

static int tiocswinsz(struct tty_struct *tty, struct tty_struct *real_tty,
	struct winsize * arg)
{
	struct winsize tmp_ws;

	if (copy_from_user(&tmp_ws, arg, sizeof(*arg)))
		return -EFAULT;
	if (!memcmp(&tmp_ws, &tty->winsize, sizeof(*arg)))
		return 0;
#ifdef CONFIG_VT
	if (tty->driver.type == TTY_DRIVER_TYPE_CONSOLE) {
		unsigned int currcons = minor(tty->device) - tty->driver.minor_start;
		if (vc_resize(currcons, tmp_ws.ws_col, tmp_ws.ws_row))
			return -ENXIO;
	}
#endif
	if (tty->pgrp > 0)
		kill_pg(tty->pgrp, SIGWINCH, 1);
	if ((real_tty->pgrp != tty->pgrp) && (real_tty->pgrp > 0))
		kill_pg(real_tty->pgrp, SIGWINCH, 1);
	tty->winsize = tmp_ws;
	real_tty->winsize = tmp_ws;
	return 0;
}

static int tioccons(struct inode *inode,
	struct tty_struct *tty, struct tty_struct *real_tty)
{
	if (IS_SYSCONS_DEV(inode->i_rdev) ||
	    IS_CONSOLE_DEV(inode->i_rdev)) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		redirect = NULL;
		return 0;
	}
	if (redirect)
		return -EBUSY;
	redirect = real_tty;
	return 0;
}


static int fionbio(struct file *file, int *arg)
{
	int nonblock;

	if (get_user(nonblock, arg))
		return -EFAULT;

	if (nonblock)
		file->f_flags |= O_NONBLOCK;
	else
		file->f_flags &= ~O_NONBLOCK;
	return 0;
}

static int tiocsctty(struct tty_struct *tty, int arg)
{
	struct list_head *l;
	struct pid *pid;
	task_t *p;

	if (current->leader &&
	    (current->session == tty->session))
		return 0;
	/*
	 * The process must be a session leader and
	 * not have a controlling tty already.
	 */
	if (!current->leader || current->tty)
		return -EPERM;
	if (tty->session > 0) {
		/*
		 * This tty is already the controlling
		 * tty for another session group!
		 */
		if ((arg == 1) && capable(CAP_SYS_ADMIN)) {
			/*
			 * Steal it away
			 */

			read_lock(&tasklist_lock);
			for_each_task_pid(tty->session, PIDTYPE_SID, p, l, pid)
				p->tty = NULL;
			read_unlock(&tasklist_lock);
		} else
			return -EPERM;
	}
	task_lock(current);
	current->tty = tty;
	task_unlock(current);
	current->tty_old_pgrp = 0;
	tty->session = current->session;
	tty->pgrp = current->pgrp;
	return 0;
}

static int tiocgpgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t *arg)
{
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	 */
	if (tty == real_tty && current->tty != real_tty)
		return -ENOTTY;
	return put_user(real_tty->pgrp, arg);
}

static int tiocspgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t *arg)
{
	pid_t pgrp;
	int retval = tty_check_change(real_tty);

	if (retval == -EIO)
		return -ENOTTY;
	if (retval)
		return retval;
	if (!current->tty ||
	    (current->tty != real_tty) ||
	    (real_tty->session != current->session))
		return -ENOTTY;
	if (get_user(pgrp, (pid_t *) arg))
		return -EFAULT;
	if (pgrp < 0)
		return -EINVAL;
	if (session_of_pgrp(pgrp) != current->session)
		return -EPERM;
	real_tty->pgrp = pgrp;
	return 0;
}

static int tiocgsid(struct tty_struct *tty, struct tty_struct *real_tty, pid_t *arg)
{
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	*/
	if (tty == real_tty && current->tty != real_tty)
		return -ENOTTY;
	if (real_tty->session <= 0)
		return -ENOTTY;
	return put_user(real_tty->session, arg);
}

static int tiocsetd(struct tty_struct *tty, int *arg)
{
	int ldisc;

	if (get_user(ldisc, arg))
		return -EFAULT;
	return tty_set_ldisc(tty, ldisc);
}

static int send_break(struct tty_struct *tty, int duration)
{
	set_current_state(TASK_INTERRUPTIBLE);

	tty->driver.break_ctl(tty, -1);
	if (!signal_pending(current))
		schedule_timeout(duration);
	tty->driver.break_ctl(tty, 0);
	if (signal_pending(current))
		return -EINTR;
	return 0;
}

static int
tty_tiocmget(struct tty_struct *tty, struct file *file, unsigned long arg)
{
	int retval = -EINVAL;

	if (tty->driver.tiocmget) {
		retval = tty->driver.tiocmget(tty, file);

		if (retval >= 0)
			retval = put_user(retval, (int *)arg);
	}
	return retval;
}

static int
tty_tiocmset(struct tty_struct *tty, struct file *file, unsigned int cmd,
	     unsigned long arg)
{
	int retval = -EINVAL;

	if (tty->driver.tiocmset) {
		unsigned int set, clear, val;

		retval = get_user(val, (unsigned int *)arg);
		if (retval)
			return retval;

		set = clear = 0;
		switch (cmd) {
		case TIOCMBIS:
			set = val;
			break;
		case TIOCMBIC:
			clear = val;
			break;
		case TIOCMSET:
			set = val;
			clear = ~val;
			break;
		}

		set &= TIOCM_DTR|TIOCM_RTS|TIOCM_OUT1|TIOCM_OUT2;
		clear &= TIOCM_DTR|TIOCM_RTS|TIOCM_OUT1|TIOCM_OUT2;

		retval = tty->driver.tiocmset(tty, file, set, clear);
	}
	return retval;
}

/*
 * Split this up, as gcc can choke on it otherwise..
 */
int tty_ioctl(struct inode * inode, struct file * file,
	      unsigned int cmd, unsigned long arg)
{
	struct tty_struct *tty, *real_tty;
	int retval;
	
	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_ioctl"))
		return -EINVAL;

	real_tty = tty;
	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_MASTER)
		real_tty = tty->link;

	/*
	 * Break handling by driver
	 */
	if (!tty->driver.break_ctl) {
		switch(cmd) {
		case TIOCSBRK:
		case TIOCCBRK:
			if (tty->driver.ioctl)
				return tty->driver.ioctl(tty, file, cmd, arg);
			return -EINVAL;
			
		/* These two ioctl's always return success; even if */
		/* the driver doesn't support them. */
		case TCSBRK:
		case TCSBRKP:
			if (!tty->driver.ioctl)
				return 0;
			retval = tty->driver.ioctl(tty, file, cmd, arg);
			if (retval == -ENOIOCTLCMD)
				retval = 0;
			return retval;
		}
	}

	/*
	 * Factor out some common prep work
	 */
	switch (cmd) {
	case TIOCSETD:
	case TIOCSBRK:
	case TIOCCBRK:
	case TCSBRK:
	case TCSBRKP:			
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		if (cmd != TIOCCBRK) {
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
		}
		break;
	}

	switch (cmd) {
		case TIOCSTI:
			return tiocsti(tty, (char *)arg);
		case TIOCGWINSZ:
			return tiocgwinsz(tty, (struct winsize *) arg);
		case TIOCSWINSZ:
			return tiocswinsz(tty, real_tty, (struct winsize *) arg);
		case TIOCCONS:
			return tioccons(inode, tty, real_tty);
		case FIONBIO:
			return fionbio(file, (int *) arg);
		case TIOCEXCL:
			set_bit(TTY_EXCLUSIVE, &tty->flags);
			return 0;
		case TIOCNXCL:
			clear_bit(TTY_EXCLUSIVE, &tty->flags);
			return 0;
		case TIOCNOTTY:
			if (current->tty != tty)
				return -ENOTTY;
			if (current->leader)
				disassociate_ctty(0);
			task_lock(current);
			current->tty = NULL;
			task_unlock(current);
			return 0;
		case TIOCSCTTY:
			return tiocsctty(tty, arg);
		case TIOCGPGRP:
			return tiocgpgrp(tty, real_tty, (pid_t *) arg);
		case TIOCSPGRP:
			return tiocspgrp(tty, real_tty, (pid_t *) arg);
		case TIOCGSID:
			return tiocgsid(tty, real_tty, (pid_t *) arg);
		case TIOCGETD:
			return put_user(tty->ldisc.num, (int *) arg);
		case TIOCSETD:
			return tiocsetd(tty, (int *) arg);
#ifdef CONFIG_VT
		case TIOCLINUX:
			return tioclinux(tty, arg);
#endif
		/*
		 * Break handling
		 */
		case TIOCSBRK:	/* Turn break on, unconditionally */
			tty->driver.break_ctl(tty, -1);
			return 0;
			
		case TIOCCBRK:	/* Turn break off, unconditionally */
			tty->driver.break_ctl(tty, 0);
			return 0;
		case TCSBRK:   /* SVID version: non-zero arg --> no break */
			/*
			 * XXX is the above comment correct, or the
			 * code below correct?  Is this ioctl used at
			 * all by anyone?
			 */
			if (!arg)
				return send_break(tty, HZ/4);
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */	
			return send_break(tty, arg ? arg*(HZ/10) : HZ/4);

		case TIOCMGET:
			return tty_tiocmget(tty, file, arg);

		case TIOCMSET:
		case TIOCMBIC:
		case TIOCMBIS:
			return tty_tiocmset(tty, file, cmd, arg);
	}
	if (tty->driver.ioctl) {
		int retval = (tty->driver.ioctl)(tty, file, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}
	if (tty->ldisc.ioctl) {
		int retval = (tty->ldisc.ioctl)(tty, file, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}
	return -EINVAL;
}


/*
 * This implements the "Secure Attention Key" ---  the idea is to
 * prevent trojan horses by killing all processes associated with this
 * tty when the user hits the "Secure Attention Key".  Required for
 * super-paranoid applications --- see the Orange Book for more details.
 * 
 * This code could be nicer; ideally it should send a HUP, wait a few
 * seconds, then send a INT, and then a KILL signal.  But you then
 * have to coordinate with the init process, since all processes associated
 * with the current tty must be dead before the new getty is allowed
 * to spawn.
 *
 * Now, if it would be correct ;-/ The current code has a nasty hole -
 * it doesn't catch files in flight. We may send the descriptor to ourselves
 * via AF_UNIX socket, close it and later fetch from socket. FIXME.
 *
 * Nasty bug: do_SAK is being called in interrupt context.  This can
 * deadlock.  We punt it up to process context.  AKPM - 16Mar2001
 */
static void __do_SAK(void *arg)
{
#ifdef TTY_SOFT_SAK
	tty_hangup(tty);
#else
	struct tty_struct *tty = arg;
	struct task_struct *p;
	struct list_head *l;
	struct pid *pid;
	int session;
	int		i;
	struct file	*filp;
	
	if (!tty)
		return;
	session  = tty->session;
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	read_lock(&tasklist_lock);
	for_each_task_pid(session, PIDTYPE_SID, p, l, pid) {
		if (p->tty == tty || session > 0) {
			printk(KERN_NOTICE "SAK: killed process %d"
			    " (%s): p->session==tty->session\n",
			    p->pid, p->comm);
			send_sig(SIGKILL, p, 1);
			continue;
		}
		task_lock(p);
		if (p->files) {
			spin_lock(&p->files->file_lock);
			for (i=0; i < p->files->max_fds; i++) {
				filp = fcheck_files(p->files, i);
				if (filp && (filp->f_op == &tty_fops) &&
				    (filp->private_data == tty)) {
					printk(KERN_NOTICE "SAK: killed process %d"
					    " (%s): fd#%d opened to the tty\n",
					    p->pid, p->comm, i);
					send_sig(SIGKILL, p, 1);
					break;
				}
			}
			spin_unlock(&p->files->file_lock);
		}
		task_unlock(p);
	}
	read_unlock(&tasklist_lock);
#endif
}

/*
 * The tq handling here is a little racy - tty->SAK_work may already be queued.
 * Fortunately we don't need to worry, because if ->SAK_work is already queued,
 * the values which we write to it will be identical to the values which it
 * already has. --akpm
 */
void do_SAK(struct tty_struct *tty)
{
	if (!tty)
		return;
	PREPARE_WORK(&tty->SAK_work, __do_SAK, tty);
	schedule_work(&tty->SAK_work);
}

EXPORT_SYMBOL(do_SAK);

/*
 * This routine is called out of the software interrupt to flush data
 * from the flip buffer to the line discipline.
 */
static void flush_to_ldisc(void *private_)
{
	struct tty_struct *tty = (struct tty_struct *) private_;
	unsigned char	*cp;
	char		*fp;
	int		count;
	unsigned long flags;

	if (test_bit(TTY_DONT_FLIP, &tty->flags)) {
		/*
		 * Do it after the next timer tick:
		 */
		schedule_delayed_work(&tty->flip.work, 1);
		return;
	}

	spin_lock_irqsave(&tty->read_lock, flags);
	if (tty->flip.buf_num) {
		cp = tty->flip.char_buf + TTY_FLIPBUF_SIZE;
		fp = tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
		tty->flip.buf_num = 0;
		tty->flip.char_buf_ptr = tty->flip.char_buf;
		tty->flip.flag_buf_ptr = tty->flip.flag_buf;
	} else {
		cp = tty->flip.char_buf;
		fp = tty->flip.flag_buf;
		tty->flip.buf_num = 1;
		tty->flip.char_buf_ptr = tty->flip.char_buf + TTY_FLIPBUF_SIZE;
		tty->flip.flag_buf_ptr = tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
	}
	count = tty->flip.count;
	tty->flip.count = 0;
	spin_unlock_irqrestore(&tty->read_lock, flags);

	tty->ldisc.receive_buf(tty, cp, fp, count);
}

/*
 * Routine which returns the baud rate of the tty
 *
 * Note that the baud_table needs to be kept in sync with the
 * include/asm/termbits.h file.
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800,
#ifdef __sparc__
	76800, 153600, 307200, 614400, 921600
#else
	500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
	2500000, 3000000, 3500000, 4000000
#endif
};

static int n_baud_table = ARRAY_SIZE(baud_table);

int tty_termios_baud_rate(struct termios *termios)
{
	unsigned int cbaud = termios->c_cflag & CBAUD;

	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~CBAUDEX;
		else
			cbaud += 15;
	}

	return baud_table[cbaud];
}

EXPORT_SYMBOL(tty_termios_baud_rate);

int tty_get_baud_rate(struct tty_struct *tty)
{
	int baud = tty_termios_baud_rate(tty->termios);

	if (baud == 38400 && tty->alt_speed) {
		if (!tty->warned) {
			printk(KERN_WARNING "Use of setserial/setrocket to "
					    "set SPD_* flags is deprecated\n");
			tty->warned = 1;
		}
		baud = tty->alt_speed;
	}
	
	return baud;
}

EXPORT_SYMBOL(tty_get_baud_rate);

void tty_flip_buffer_push(struct tty_struct *tty)
{
	if (tty->low_latency)
		flush_to_ldisc((void *) tty);
	else
		schedule_delayed_work(&tty->flip.work, 1);
}

/*
 * This subroutine initializes a tty structure.
 */
static void initialize_tty_struct(struct tty_struct *tty)
{
	memset(tty, 0, sizeof(struct tty_struct));
	tty->magic = TTY_MAGIC;
	tty->ldisc = ldiscs[N_TTY];
	tty->pgrp = -1;
	tty->flip.char_buf_ptr = tty->flip.char_buf;
	tty->flip.flag_buf_ptr = tty->flip.flag_buf;
	INIT_WORK(&tty->flip.work, flush_to_ldisc, tty);
	init_MUTEX(&tty->flip.pty_sem);
	init_waitqueue_head(&tty->write_wait);
	init_waitqueue_head(&tty->read_wait);
	INIT_WORK(&tty->hangup_work, do_tty_hangup, tty);
	sema_init(&tty->atomic_read, 1);
	sema_init(&tty->atomic_write, 1);
	spin_lock_init(&tty->read_lock);
	INIT_LIST_HEAD(&tty->tty_files);
	INIT_WORK(&tty->SAK_work, NULL, NULL);
}

/*
 * The default put_char routine if the driver did not define one.
 */
static void tty_default_put_char(struct tty_struct *tty, unsigned char ch)
{
	tty->driver.write(tty, 0, &ch, 1);
}

#ifdef CONFIG_DEVFS_FS
static void tty_register_devfs(struct tty_driver *driver, unsigned minor)
{
	umode_t mode = S_IFCHR | S_IRUSR | S_IWUSR;
	kdev_t dev = mk_kdev(driver->major, minor);
	int idx = minor - driver->minor_start;
	char buf[32];

	if ((minor < driver->minor_start) || 
	    (minor >= driver->minor_start + driver->num)) {
		printk(KERN_ERR "Attempt to register invalid minor number "
		       "with devfs (%d:%d).\n", (int)driver->major,(int)minor);
		return;
	}

	if (IS_TTY_DEV(dev) || IS_PTMX_DEV(dev)) 
		mode |= S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

	sprintf(buf, driver->name, idx + driver->name_base);
	devfs_register(NULL, buf, 0, driver->major, minor, mode,
		       &tty_fops, NULL);
}

static void tty_unregister_devfs(struct tty_driver *driver, unsigned minor)
{
	devfs_remove(driver->name,
		     minor - driver->minor_start + driver->name_base);
}
#else
# define tty_register_devfs(driver, minor)	do { } while (0)
# define tty_unregister_devfs(driver, minor)	do { } while (0)
#endif /* CONFIG_DEVFS_FS */

/*
 * Register a tty device described by <driver>, with minor number <minor>.
 */
void tty_register_device(struct tty_driver *driver, unsigned minor)
{
	tty_register_devfs(driver, minor);
}

void tty_unregister_device(struct tty_driver *driver, unsigned minor)
{
	tty_unregister_devfs(driver, minor);
}

EXPORT_SYMBOL(tty_register_device);
EXPORT_SYMBOL(tty_unregister_device);

/*
 * Called by a tty driver to register itself.
 */
int tty_register_driver(struct tty_driver *driver)
{
	int error;
        int i;

	if (driver->flags & TTY_DRIVER_INSTALLED)
		return 0;

	error = register_chrdev_region(driver->major, driver->minor_start,
				       driver->num, driver->name, &tty_fops);
	if (error < 0)
		return error;
	else if(driver->major == 0)
		driver->major = error;

	if (!driver->put_char)
		driver->put_char = tty_default_put_char;
	
	list_add(&driver->tty_drivers, &tty_drivers);
	
	if ( !(driver->flags & TTY_DRIVER_NO_DEVFS) ) {
		for(i = 0; i < driver->num; i++)
		    tty_register_device(driver, driver->minor_start + i);
	}
	proc_tty_register_driver(driver);
	return error;
}

/*
 * Called by a tty driver to unregister itself.
 */
int tty_unregister_driver(struct tty_driver *driver)
{
	int retval, i;
	struct termios *tp;

	if (*driver->refcount)
		return -EBUSY;

	retval = unregister_chrdev_region(driver->major, driver->minor_start,
					  driver->num, driver->name);
	if (retval)
		return retval;

	list_del(&driver->tty_drivers);

	/*
	 * Free the termios and termios_locked structures because
	 * we don't want to get memory leaks when modular tty
	 * drivers are removed from the kernel.
	 */
	for (i = 0; i < driver->num; i++) {
		tp = driver->termios[i];
		if (tp) {
			driver->termios[i] = NULL;
			kfree(tp);
		}
		tp = driver->termios_locked[i];
		if (tp) {
			driver->termios_locked[i] = NULL;
			kfree(tp);
		}
		if (!(driver->flags & TTY_DRIVER_NO_DEVFS))
			tty_unregister_device(driver, driver->minor_start + i);
	}
	proc_tty_unregister_driver(driver);
	return 0;
}


/*
 * Initialize the console device. This is called *early*, so
 * we can't necessarily depend on lots of kernel help here.
 * Just do some early initializations, and do the complex setup
 * later.
 */
void __init console_init(void)
{
	initcall_t *call;

	/* Setup the default TTY line discipline. */
	(void) tty_register_ldisc(N_TTY, &tty_ldisc_N_TTY);

	/*
	 * set up the console device so that later boot sequences can 
	 * inform about problems etc..
	 */
#ifdef CONFIG_EARLY_PRINTK
	disable_early_printk();
#endif
#ifdef CONFIG_SERIAL_68360
 	/* This is not a console initcall. I know not what it's doing here.
	   So I haven't moved it. dwmw2 */
        rs_360_init();
#endif
	call = &__con_initcall_start;
	while (call < &__con_initcall_end) {
		(*call)();
		call++;
	}
}

static struct tty_driver dev_tty_driver, dev_syscons_driver;
#ifdef CONFIG_UNIX98_PTYS
static struct tty_driver dev_ptmx_driver;
#endif
#ifdef CONFIG_VT
static struct tty_driver dev_console_driver;
extern int vty_init(void);
#endif

struct device_class tty_devclass = {
	.name	= "tty",
};
EXPORT_SYMBOL(tty_devclass);

static int __init tty_devclass_init(void)
{
	return devclass_register(&tty_devclass);
}

postcore_initcall(tty_devclass_init);

/*
 * Ok, now we can initialize the rest of the tty devices and can count
 * on memory allocations, interrupts etc..
 */
void __init tty_init(void)
{
	/*
	 * dev_tty_driver and dev_console_driver are actually magic
	 * devices which get redirected at open time.  Nevertheless,
	 * we register them so that register_chrdev is called
	 * appropriately.
	 */
	memset(&dev_tty_driver, 0, sizeof(struct tty_driver));
	dev_tty_driver.magic = TTY_DRIVER_MAGIC;
	dev_tty_driver.owner = THIS_MODULE;
	dev_tty_driver.driver_name = "/dev/tty";
	dev_tty_driver.name = dev_tty_driver.driver_name + 5;
	dev_tty_driver.name_base = 0;
	dev_tty_driver.major = TTYAUX_MAJOR;
	dev_tty_driver.minor_start = 0;
	dev_tty_driver.num = 1;
	dev_tty_driver.type = TTY_DRIVER_TYPE_SYSTEM;
	dev_tty_driver.subtype = SYSTEM_TYPE_TTY;
	
	if (tty_register_driver(&dev_tty_driver))
		panic("Couldn't register /dev/tty driver\n");

	dev_syscons_driver = dev_tty_driver;
	dev_syscons_driver.owner = THIS_MODULE;
	dev_syscons_driver.driver_name = "/dev/console";
	dev_syscons_driver.name = dev_syscons_driver.driver_name + 5;
	dev_syscons_driver.major = TTYAUX_MAJOR;
	dev_syscons_driver.minor_start = 1;
	dev_syscons_driver.type = TTY_DRIVER_TYPE_SYSTEM;
	dev_syscons_driver.subtype = SYSTEM_TYPE_SYSCONS;

	if (tty_register_driver(&dev_syscons_driver))
		panic("Couldn't register /dev/console driver\n");

#ifdef CONFIG_UNIX98_PTYS
	dev_ptmx_driver = dev_tty_driver;
	dev_ptmx_driver.owner = THIS_MODULE;
	dev_ptmx_driver.driver_name = "/dev/ptmx";
	dev_ptmx_driver.name = dev_ptmx_driver.driver_name + 5;
	dev_ptmx_driver.major= TTYAUX_MAJOR;
	dev_ptmx_driver.minor_start = 2;
	dev_ptmx_driver.type = TTY_DRIVER_TYPE_SYSTEM;
	dev_ptmx_driver.subtype = SYSTEM_TYPE_SYSPTMX;

	if (tty_register_driver(&dev_ptmx_driver))
		panic("Couldn't register /dev/ptmx driver\n");
#endif
	
#ifdef CONFIG_VT
	dev_console_driver = dev_tty_driver;
	dev_console_driver.owner = THIS_MODULE;
	dev_console_driver.driver_name = "/dev/vc/0";
	dev_console_driver.name = dev_console_driver.driver_name + 5;
	dev_console_driver.major = TTY_MAJOR;
	dev_console_driver.type = TTY_DRIVER_TYPE_SYSTEM;
	dev_console_driver.subtype = SYSTEM_TYPE_CONSOLE;

	if (tty_register_driver(&dev_console_driver))
		panic("Couldn't register /dev/tty0 driver\n");
	vty_init();
#endif

#ifdef CONFIG_ESPSERIAL  /* init ESP before rs, so rs doesn't see the port */
	espserial_init();
#endif
#if defined(CONFIG_MVME162_SCC) || defined(CONFIG_BVME6000_SCC) || defined(CONFIG_MVME147_SCC)
	vme_scc_init();
#endif
#ifdef CONFIG_SERIAL_TX3912
	tx3912_rs_init();
#endif
#ifdef CONFIG_ROCKETPORT
	rp_init();
#endif
#ifdef CONFIG_SERIAL167
	serial167_init();
#endif
#ifdef CONFIG_CYCLADES
	cy_init();
#endif
#ifdef CONFIG_STALLION
	stl_init();
#endif
#ifdef CONFIG_ISTALLION
	stli_init();
#endif
#ifdef CONFIG_DIGI
	pcxe_init();
#endif
#ifdef CONFIG_DIGIEPCA
	pc_init();
#endif
#ifdef CONFIG_SPECIALIX
	specialix_init();
#endif
#if (defined(CONFIG_8xx) || defined(CONFIG_8260))
	rs_8xx_init();
#endif /* CONFIG_8xx */
	pty_init();
#ifdef CONFIG_MOXA_INTELLIO
	moxa_init();
#endif	
#ifdef CONFIG_TN3270
	tub3270_init();
#endif
#ifdef CONFIG_SCLP_TTY
	sclp_tty_init();
#endif
#ifdef CONFIG_A2232
	a2232board_init();
#endif
}

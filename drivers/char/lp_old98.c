/*
 *	linux/drivers/char/lp_old98.c
 *
 * printer port driver for ancient PC-9800s with no bidirectional port support
 *
 * Copyright (C)  1998,99  Kousuke Takai <tak@kmc.kyoto-u.ac.jp>,
 *			   Kyoto University Microcomputer Club
 *
 * This driver is based on and has compatibility with `lp.c',
 * generic PC printer port driver.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/fs.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/lp.h>

/*
 *  I/O port numbers
 */
#define	LP_PORT_DATA	0x40
#define	LP_PORT_STATUS	(LP_PORT_DATA + 2)
#define	LP_PORT_STROBE	(LP_PORT_DATA + 4)
#define LP_PORT_CONTROL	(LP_PORT_DATA + 6)

#define	LP_PORT_H98MODE	0x0448
#define	LP_PORT_EXTMODE	0x0149

/*
 *  bit mask for I/O
 */
#define	LP_MASK_nBUSY	(1 << 2)
#define	LP_MASK_nSTROBE	(1 << 7)

#define LP_CONTROL_ASSERT_STROBE	(0x0e)
#define LP_CONTROL_NEGATE_STROBE	(0x0f)

/*
 *  Acceptable maximum value for non-privileged user for LPCHARS ioctl.
 */
#define LP_CHARS_NOPRIV_MAX	65535

#define	DC1	'\x11'
#define	DC3	'\x13'

/* PC-9800s have at least and at most one old-style printer port. */
static struct lp_struct lp = {
	.flags	= LP_EXIST | LP_ABORTOPEN,
	.chars	= LP_INIT_CHAR,
	.time	= LP_INIT_TIME,
	.wait	= LP_INIT_WAIT,
};

static	int	dc1_check;
static spinlock_t lp_old98_lock = SPIN_LOCK_UNLOCKED;


#undef LP_OLD98_DEBUG

#ifdef CONFIG_PC9800_OLDLP_CONSOLE
static struct console lp_old98_console;		/* defined later */
static short saved_console_flags;
#endif

static DECLARE_WAIT_QUEUE_HEAD (lp_old98_waitq);

static void lp_old98_timer_function(unsigned long data)
{
	if (inb(LP_PORT_STATUS) & LP_MASK_nBUSY)
		wake_up_interruptible(&lp_old98_waitq);
	else {
		struct timer_list *t = (struct timer_list *) data;

		t->expires = jiffies + 1;
		add_timer(t);
	}
}

static inline int lp_old98_wait_ready(void)
{
	struct timer_list timer;

	init_timer(&timer);
	timer.function = lp_old98_timer_function;
	timer.expires = jiffies + 1;
	timer.data = (unsigned long)&timer;
	add_timer(&timer);
	interruptible_sleep_on(&lp_old98_waitq);
	del_timer(&timer);
	return signal_pending(current);
}

static inline int lp_old98_char(char lpchar)
{
	unsigned long count = 0;
#ifdef LP_STATS
	int tmp;
#endif

	while (!(inb(LP_PORT_STATUS) & LP_MASK_nBUSY)) {
		count++;
		if (count >= lp.chars)
			return 0;
	}

	outb(lpchar, LP_PORT_DATA);

#ifdef LP_STATS
	/*
	 *  Update lp statsistics here (and between next two outb()'s).
	 *  Time to compute it is part of storobe delay.
	 */
	if (count > lp.stats.maxwait) {
#ifdef LP_OLD98_DEBUG
		printk(KERN_DEBUG "lp_old98: success after %d counts.\n",
		       count);
#endif
		lp.stats.maxwait = count;
	}
	count *= 256;
	tmp = count - lp.stats.meanwait;
	if (tmp < 0)
		tmp = -tmp;
#endif
	ndelay(lp.wait);
    
	/* negate PSTB# (activate strobe)	*/
	outb(LP_CONTROL_ASSERT_STROBE, LP_PORT_CONTROL);

#ifdef LP_STATS
	lp.stats.meanwait = (255 * lp.stats.meanwait + count + 128) / 256;
	lp.stats.mdev = (127 * lp.stats.mdev + tmp + 64) / 128;
	lp.stats.chars ++;
#endif

	ndelay(lp.wait);

	/* assert PSTB# (deactivate strobe)	*/
	outb(LP_CONTROL_NEGATE_STROBE, LP_PORT_CONTROL);

	return 1;
}

static ssize_t lp_old98_write(struct file * file,
			      const char * buf, size_t count,
			      loff_t *dummy)
{
	unsigned long total_bytes_written = 0;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

#ifdef LP_STATS
	if (jiffies - lp.lastcall > lp.time)
		lp.runchars = 0;
	lp.lastcall = jiffies;
#endif

	do {
		unsigned long bytes_written = 0;
		unsigned long copy_size
			= (count < LP_BUFFER_SIZE ? count : LP_BUFFER_SIZE);

		if (__copy_from_user(lp.lp_buffer, buf, copy_size))
			return -EFAULT;

		while (bytes_written < copy_size) {
			if (lp_old98_char(lp.lp_buffer[bytes_written]))
				bytes_written ++;
			else {
#ifdef LP_STATS
				int rc = lp.runchars + bytes_written;

				if (rc > lp.stats.maxrun)
					lp.stats.maxrun = rc;

				lp.stats.sleeps ++;
#endif
#ifdef LP_OLD98_DEBUG
				printk(KERN_DEBUG
				       "lp_old98: sleeping at %d characters"
				       " for %d jiffies\n",
				       lp.runchars, lp.time);
				lp.runchars = 0;
#endif
				if (lp_old98_wait_ready())
					return ((total_bytes_written
						 + bytes_written)
						? : -EINTR);
			}
		}
		total_bytes_written += bytes_written;
		buf += bytes_written;
#ifdef LP_STATS
		lp.runchars += bytes_written;
#endif
		count -= bytes_written;
	} while (count > 0);

	return total_bytes_written;
}

static int lp_old98_open(struct inode * inode, struct file * file)
{
	if (iminor(inode) != 0)
		return -ENXIO;

	if (lp.flags & LP_BUSY)
		return -EBUSY;

	if (dc1_check && (lp.flags & LP_ABORTOPEN)
	    && !(file->f_flags & O_NONBLOCK)) {
		/*
		 *  Check whether printer is on-line.
		 *  PC-9800's old style port have only BUSY# as status input,
		 *  so that it is impossible to distinguish that the printer is
		 *  ready and that the printer is off-line or not connected
		 *  (in both case BUSY# is in the same state). So:
		 *
		 *    (1) output DC1 (0x11) to printer port and do strobe.
		 *    (2) watch BUSY# line for a while. If BUSY# is pulled
		 *	  down, the printer will be ready. Otherwise,
		 *	  it will be off-line (or not connected, or power-off,
		 *	   ...).
		 *
		 *  The source of this procedure:
		 *	Terumasa KODAKA, Kazufumi SHIMIZU, Yu HAYAMI:
		 *		`PC-9801 Super Technique', Ascii, 1992.
		 */
		int count;
		unsigned long flags;

		/* interrupts while check is fairly bad */
		spin_lock_irqsave(&lp_old98_lock, flags);

		if (!lp_old98_char(DC1)) {
			spin_unlock_irqrestore(&lp_old98_lock, flags);
			return -EBUSY;
		}
		count = (unsigned int)dc1_check > 10000 ? 10000 : dc1_check;
		while (inb(LP_PORT_STATUS) & LP_MASK_nBUSY) {
			if (--count == 0) {
				spin_unlock_irqrestore(&lp_old98_lock, flags);
				return -ENODEV;
			}
		}
		spin_unlock_irqrestore(&lp_old98_lock, flags);
	}

	if ((lp.lp_buffer = kmalloc(LP_BUFFER_SIZE, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	lp.flags |= LP_BUSY;

#ifdef CONFIG_PC9800_OLDLP_CONSOLE
	saved_console_flags = lp_old98_console.flags;
	lp_old98_console.flags &= ~CON_ENABLED;
#endif
	return 0;
}

static int lp_old98_release(struct inode * inode, struct file * file)
{
	kfree(lp.lp_buffer);
	lp.lp_buffer = NULL;
	lp.flags &= ~LP_BUSY;
#ifdef CONFIG_PC9800_OLDLP_CONSOLE
	lp_old98_console.flags = saved_console_flags;
#endif
	return 0;
}

static int lp_old98_init_device(void)
{
	unsigned char data;

	if ((data = inb(LP_PORT_EXTMODE)) != 0xFF && (data & 0x10)) {
		printk(KERN_INFO
		       "lp_old98: shutting down extended parallel port mode...\n");
		outb(data & ~0x10, LP_PORT_EXTMODE);
	}
#ifdef	PC98_HW_H98
	if ((pc98_hw_flags & PC98_HW_H98)
	    && ((data = inb(LP_PORT_H98MODE)) & 0x01)) {
		printk(KERN_INFO
		       "lp_old98: shutting down H98 full centronics mode...\n");
		outb(data & ~0x01, LP_PORT_H98MODE);
	}
#endif
	return 0;
}

static int lp_old98_ioctl(struct inode *inode, struct file *file,
			  unsigned int command, unsigned long arg)
{
	int retval = 0;

	switch (command) {
	case LPTIME:
		lp.time = arg * HZ/100;
		break;
	case LPCHAR:
		lp.chars = arg;
		break;
	case LPABORT:
		if (arg)
			lp.flags |= LP_ABORT;
		else
			lp.flags &= ~LP_ABORT;
		break;
	case LPABORTOPEN:
		if (arg)
			lp.flags |= LP_ABORTOPEN;
		else
			lp.flags &= ~LP_ABORTOPEN;
		break;
	case LPCAREFUL:
		/* do nothing */
		break;
	case LPWAIT:
		lp.wait = arg;
		break;
	case LPGETIRQ:
		retval = put_user(0, (int *)arg);
		break;
	case LPGETSTATUS:
		/*
		 * convert PC-9800's status to IBM PC's one, so that tunelp(8)
		 * works in the same way on this driver.
		 */
		retval = put_user((inb(LP_PORT_STATUS) & LP_MASK_nBUSY)
					? (LP_PBUSY | LP_PERRORP) : LP_PERRORP,
					(int *)arg);
		break;
	case LPRESET:
		retval = lp_old98_init_device();
		break;
#ifdef LP_STATS
	case LPGETSTATS:
		if (copy_to_user((struct lp_stats *)arg, &lp.stats,
				 sizeof(struct lp_stats)))
			retval = -EFAULT;
		else if (suser())
			memset(&lp.stats, 0, sizeof(struct lp_stats));
		break;
#endif
	case LPGETFLAGS:
		retval = put_user(lp.flags, (int *)arg);
		break;
	case LPSETIRQ: 
	default:
		retval = -EINVAL;
	}
	return retval;
}

static struct file_operations lp_old98_fops = {
	.owner		= THIS_MODULE,
	.write		= lp_old98_write,
	.ioctl		= lp_old98_ioctl,
	.open		= lp_old98_open,
	.release	= lp_old98_release,
};

/*
 *  Support for console on lp_old98
 */
#ifdef CONFIG_PC9800_OLDLP_CONSOLE

static inline void io_delay(void)
{
	unsigned char dummy;	/* actually not output */

	asm volatile ("out%B0 %0,%1" : "=a"(dummy) : "N"(0x5f));
}

static void lp_old98_console_write(struct console *console,
				    const char *s, unsigned int count)
{
	int i;
	static unsigned int timeout_run = 0;

	while (count) {
		/* wait approx 1.2 seconds */
		for (i = 2000000; !(inb(LP_PORT_STATUS) & LP_MASK_nBUSY);
								io_delay())
			if (!--i) {
				if (++timeout_run >= 10)
					/* disable forever... */
					console->flags &= ~CON_ENABLED;
				return;
			}

		timeout_run = 0;

		if (*s == '\n') {
			outb('\r', LP_PORT_DATA);
			io_delay();
			io_delay();
			outb(LP_CONTROL_ASSERT_STROBE, LP_PORT_CONTROL);
			io_delay();
			io_delay();
			outb(LP_CONTROL_NEGATE_STROBE, LP_PORT_CONTROL);
			io_delay();
			io_delay();
			for (i = 1000000;
					!(inb(LP_PORT_STATUS) & LP_MASK_nBUSY);
					io_delay())
				if (!--i)
					return;
		}

		outb(*s++, LP_PORT_DATA);
		io_delay();
		io_delay();
		outb(LP_CONTROL_ASSERT_STROBE, LP_PORT_CONTROL);
		io_delay();
		io_delay();
		outb(LP_CONTROL_NEGATE_STROBE, LP_PORT_CONTROL);
		io_delay();
		io_delay();

		--count;
	}
}

static struct console lp_old98_console = {
	.name	= "lp_old98",
	.write	= lp_old98_console_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

#endif	/* console on lp_old98 */

static int __init lp_old98_init(void)
{
	char *errmsg = "I/O ports already occupied, giving up.";

#ifdef	PC98_HW_H98
	if (pc98_hw_flags & PC98_HW_H98)
	    if (!request_region(LP_PORT_H98MODE, 1, "lp_old98")
		goto err1;
#endif
	if (!request_region(LP_PORT_DATA,   1, "lp_old98"))
		goto err2;
	if (!request_region(LP_PORT_STATUS, 1, "lp_old98"))
		goto err3;
	if (!request_region(LP_PORT_STROBE, 1, "lp_old98"))
		goto err4;
	if (!request_region(LP_PORT_EXTMODE, 1, "lp_old98"))
		goto err5;
	if (!register_chrdev(LP_MAJOR, "lp", &lp_old98_fops)) {
#ifdef CONFIG_PC9800_OLDLP_CONSOLE
		register_console(&lp_old98_console);
		printk(KERN_INFO "lp_old98: console ready\n");
#endif
		/*
		 * rest are not needed by this driver,
		 * but for locking out other printer drivers...
		 */
		lp_old98_init_device();
		return 0;
	} else
		errmsg = "unable to register device";

	release_region(LP_PORT_EXTMODE, 1);
err5:
	release_region(LP_PORT_STROBE, 1);
err4:
	release_region(LP_PORT_STATUS, 1);
err3:
	release_region(LP_PORT_DATA, 1);
err2:
#ifdef	PC98_HW_H98
	if (pc98_hw_flags & PC98_HW_H98)
	    release_region(LP_PORT_H98MODE, 1);

err1:
#endif
	printk(KERN_ERR "lp_old98: %s\n", errmsg);
	return -EBUSY;
}

static void __exit lp_old98_exit(void)
{
#ifdef CONFIG_PC9800_OLDLP_CONSOLE
	unregister_console(&lp_old98_console);
#endif
	unregister_chrdev(LP_MAJOR, "lp");

	release_region(LP_PORT_DATA,   1);
	release_region(LP_PORT_STATUS, 1);
	release_region(LP_PORT_STROBE, 1);
#ifdef	PC98_HW_H98
	if (pc98_hw_flags & PC98_HW_H98)
		release_region(LP_PORT_H98MODE, 1);
#endif
	release_region(LP_PORT_EXTMODE, 1);
}

#ifndef MODULE
static int __init lp_old98_setup(char *str)
{
        int ints[4];

        str = get_options(str, ARRAY_SIZE(ints), ints);
        if (ints[0] > 0)
		dc1_check = ints[1];
        return 1;
}
__setup("lp_old98_dc1_check=", lp_old98_setup);
#endif

MODULE_PARM(dc1_check, "i");
MODULE_AUTHOR("Kousuke Takai <tak@kmc.kyoto-u.ac.jp>");
MODULE_DESCRIPTION("PC-9800 old printer port driver");
MODULE_LICENSE("GPL");

module_init(lp_old98_init);
module_exit(lp_old98_exit);

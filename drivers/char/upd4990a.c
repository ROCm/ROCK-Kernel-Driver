/*
 * NEC PC-9800 Real Time Clock interface for Linux	
 *
 * Copyright (C) 1997-2001  Linux/98 project,
 *			    Kyoto University Microcomputer Club.
 *
 * Based on:
 *	drivers/char/rtc.c by Paul Gortmaker
 *
 * Changes:
 *  2001-02-09	Call check_region on rtc_init and do not request I/O 0033h.
 *		Call del_timer and release_region on rtc_exit. -- tak
 *  2001-07-14	Rewrite <linux/upd4990a.h> and split to <linux/upd4990a.h>
 *		and <asm-i386/upd4990a.h>.
 *		Introduce a lot of spin_lock/unlock (&rtc_lock).
 */

#define RTC98_VERSION	"1.2"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/upd4990a.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

static struct fasync_struct *rtc_async_queue;

static DECLARE_WAIT_QUEUE_HEAD(rtc_wait);

static struct timer_list rtc_uie_timer;
static u8 old_refclk;

static int rtc_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg);

static int rtc_read_proc(char *page, char **start, off_t off,
			  int count, int *eof, void *data);

/*
 *	Bits in rtc_status. (5 bits of room for future expansion)
 */

#define RTC_IS_OPEN		0x01	/* means /dev/rtc is in use	*/
#define RTC_TIMER_ON            0x02    /* not used */
#define RTC_UIE_TIMER_ON        0x04	/* UIE emulation timer is active */

/*
 * rtc_status is never changed by rtc_interrupt, and ioctl/open/close is
 * protected by the big kernel lock. However, ioctl can still disable the timer
 * in rtc_status and then with del_timer after the interrupt has read
 * rtc_status but before mod_timer is called, which would then reenable the
 * timer (but you would need to have an awful timing before you'd trip on it)
 */
static unsigned char rtc_status;	/* bitmapped status byte.	*/
static unsigned long rtc_irq_data;	/* our output to the world	*/

static const unsigned char days_in_mo[] = 
{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

extern spinlock_t rtc_lock;	/* defined in arch/i386/kernel/time.c */

static void rtc_uie_intr(unsigned long data)
{
	u8 refclk, tmp;

	/* Kernel timer does del_timer internally before calling
	   each timer entry, so this is unnecessary.
	   del_timer(&rtc_uie_timer);  */
	spin_lock(&rtc_lock);

	/* Detect rising edge of 1Hz reference clock.  */
	refclk = UPD4990A_READ_DATA();
	tmp = old_refclk & refclk;
	old_refclk = ~refclk;
	if (!(tmp & 1))
		rtc_irq_data += 0x100;

	spin_unlock(&rtc_lock);

	if (!(tmp & 1)) {
		/* Now do the rest of the actions */
		wake_up_interruptible(&rtc_wait);
		kill_fasync(&rtc_async_queue, SIGIO, POLL_IN);
	}

	rtc_uie_timer.expires = jiffies + 1;
	add_timer(&rtc_uie_timer);
}

/*
 *	Now all the various file operations that we export.
 */

static ssize_t rtc_read(struct file *file, char *buf,
			size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long data;
	ssize_t retval = 0;
	
	if (count < sizeof(unsigned long))
		return -EINVAL;

	add_wait_queue(&rtc_wait, &wait);

	set_current_state(TASK_INTERRUPTIBLE);

	do {
		/* First make it right. Then make it fast. Putting this whole
		 * block within the parentheses of a while would be too
		 * confusing. And no, xchg() is not the answer. */
		spin_lock_irq(&rtc_lock);
		data = rtc_irq_data;
		rtc_irq_data = 0;
		spin_unlock_irq(&rtc_lock);

		if (data != 0)
			break;
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
		schedule();
	} while (1);

	retval = put_user(data, (unsigned long *)buf);
	if (!retval)
		retval = sizeof(unsigned long); 
 out:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&rtc_wait, &wait);

	return retval;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	struct rtc_time wtime; 
	struct upd4990a_raw_data raw;

	switch (cmd) {
	case RTC_UIE_OFF:	/* Mask ints from RTC updates.	*/
		spin_lock_irq(&rtc_lock);
		if (rtc_status & RTC_UIE_TIMER_ON) {
			rtc_status &= ~RTC_UIE_TIMER_ON;
			del_timer(&rtc_uie_timer);
		}
		spin_unlock_irq(&rtc_lock);
		return 0;

	case RTC_UIE_ON:	/* Allow ints for RTC updates.	*/
		spin_lock_irq(&rtc_lock);
		rtc_irq_data = 0;
		if (!(rtc_status & RTC_UIE_TIMER_ON)) {
			rtc_status |= RTC_UIE_TIMER_ON;
			rtc_uie_timer.expires = jiffies + 1;
			add_timer(&rtc_uie_timer);
		}
		/* Just in case... */
		upd4990a_serial_command(UPD4990A_REGISTER_HOLD);
		old_refclk = ~UPD4990A_READ_DATA();
		spin_unlock_irq(&rtc_lock);
		return 0;

	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
		spin_lock_irq(&rtc_lock);
		upd4990a_get_time(&raw, 0);
		spin_unlock_irq(&rtc_lock);

		wtime.tm_sec	= BCD2BIN(raw.sec);
		wtime.tm_min	= BCD2BIN(raw.min);
		wtime.tm_hour	= BCD2BIN(raw.hour);
		wtime.tm_mday	= BCD2BIN(raw.mday);
		wtime.tm_mon	= raw.mon - 1; /* convert to 0-base */
		wtime.tm_wday	= raw.wday;

		/*
		 * Account for differences between how the RTC uses the values
		 * and how they are defined in a struct rtc_time;
		 */
		if ((wtime.tm_year = BCD2BIN(raw.year)) < 95)
			wtime.tm_year += 100;

		wtime.tm_isdst = 0;
		break;

	case RTC_SET_TIME:	/* Set the RTC */
	{
		int leap_yr;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&wtime, (struct rtc_time *) arg,
				    sizeof (struct rtc_time)))
			return -EFAULT;

		/* Valid year is 1995 - 2094, inclusive.  */
		if (wtime.tm_year < 95 || wtime.tm_year > 194)
			return -EINVAL;

		if (wtime.tm_mon > 11 || wtime.tm_mday == 0)
			return -EINVAL;

		/* For acceptable year domain (1995 - 2094),
		   this IS sufficient.  */
		leap_yr = !(wtime.tm_year % 4);

		if (wtime.tm_mday > (days_in_mo[wtime.tm_mon]
				     + (wtime.tm_mon == 2 && leap_yr)))
			return -EINVAL;
			
		if (wtime.tm_hour >= 24
		    || wtime.tm_min >= 60 || wtime.tm_sec >= 60)
			return -EINVAL;

		if (wtime.tm_wday > 6)
			return -EINVAL;

		raw.sec  = BIN2BCD(wtime.tm_sec);
		raw.min  = BIN2BCD(wtime.tm_min);
		raw.hour = BIN2BCD(wtime.tm_hour);
		raw.mday = BIN2BCD(wtime.tm_mday);
		raw.mon  = wtime.tm_mon + 1;
		raw.wday = wtime.tm_wday;
		raw.year = BIN2BCD(wtime.tm_year % 100);

		spin_lock_irq(&rtc_lock);
		upd4990a_set_time(&raw, 0);
		spin_unlock_irq(&rtc_lock);

		return 0;
	}
	default:
		return -EINVAL;
	}
	return copy_to_user((void *)arg, &wtime, sizeof wtime) ? -EFAULT : 0;
}

/*
 *	We enforce only one user at a time here with the open/close.
 *	Also clear the previous interrupt data on an open, and clean
 *	up things on a close.
 */

static int rtc_open(struct inode *inode, struct file *file)
{
	spin_lock_irq(&rtc_lock);

	if(rtc_status & RTC_IS_OPEN)
		goto out_busy;

	rtc_status |= RTC_IS_OPEN;

	rtc_irq_data = 0;
	spin_unlock_irq(&rtc_lock);
	return 0;

 out_busy:
	spin_unlock_irq(&rtc_lock);
	return -EBUSY;
}

static int rtc_fasync(int fd, struct file *filp, int on)
{
	return fasync_helper(fd, filp, on, &rtc_async_queue);
}

static int rtc_release(struct inode *inode, struct file *file)
{
	del_timer(&rtc_uie_timer);

	if (file->f_flags & FASYNC)
		rtc_fasync(-1, file, 0);

	rtc_irq_data = 0;

	/* No need for locking -- nobody else can do anything until this rmw is
	 * committed, and no timer is running. */
	rtc_status &= ~(RTC_IS_OPEN | RTC_UIE_TIMER_ON);
	return 0;
}

static unsigned int rtc_poll(struct file *file, poll_table *wait)
{
	unsigned long l;

	poll_wait(file, &rtc_wait, wait);

	spin_lock_irq(&rtc_lock);
	l = rtc_irq_data;
	spin_unlock_irq(&rtc_lock);

	if (l != 0)
		return POLLIN | POLLRDNORM;
	return 0;
}

/*
 *	The various file operations we support.
 */

static struct file_operations rtc_fops = {
	.owner		= THIS_MODULE,
	.read		= rtc_read,
	.poll		= rtc_poll,
	.ioctl		= rtc_ioctl,
	.open		= rtc_open,
	.release	= rtc_release,
	.fasync		= rtc_fasync,
};

static struct miscdevice rtc_dev=
{
	.minor	= RTC_MINOR,
	.name	= "rtc",
	.fops	= &rtc_fops,
};

static int __init rtc_init(void)
{
	int err = 0;

	if (!request_region(UPD4990A_IO, 1, "rtc")) {
		printk(KERN_ERR "upd4990a: could not acquire I/O port %#x\n",
			UPD4990A_IO);
		return -EBUSY;
	}

	err = misc_register(&rtc_dev);
	if (err) {
		printk(KERN_ERR "upd4990a: can't misc_register() on minor=%d\n",
			RTC_MINOR);
		release_region(UPD4990A_IO, 1);
		return err;
	}
		
#if 0
	printk(KERN_INFO "\xB6\xDA\xDD\xC0\xDE \xC4\xDE\xB9\xB2 Driver\n");  /* Calender Clock Driver */
#else
	printk(KERN_INFO
	       "Real Time Clock driver for NEC PC-9800 v" RTC98_VERSION "\n");
#endif
	create_proc_read_entry("driver/rtc", 0, NULL, rtc_read_proc, NULL);

	init_timer(&rtc_uie_timer);
	rtc_uie_timer.function = rtc_uie_intr;

	return 0;
}

module_init (rtc_init);

static void __exit rtc_exit(void)
{
	del_timer(&rtc_uie_timer);
	release_region(UPD4990A_IO, 1);
	remove_proc_entry("driver/rtc", NULL);
	misc_deregister(&rtc_dev);
}

module_exit (rtc_exit);

/*
 *	Info exported via "/proc/driver/rtc".
 */

static inline int rtc_get_status(char *buf)
{
	char *p;
	unsigned int year;
	struct upd4990a_raw_data data;

	p = buf;

	upd4990a_get_time(&data, 0);

	/*
	 * There is no way to tell if the luser has the RTC set for local
	 * time or for Universal Standard Time (GMT). Probably local though.
	 */
	if ((year = BCD2BIN(data.year) + 1900) < 1995)
		year += 100;
	p += sprintf(p,
		     "rtc_time\t: %02d:%02d:%02d\n"
		     "rtc_date\t: %04d-%02d-%02d\n",
		     BCD2BIN(data.hour), BCD2BIN(data.min),
		     BCD2BIN(data.sec),
		     year, data.mon, BCD2BIN(data.mday));

	return  p - buf;
}

static int rtc_read_proc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len = rtc_get_status(page);

	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

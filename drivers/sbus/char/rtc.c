/* $Id: rtc.c,v 1.23 2000/08/29 07:01:55 davem Exp $
 *
 * Linux/SPARC Real Time Clock Driver
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * This is a little driver that lets a user-level program access
 * the SPARC Mostek real time clock chip. It is no use unless you
 * use the modified clock utility.
 *
 * Get the modified clock utility from:
 *   ftp://vger.kernel.org/pub/linux/Sparc/userland/clock.c
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <asm/mostek.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/rtc.h>

static int rtc_busy = 0;

/* Retrieve the current date and time from the real time clock. */
void get_rtc_time(struct rtc_time *t)
{
	unsigned long regs = mstk48t02_regs;
	unsigned long flags;
	u8 tmp;

	save_flags(flags);
	cli();

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_READ;
	mostek_write(regs + MOSTEK_CREG, tmp);

	t->sec = MSTK_REG_SEC(regs);
	t->min = MSTK_REG_MIN(regs);
	t->hour = MSTK_REG_HOUR(regs);
	t->dow = MSTK_REG_DOW(regs);
	t->dom = MSTK_REG_DOM(regs);
	t->month = MSTK_REG_MONTH(regs);
	t->year = MSTK_CVT_YEAR( MSTK_REG_YEAR(regs) );

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_READ;
	mostek_write(regs + MOSTEK_CREG, tmp);
	restore_flags(flags);
}

/* Set the current date and time inthe real time clock. */
void set_rtc_time(struct rtc_time *t)
{
	unsigned long regs = mstk48t02_regs;
	unsigned long flags;
	u8 tmp;

	save_flags(flags);
	cli();
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);

	MSTK_SET_REG_SEC(regs,t->sec);
	MSTK_SET_REG_MIN(regs,t->min);
	MSTK_SET_REG_HOUR(regs,t->hour);
	MSTK_SET_REG_DOW(regs,t->dow);
	MSTK_SET_REG_DOM(regs,t->dom);
	MSTK_SET_REG_MONTH(regs,t->month);
	MSTK_SET_REG_YEAR(regs,t->year - MSTK_YEAR_ZERO);

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);
	restore_flags(flags);
}

static long long rtc_lseek(struct file *file, long long offset, int origin)
{
	return -ESPIPE;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct rtc_time rtc_tm;

	switch (cmd)
	{
	case RTCGET:
		get_rtc_time(&rtc_tm);

		if (copy_to_user((struct rtc_time*)arg, &rtc_tm, sizeof(struct rtc_time)))
			return -EFAULT;

		return 0;


	case RTCSET:
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		if (copy_from_user(&rtc_tm, (struct rtc_time*)arg, sizeof(struct rtc_time)))
			return -EFAULT;

		set_rtc_time(&rtc_tm);

		return 0;

	default:
		return -EINVAL;
	}
}

static int rtc_open(struct inode *inode, struct file *file)
{

	if (rtc_busy)
		return -EBUSY;

	rtc_busy = 1;

	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	lock_kernel();
	rtc_busy = 0;
	unlock_kernel();
	return 0;
}

static struct file_operations rtc_fops = {
	owner:		THIS_MODULE,
	llseek:		rtc_lseek,
	ioctl:		rtc_ioctl,
	open:		rtc_open,
	release:	rtc_release,
};

static struct miscdevice rtc_dev = { RTC_MINOR, "rtc", &rtc_fops };

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
int __init rtc_sun_init(void)
#endif
{
	int error;

	if (mstk48t02_regs == 0) {
		/* This diagnostic is a debugging aid... But a useful one. */
		printk(KERN_ERR "rtc: no Mostek in this computer\n");
		return -ENODEV;
	}

	error = misc_register(&rtc_dev);
	if (error) {
		printk(KERN_ERR "rtc: unable to get misc minor for Mostek\n");
		return error;
	}

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	misc_deregister(&rtc_dev);
}
#endif

/*
 *	Real Time Clock interface for IBM iSeries	
 *
 *	Based on rtc.c by Paul Gortmaker
 *
 *	This driver allows use of the real time clock
 *	from user space. It exports the /dev/rtc
 *	interface supporting various ioctl() and also the
 *	/proc/driver/rtc pseudo-file for status information.
 *
 * 	iSeries does not support RTC interrupts nor an alarm.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *      1.0	Mike Corrigan: IBM iSeries rtc support
 */

#define RTC_VERSION		"1.0"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <asm/iSeries/mf.h>

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

static loff_t rtc_llseek(struct file *file, loff_t offset, int origin);

static ssize_t rtc_read(struct file *file, char *buf,
			size_t count, loff_t *ppos);

static int rtc_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg);

static void get_rtc_time (struct rtc_time *rtc_tm);

static int rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data);

/*
 *	If this driver ever becomes modularised, it will be really nice
 *	to make the epoch retain its value across module reload...
 */

static unsigned long epoch = 1900;	/* year corresponding to 0x00	*/

static const unsigned char days_in_mo[] = 
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 *	Now all the various file operations that we export.
 */

static loff_t rtc_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t rtc_read(struct file *file, char *buf,
			size_t count, loff_t *ppos)
{
	return -EIO;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	struct rtc_time wtime; 

	switch (cmd) {
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		get_rtc_time(&wtime);
		break;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		struct rtc_time rtc_tm;
		unsigned char mon, day, hrs, min, sec, leap_yr;
		unsigned int yrs;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&rtc_tm, (struct rtc_time*)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		yrs = rtc_tm.tm_year;
		mon = rtc_tm.tm_mon + 1;   /* tm_mon starts at zero */
		day = rtc_tm.tm_mday;
		hrs = rtc_tm.tm_hour;
		min = rtc_tm.tm_min;
		sec = rtc_tm.tm_sec;

		if (yrs < 70)
			return -EINVAL;

		leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

		if ((mon > 12) || (day == 0))
			return -EINVAL;

		if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
			return -EINVAL;
			
		if ((hrs >= 24) || (min >= 60) || (sec >= 60))
			return -EINVAL;

		if ( yrs > 169 )
			return -EINVAL;

		mf_setRtc( &rtc_tm );
		
		return 0;
	}
	case RTC_EPOCH_READ:	/* Read the epoch.	*/
	{
		return put_user (epoch, (unsigned long *)arg);
	}
	case RTC_EPOCH_SET:	/* Set the epoch.	*/
	{
		/* 
		 * There were no RTC clocks before 1900.
		 */
		if (arg < 1900)
			return -EINVAL;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		epoch = arg;
		return 0;
	}
	default:
		return -EINVAL;
	}
	return copy_to_user((void *)arg, &wtime, sizeof wtime) ? -EFAULT : 0;
}

static int rtc_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	The various file operations we support.
 */

static struct file_operations rtc_fops = {
	.owner =	THIS_MODULE,
	.llseek =	rtc_llseek,
	.read =		rtc_read,
	.ioctl =	rtc_ioctl,
	.open =		rtc_open,
	.release =	rtc_release,
};

static struct miscdevice rtc_dev=
{
	RTC_MINOR,
	"rtc",
	&rtc_fops
};

static int __init rtc_init(void)
{
	if (misc_register(&rtc_dev))
		return -ENODEV;
	create_proc_read_entry ("driver/rtc", 0, 0, rtc_read_proc, NULL);

	printk(KERN_INFO "iSeries Real Time Clock Driver v" RTC_VERSION "\n");

	return 0;
}

static void __exit rtc_exit (void)
{
	remove_proc_entry ("driver/rtc", NULL);
	misc_deregister(&rtc_dev);
}

module_init(rtc_init);
module_exit(rtc_exit);

/*
 *	Info exported via "/proc/driver/rtc".
 */

static int rtc_proc_output (char *buf)
{
	
	char *p;
	struct rtc_time tm;
	
	p = buf;

	get_rtc_time(&tm);

	/*
	 * There is no way to tell if the luser has the RTC set for local
	 * time or for Universal Standard Time (GMT). Probably local though.
	 */
	p += sprintf(p,
		     "rtc_time\t: %02d:%02d:%02d\n"
		     "rtc_date\t: %04d-%02d-%02d\n"
	 	     "rtc_epoch\t: %04lu\n",
		     tm.tm_hour, tm.tm_min, tm.tm_sec,
		     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, epoch);

	p += sprintf(p,
		     "DST_enable\t: no\n"
		     "BCD\t\t: yes\n"
		     "24hr\t\t: yes\n" );

	return  p - buf;
}

static int rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data)
{
        int len = rtc_proc_output (page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

static void get_rtc_time(struct rtc_time *rtc_tm)
{
	mf_getRtc( rtc_tm );

	rtc_tm->tm_mon--;
}



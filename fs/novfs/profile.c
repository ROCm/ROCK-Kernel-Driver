/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * This file contains a debugging code for the novfs VFS.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/time.h>

#include <linux/profile.h>
#include <linux/notifier.h>

#include "vfs.h"

/*===[ Manifest constants ]===============================================*/
#define DBGBUFFERSIZE (1024*1024*32)

/*===[ Type definitions ]=================================================*/
struct local_rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

char *DbgPrintBuffer = NULL;
char DbgPrintOn = 0;
char DbgSyslogOn = 0;
char DbgProfileOn = 0;

static unsigned long DbgPrintBufferOffset = 0;
static unsigned long DbgPrintBufferReadOffset = 0;
static unsigned long DbgPrintBufferSize = DBGBUFFERSIZE;

static struct file_operations Dbg_proc_file_operations;
static struct file_operations dentry_proc_file_ops;
static struct file_operations inode_proc_file_ops;

static struct proc_dir_entry *dbg_dir = NULL;
static struct proc_dir_entry *dbg_file = NULL;
static struct proc_dir_entry *dentry_file = NULL;
static struct proc_dir_entry *inode_file = NULL;

static DECLARE_MUTEX(LocalPrint_lock);

static ssize_t User_proc_write_DbgBuffer(struct file *file, const char __user *buf, size_t nbytes, loff_t *ppos)
{
	ssize_t retval = nbytes;
	u_char *lbuf, *p;
	int i;
	u_long cpylen;

	lbuf = kmalloc(nbytes + 1, GFP_KERNEL);
	if (lbuf) {
		cpylen = copy_from_user(lbuf, buf, nbytes);

		lbuf[nbytes] = 0;
		DbgPrint("User_proc_write_DbgBuffer: %s\n", lbuf);

		for (i = 0; lbuf[i] && lbuf[i] != '\n'; i++) ;

		if ('\n' == lbuf[i]) {
			lbuf[i] = '\0';
		}

		if (!strcmp("on", lbuf)) {
			DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
			DbgPrintOn = 1;
		} else if (!strcmp("off", lbuf)) {
			DbgPrintOn = 0;
		} else if (!strcmp("reset", lbuf)) {
			DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		} else if (NULL != (p = strchr(lbuf, ' '))) {
			*p++ = '\0';
			if (!strcmp("syslog", lbuf)) {

				if (!strcmp("on", p)) {
					DbgSyslogOn = 1;
				} else if (!strcmp("off", p)) {
					DbgSyslogOn = 0;
				}
			} else if (!strcmp("novfsd", lbuf)) {
				novfs_daemon_debug_cmd_send(p);
			} else if (!strcmp("file_update_timeout", lbuf)) {
				novfs_update_timeout =
				    simple_strtoul(p, NULL, 0);
			} else if (!strcmp("cache", lbuf)) {
				if (!strcmp("on", p)) {
					novfs_page_cache = 1;
				} else if (!strcmp("off", p)) {
					novfs_page_cache = 0;
				}
			} else if (!strcmp("profile", lbuf)) {
				if (!strcmp("on", p)) {
					DbgProfileOn = 1;
				} else if (!strcmp("off", p)) {
					DbgProfileOn = 0;
				}
			}
		}
		kfree(lbuf);
	}

	return (retval);
}

static ssize_t User_proc_read_DbgBuffer(struct file *file, char *buf, size_t nbytes, loff_t * ppos)
{
	ssize_t retval = 0;
	size_t count;

	if (0 != (count = DbgPrintBufferOffset - DbgPrintBufferReadOffset)) {

		if (count > nbytes) {
			count = nbytes;
		}

		count -=
		    copy_to_user(buf, &DbgPrintBuffer[DbgPrintBufferReadOffset],
				 count);

		if (count == 0) {
			if (retval == 0)
				retval = -EFAULT;
		} else {
			DbgPrintBufferReadOffset += count;
			if (DbgPrintBufferReadOffset >= DbgPrintBufferOffset) {
				DbgPrintBufferOffset =
				    DbgPrintBufferReadOffset = 0;
			}
			retval = count;
		}
	}

	return retval;
}

static int proc_read_DbgBuffer(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len;

	printk(KERN_ALERT "proc_read_DbgBuffer: off=%ld count=%d DbgPrintBufferOffset=%lu DbgPrintBufferReadOffset=%lu\n", off, count, DbgPrintBufferOffset, DbgPrintBufferReadOffset);

	len = DbgPrintBufferOffset - DbgPrintBufferReadOffset;

	if ((int)(DbgPrintBufferOffset - DbgPrintBufferReadOffset) > count)
		len = count;

	if (len) {
		memcpy(page, &DbgPrintBuffer[DbgPrintBufferReadOffset], len);
		DbgPrintBufferReadOffset += len;
	}

	if (DbgPrintBufferReadOffset >= DbgPrintBufferOffset)
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;

	printk(KERN_ALERT "proc_read_DbgBuffer: return %d\n", len);

	return len;
}

#define DBG_BUFFER_SIZE (2*1024)

static int LocalPrint(char *Fmt, ...)
{
	int len = 0;
	va_list args;

	if (DbgPrintBuffer) {
		va_start(args, Fmt);
		len += vsnprintf(DbgPrintBuffer + DbgPrintBufferOffset,
				 DbgPrintBufferSize - DbgPrintBufferOffset,
				 Fmt, args);
		DbgPrintBufferOffset += len;
	}

	return (len);
}

int DbgPrint(char *Fmt, ...)
{
	char *buf;
	int len = 0;
	unsigned long offset;
	va_list args;

	if ((DbgPrintBuffer && DbgPrintOn) || DbgSyslogOn) {
		buf = kmalloc(DBG_BUFFER_SIZE, GFP_KERNEL);

		if (buf) {
			va_start(args, Fmt);
			len = sprintf(buf, "[%d] ", current->pid);

			len +=
			    vsnprintf(buf + len, DBG_BUFFER_SIZE - len, Fmt,
				      args);
			if (-1 == len) {
				len = DBG_BUFFER_SIZE - 1;
				buf[len] = '\0';
			}
			/*
			   len = sprintf(&DbgPrintBuffer[offset], "[%llu] ", ts);
			   len += vsprintf(&DbgPrintBuffer[offset+len], Fmt, args);
			 */

			if (len) {
				if (DbgSyslogOn) {
					printk("<6>%s", buf);
				}

				if (DbgPrintBuffer && DbgPrintOn) {
					if ((DbgPrintBufferOffset + len) >
					    DbgPrintBufferSize) {
						offset = DbgPrintBufferOffset;
						DbgPrintBufferOffset = 0;
						memset(&DbgPrintBuffer[offset],
						       0,
						       DbgPrintBufferSize -
						       offset);
					}

					mb();

					if ((DbgPrintBufferOffset + len) <
					    DbgPrintBufferSize) {
						DbgPrintBufferOffset += len;
						offset =
						    DbgPrintBufferOffset - len;
						memcpy(&DbgPrintBuffer[offset],
						       buf, len + 1);
					}
				}
			}
			kfree(buf);
		}
	}

	return (len);
}

static void doline(unsigned char *b, unsigned char *e, unsigned char *l)
{
	unsigned char c;

	*b++ = ' ';

	while (l < e) {
		c = *l++;
		if ((c < ' ') || (c > '~')) {
			c = '.';
		}
		*b++ = c;
		*b = '\0';
	}
}

void novfs_dump(int size, void *dumpptr)
{
	unsigned char *ptr = (unsigned char *)dumpptr;
	unsigned char *line = NULL, buf[100], *bptr = buf;
	int i;

	if (DbgPrintBuffer || DbgSyslogOn) {
		if (size) {
			for (i = 0; i < size; i++) {
				if (0 == (i % 16)) {
					if (line) {
						doline(bptr, ptr, line);
						DbgPrint("%s\n", buf);
						bptr = buf;
					}
					bptr += sprintf(bptr, "0x%p: ", ptr);
					line = ptr;
				}
				bptr += sprintf(bptr, "%02x ", *ptr++);
			}
			doline(bptr, ptr, line);
			DbgPrint("%s\n", buf);
		}
	}
}

#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * This only works for the Gregorian calendar - i.e. after 1752 (in the UK)
 */
static void GregorianDay(struct local_rtc_time *tm)
{
	int leapsToDate;
	int lastYear;
	int day;
	int MonthOffset[] =
	    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	lastYear = tm->tm_year - 1;

	/*
	 * Number of leap corrections to apply up to end of last year
	 */
	leapsToDate = lastYear / 4 - lastYear / 100 + lastYear / 400;

	/*
	 * This year is a leap year if it is divisible by 4 except when it is
	 * divisible by 100 unless it is divisible by 400
	 *
	 * e.g. 1904 was a leap year, 1900 was not, 1996 is, and 2000 will be
	 */
	if ((tm->tm_year % 4 == 0) &&
	    ((tm->tm_year % 100 != 0) || (tm->tm_year % 400 == 0)) &&
	    (tm->tm_mon > 2)) {
		/*
		 * We are past Feb. 29 in a leap year
		 */
		day = 1;
	} else {
		day = 0;
	}

	day += lastYear * 365 + leapsToDate + MonthOffset[tm->tm_mon - 1] +
	    tm->tm_mday;

	tm->tm_wday = day % 7;
}

static void private_to_tm(int tim, struct local_rtc_time *tm)
{
	register int i;
	register long hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week
	 */
	GregorianDay(tm);
}

char *ctime_r(time_t * clock, char *buf)
{
	struct local_rtc_time tm;
	static char *DAYOFWEEK[] =
	    { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static char *MONTHOFYEAR[] =
	    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
"Oct", "Nov", "Dec" };

	private_to_tm(*clock, &tm);

	sprintf(buf, "%s %s %d %d:%02d:%02d %d", DAYOFWEEK[tm.tm_wday],
		MONTHOFYEAR[tm.tm_mon - 1], tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec, tm.tm_year);
	return (buf);
}

static void dump(struct dentry *parent, void *pf)
{
	void (*pfunc) (char *Fmt, ...) = pf;
	struct l {
		struct l *next;
		struct dentry *dentry;
	} *l, *n, *start;
	struct list_head *p;
	struct dentry *d;
	char *buf, *path, *sd;
	char inode_number[16];

	buf = (char *)kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);

	if (NULL == buf) {
		return;
	}

	if (parent) {
		pfunc("starting 0x%p %.*s\n", parent, parent->d_name.len,
		      parent->d_name.name);
		if (parent->d_subdirs.next == &parent->d_subdirs) {
			pfunc("No children...\n");
		} else {
			start = kmalloc(sizeof(*start), GFP_KERNEL);
			if (start) {
				start->next = NULL;
				start->dentry = parent;
				l = start;
				while (l) {
					p = l->dentry->d_subdirs.next;
					while (p != &l->dentry->d_subdirs) {
						d = list_entry(p, struct dentry,
							       d_u.d_child);
						p = p->next;

						if (d->d_subdirs.next !=
						    &d->d_subdirs) {
							n = kmalloc(sizeof
									 (*n),
									 GFP_KERNEL);
							if (n) {
								n->next =
								    l->next;
								l->next = n;
								n->dentry = d;
							}
						} else {
							path = novfs_scope_dget_path(d, buf, PATH_LENGTH_BUFFER, 1);
							if (path) {
								pfunc
								    ("1-0x%p %s\n"
								     "   d_name:    %.*s\n"
								     "   d_parent:  0x%p\n"
								     "   d_count:   %d\n"
								     "   d_flags:   0x%x\n"
								     "   d_subdirs: 0x%p\n"
								     "   d_inode:   0x%p\n",
								     d, path,
								     d->d_name.
								     len,
								     d->d_name.
								     name,
								     d->
								     d_parent,
								     atomic_read
								     (&d->
								      d_count),
								     d->d_flags,
								     d->
								     d_subdirs.
								     next,
								     d->
								     d_inode);
							}
						}
					}
					l = l->next;
				}
				l = start;
				while (l) {
					d = l->dentry;
					path =
					    novfs_scope_dget_path(d, buf,
							    PATH_LENGTH_BUFFER,
							    1);
					if (path) {
						sd = " (None)";
						if (&d->d_subdirs !=
						    d->d_subdirs.next) {
							sd = "";
						}
						inode_number[0] = '\0';
						if (d->d_inode) {
							sprintf(inode_number,
								" (%lu)",
								d->d_inode->
								i_ino);
						}
						pfunc("0x%p %s\n"
						      "   d_parent:  0x%p\n"
						      "   d_count:   %d\n"
						      "   d_flags:   0x%x\n"
						      "   d_subdirs: 0x%p%s\n"
						      "   d_inode:   0x%p%s\n",
						      d, path, d->d_parent,
						      atomic_read(&d->d_count),
						      d->d_flags,
						      d->d_subdirs.next, sd,
						      d->d_inode, inode_number);
					}

					n = l;
					l = l->next;
					kfree(n);
				}
			}
		}
	}

	kfree(buf);

}

static ssize_t common_read(char *buf, size_t len, loff_t * off)
{
	ssize_t retval = 0;
	size_t count;
	unsigned long offset = *off;

	if (0 != (count = DbgPrintBufferOffset - offset)) {
		if (count > len) {
			count = len;
		}

		count -= copy_to_user(buf, &DbgPrintBuffer[offset], count);

		if (count == 0) {
			retval = -EFAULT;
		} else {
			*off += (loff_t) count;
			retval = count;
		}
	}
	return retval;

}

static ssize_t novfs_profile_read_inode(struct file * file, char *buf, size_t len,
			   loff_t * off)
{
	ssize_t retval = 0;
	unsigned long offset = *off;
	static char save_DbgPrintOn;

	if (offset == 0) {
		down(&LocalPrint_lock);
		save_DbgPrintOn = DbgPrintOn;
		DbgPrintOn = 0;

		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		novfs_dump_inode(LocalPrint);
	}


	retval = common_read(buf, len, off);

	if (0 == retval) {
		DbgPrintOn = save_DbgPrintOn;
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;

		up(&LocalPrint_lock);
	}

	return retval;

}

static ssize_t novfs_profile_dentry_read(struct file * file, char *buf, size_t len,
				     loff_t * off)
{
	ssize_t retval = 0;
	unsigned long offset = *off;
	static char save_DbgPrintOn;

	if (offset == 0) {
		down(&LocalPrint_lock);
		save_DbgPrintOn = DbgPrintOn;
		DbgPrintOn = 0;
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		dump(novfs_root, LocalPrint);
	}

	retval = common_read(buf, len, off);

	if (0 == retval) {
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		DbgPrintOn = save_DbgPrintOn;

		up(&LocalPrint_lock);
	}

	return retval;

}

uint64_t get_nanosecond_time()
{
	struct timespec ts;
	uint64_t retVal;

	ts = current_kernel_time();

	retVal = (uint64_t) NSEC_PER_SEC;
	retVal *= (uint64_t) ts.tv_sec;
	retVal += (uint64_t) ts.tv_nsec;

	return (retVal);
}

void novfs_profile_init()
{
	if (novfs_procfs_dir)
		dbg_dir = novfs_procfs_dir;
	else
		dbg_dir = proc_mkdir(MODULE_NAME, NULL);

	if (dbg_dir) {
		dbg_dir->owner = THIS_MODULE;
		dbg_file = create_proc_read_entry("Debug",
						  0600,
						  dbg_dir,
						  proc_read_DbgBuffer, NULL);
		if (dbg_file) {
			dbg_file->owner = THIS_MODULE;
			dbg_file->size = DBGBUFFERSIZE;
			memcpy(&Dbg_proc_file_operations, dbg_file->proc_fops,
			       sizeof(struct file_operations));
			Dbg_proc_file_operations.read =
			    User_proc_read_DbgBuffer;
			Dbg_proc_file_operations.write =
			    User_proc_write_DbgBuffer;
			dbg_file->proc_fops = &Dbg_proc_file_operations;
		} else {
			remove_proc_entry(MODULE_NAME, NULL);
			vfree(DbgPrintBuffer);
			DbgPrintBuffer = NULL;
		}
	}

	if (DbgPrintBuffer) {
		if (dbg_dir) {
			inode_file = create_proc_entry("inode", 0600, dbg_dir);
			if (inode_file) {
				inode_file->owner = THIS_MODULE;
				inode_file->size = 0;
				memcpy(&inode_proc_file_ops,
				       inode_file->proc_fops,
				       sizeof(struct file_operations));
				inode_proc_file_ops.owner = THIS_MODULE;
				inode_proc_file_ops.read =
					novfs_profile_read_inode;
				inode_file->proc_fops = &inode_proc_file_ops;
			}

			dentry_file = create_proc_entry("dentry",
							0600, dbg_dir);
			if (dentry_file) {
				dentry_file->owner = THIS_MODULE;
				dentry_file->size = 0;
				memcpy(&dentry_proc_file_ops,
				       dentry_file->proc_fops,
				       sizeof(struct file_operations));
				dentry_proc_file_ops.owner = THIS_MODULE;
				dentry_proc_file_ops.read = novfs_profile_dentry_read;
				dentry_file->proc_fops = &dentry_proc_file_ops;
			}

		} else {
			vfree(DbgPrintBuffer);
			DbgPrintBuffer = NULL;
		}
	}
}

void novfs_profile_exit(void)
{
	if (dbg_file)
		DbgPrint("Calling remove_proc_entry(Debug, NULL)\n"),
		    remove_proc_entry("Debug", dbg_dir);
	if (inode_file)
		DbgPrint("Calling remove_proc_entry(inode, NULL)\n"),
		    remove_proc_entry("inode", dbg_dir);
	if (dentry_file)
		DbgPrint("Calling remove_proc_entry(dentry, NULL)\n"),
		    remove_proc_entry("dentry", dbg_dir);

	if (dbg_dir && (dbg_dir != novfs_procfs_dir)) {
		DbgPrint("Calling remove_proc_entry(%s, NULL)\n", MODULE_NAME);
		remove_proc_entry(MODULE_NAME, NULL);
	}
}



/*
 *  drivers/s390/cio/blacklist.c
 *   S/390 common I/O routines -- blacklisting of specific devices
 *   $Revision: 1.7 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *                            IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *               Cornelia Huck (cohuck@de.ibm.com) 
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *    ChangeLog: 11/04/2002 Arnd Bergmann Split s390io.c into multiple files,
 *					  see s390io.c for complete list of
 * 					  changes.
 * 		 15/04/2002 Arnd Bergmann check ranges of user input
 * 		 18/04/2002 Arnd Bergmann remove bogus optimization and
 * 		 			  now unnecessary locking
 * 		 19/04/2002 Arnd Bergmann cleanup parameter parsing
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>

#include <asm/irq.h>
#include <asm/s390dyn.h>
#include <asm/uaccess.h>
#include <asm/debug.h>

#include "blacklist.h"
#include "cio_debug.h"
#include "ioinfo.h"
#include "proc.h"
#include "s390io.h"

/* 
 * "Blacklisting" of certain devices:
 * Device numbers given in the commandline as cio_ignore=... won't be known
 * to Linux.
 *
 * These can be single devices or ranges of devices
 */

#define max_devno (0xffffUL)
static uint32_t bl_dev[(max_devno+1) / 32]; /* 65536 bits to indicate if a
					       devno is blacklisted or not */
typedef enum {add, free} range_action;

/* 
 * Function: blacklist_range
 * (Un-)blacklist the devices from-to
 */
static inline void
blacklist_range (range_action action, unsigned int from, unsigned int to)
{
	if (!to)
		to = from;

	if ((from > to) || (to > max_devno)) {
		printk (KERN_WARNING "Invalid blacklist range "
			"0x%04x to 0x%04x, skipping\n", from, to);
		return;
	}
	for (; from <= to; from++) {
		(action == add) ? set_bit (from, &bl_dev)
				: clear_bit (from, &bl_dev);
	}
}

/* 
 * function: blacklist_strtoul
 * Strip leading '0x' and interpret the values as Hex
 */
static inline int
blacklist_strtoul (const char *str, char **stra)
{
        if (*str == '0') {
                if (*(++str) == 'x')  /* strip leading zero */
                        str++;        /* strip leading x */
        }
        return simple_strtoul (str, stra, 16); /* interpret anything as hex */
}

static inline int
blacklist_parse_parameters (char *str, range_action action)
{
	unsigned int from, to;
	while (*str != 0 && *str != '\n') {
		if (!isxdigit(*str)) {
			printk(KERN_WARNING "blacklist_setup: error parsing "
					 "\"%s\"\n", str);
			return 0;
		}

		from = blacklist_strtoul (str, &str);
		to = (*str == '-') ? blacklist_strtoul (str+1, &str) : from;

		printk (KERN_INFO "blacklist_setup: adding range "
				  "from 0x%04x to 0x%04x\n", from, to);
		blacklist_range (action, from, to);

		if (*str == ',')
			str++;
	}
	return 1;
}

/* Parsing the commandline for blacklist parameters, e.g. to blacklist
 * device IDs 0x1234, 0x1235 and 0x1236, you could use any of:
 * - cio_ignore=1234-1236
 * - cio_ignore=0x1234-0x1235,1236
 * - cio_ignore=0x1234,1235-1236
 * - cio_ignore=1236 cio_ignore=1234-0x1236
 * - cio_ignore=1234 cio_ignore=1236 cio_ignore=0x1235
 * - ...
 */
static int __init
blacklist_setup (char *str)
{
	CIO_MSG_EVENT(6, "Reading blacklist parameters\n");
	return blacklist_parse_parameters (str, add);
}

__setup ("cio_ignore=", blacklist_setup);

/* Checking if devices are blacklisted */

/*
 * Function: is_blacklisted
 * Returns 1 if the given devicenumber can be found in the blacklist, otherwise 0.
 * Used by s390_validate_subchannel()
 */
int
is_blacklisted (int devno)
{
	return test_bit (devno, &bl_dev);
}

#ifdef CONFIG_PROC_FS

/*
 * Function: s390_redo_validation
 * Look for no longer blacklisted devices
 * FIXME: there must be a better way to do this */
static inline void
s390_redo_validation (void)
{
	int irq;

	CIO_TRACE_EVENT (0, "redoval");

	for (irq=0; irq <= highest_subchannel; irq++) {
		if (ioinfo[irq] != INVALID_STORAGE_AREA
		    || s390_validate_subchannel (irq, 0))
			continue;

		/* this subchannel has just been unblacklisted, 
		 * so now try to get it working */
		s390_device_recognition_irq (irq);

		if (ioinfo[irq]->ui.flags.oper) {
			devreg_t *pdevreg;
			pdevreg = s390_search_devreg (ioinfo[irq]);
			if (pdevreg && pdevreg->oper_func != NULL)
				pdevreg->oper_func (irq, pdevreg);
		}

	}
}

/*
 * Function: blacklist_parse_proc_parameters
 * parse the stuff which is piped to /proc/cio_ignore
 */
static inline void
blacklist_parse_proc_parameters (char *buf)
{
	if (strncmp (buf, "free ", 5) == 0) {
		if (strstr (buf + 5, "all"))
			blacklist_range (free, 0, max_devno);
		else
			blacklist_parse_parameters (buf + 5, free);
	} else if (strncmp (buf, "add ", 4) == 0) {
		/* FIXME: the old code was checking if the new bl'ed
		 * devices are already known to the system so
		 * s390_validate_subchannel would still give a working
		 * status. is that necessary? */
		blacklist_parse_parameters (buf + 4, add);
	} else {
		printk (KERN_WARNING "cio_ignore: Parse error; \n"
			KERN_WARNING "try using 'free all|<devno-range>,"
				     "<devno-range>,...'\n"
			KERN_WARNING "or 'add <devno-range>,"
				     "<devno-range>,...'\n");
		return;
	}

	s390_redo_validation ();
}

static int cio_ignore_read (char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len = 0;
	const unsigned int entry_size = 14; /* "0xABCD-0xEFGH\n" */
	long devno = off; /* abuse the page variable
			   * as counter, see fs/proc/generic.c */

	while ((devno <= max_devno)
	       && (len + entry_size < count)) {
		if (test_bit (devno, &bl_dev)) {
			len += sprintf(page + len, "0x%04lx", devno);
			devno++;
			if (test_bit (devno, &bl_dev)) { /* print range */
				do { devno++; } 
					while (test_bit (devno, &bl_dev));
				
				len += sprintf(page + len, "-0x%04lx", devno-1);
			}
			len += sprintf(page + len, "\n");
		}
		devno++;
	}

	if (devno <= max_devno)
		*eof = 1;

	*start = (char *) (devno - off); /* number of checked entries */

	return len;
}

static int cio_ignore_write (struct file *file, const char *user_buf,
			     unsigned long user_len, void *data)
{
	char *buf = vmalloc (user_len + 1); /* maybe better use the stack? */

	if (buf == NULL)
		return -ENOMEM;
	if (strncpy_from_user (buf, user_buf, user_len) < 0) {
		vfree (buf);
		return -EFAULT;
	}
	buf[user_len] = '\0';
#if 0
	CIO_DEBUG(KERN_DEBUG, 2, 
		  "/proc/cio_ignore: '%s'\n", buf);
#endif
	blacklist_parse_proc_parameters (buf);

	vfree (buf);
	return user_len;
}

static int
cio_ignore_proc_init (void)
{
	struct proc_dir_entry *entry;
	entry = create_proc_entry ("cio_ignore", S_IFREG | S_IRUGO | S_IWUSR,
				   &proc_root);
	if (!entry)
		return 0;

	entry->read_proc  = cio_ignore_read;
	entry->write_proc = cio_ignore_write;

	return 1;
}

__initcall (cio_ignore_proc_init);

#endif /* CONFIG_PROC_FS */

/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "debug.h"

#include <asm/page.h>
#include <linux/sched.h>
#include <linux/kernel.h>

int			doass = 1;
static char		message[256];	/* keep it off the stack */
static spinlock_t 	xfs_err_lock = SPIN_LOCK_UNLOCKED;

/* Translate from CE_FOO to KERN_FOO, err_level(CE_FOO) == KERN_FOO */
#define MAX_XFS_ERR_LEVEL	7
static char		*err_level[XFS_MAX_ERR_LEVEL+1] =
					{KERN_EMERG, KERN_ALERT, KERN_CRIT,
					 KERN_ERR, KERN_WARNING, KERN_NOTICE,
					 KERN_INFO, KERN_DEBUG};

void
assfail(char *a, char *f, int l)
{
    printk("XFS assertion failed: %s, file: %s, line: %d\n", a, f, l);
    BUG();
}

#ifdef DEBUG

unsigned long
random(void)
{
	static unsigned long	RandomValue = 1;
	/* cycles pseudo-randomly through all values between 1 and 2^31 - 2 */
	register long	rv = RandomValue;
	register long	lo;
	register long	hi;

	hi = rv / 127773;
	lo = rv % 127773;
	rv = 16807 * lo - 2836 * hi;
	if( rv <= 0 ) rv += 2147483647;
	return( RandomValue = rv );
}

int
get_thread_id(void)
{
	return current->pid;
}

#endif /* DEBUG */

void
cmn_err(register int level, char *fmt, ...)
{
	char	*fp = fmt;
	va_list ap;

	if (level > XFS_MAX_ERR_LEVEL)
		level = XFS_MAX_ERR_LEVEL;
	spin_lock(&xfs_err_lock);
	va_start(ap, fmt);
	if (*fmt == '!') fp++;
	vsprintf(message, fp, ap);
	printk("%s%s\n", err_level[level], message);
	va_end(ap);
	spin_unlock(&xfs_err_lock);

	if (level == CE_PANIC)
		BUG();
}


void
icmn_err(register int level, char *fmt, va_list ap)
{
	if (level > XFS_MAX_ERR_LEVEL)
		level = XFS_MAX_ERR_LEVEL;
	spin_lock(&xfs_err_lock);
	vsprintf(message, fmt, ap);
	spin_unlock(&xfs_err_lock);
	printk("%s%s\n", err_level[level], message);
	if (level == CE_PANIC)
		BUG();
}

/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _H_JFS_DEBUG
#define _H_JFS_DEBUG

/*
 *	jfs_debug.h
 *
 * global debug message, data structure/macro definitions
 * under control of CONFIG_JFS_DEBUG, CONFIG_JFS_STATISTICS;
 */

/*
 * Create /proc/fs/jfs if procfs is enabled andeither
 * CONFIG_JFS_DEBUG or CONFIG_JFS_STATISTICS is defined
 */
#if defined(CONFIG_PROC_FS) && (defined(CONFIG_JFS_DEBUG) || defined(CONFIG_JFS_STATISTICS))
	#define PROC_FS_JFS
#endif

/*
 *	assert with traditional printf/panic
 */
#ifdef CONFIG_KERNEL_ASSERTS
/* kgdb stuff */
#define assert(p) KERNEL_ASSERT(#p, p)
#else
#define assert(p) {\
if (!(p))\
	{\
		printk("assert(%s)\n",#p);\
		BUG();\
	}\
}
#endif

/*
 *	debug ON
 *	--------
 */
#ifdef CONFIG_JFS_DEBUG
#define ASSERT(p) assert(p)

/* dump memory contents */
extern void dump_mem(char *label, void *data, int length);
extern int jfsloglevel;

/* information message: e.g., configuration, major event */
#define jFYI(button, prspec) \
	do { if (button && jfsloglevel > 1) printk prspec; } while (0)

/* error event message: e.g., i/o error */
extern int jfsERROR;
#define jERROR(button, prspec) \
	do { if (button && jfsloglevel > 0) { printk prspec; } } while (0)

/* debug event message: */
#define jEVENT(button,prspec) \
	do { if (button) printk prspec; } while (0)

/*
 *	debug OFF
 *	---------
 */
#else				/* CONFIG_JFS_DEBUG */
#define dump_mem(label,data,length)
#define ASSERT(p)
#define jEVENT(button,prspec)
#define jERROR(button,prspec)
#define jFYI(button,prspec)
#endif				/* CONFIG_JFS_DEBUG */

/*
 *	statistics
 *	----------
 */
#ifdef	CONFIG_JFS_STATISTICS
#define	INCREMENT(x)		((x)++)
#define	DECREMENT(x)		((x)--)
#define	HIGHWATERMARK(x,y)	((x) = max((x), (y)))
#else
#define	INCREMENT(x)
#define	DECREMENT(x)
#define	HIGHWATERMARK(x,y)
#endif				/* CONFIG_JFS_STATISTICS */

#endif				/* _H_JFS_DEBUG */

/*
 * Linux Event Logging for the Enterprise
 * Copyright (C) International Business Machines Corp., 2002
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Please send e-mail to kenistoj@users.sourceforge.net if you have
 *  questions or comments.
 *
 *  Project Website:  http://evlog.sourceforge.net/
 */

#ifndef _LINUX_PRINTKAT_H
#define _LINUX_PRINTKAT_H

extern int __printkat(const char *facname, int call_printk,
	const char *fmt, ...);

#ifndef CONFIG_EVLOG

/* Just strip {id} constructs and call printk. */
#define printkat(fmt, arg...) __printkat((const char*)0, 1, fmt, ## arg)
#define printkat_noprintk(fmt, arg...) do {} while(0)

#else	/* CONFIG_EVLOG */

#include <linux/stringify.h>
#include <linux/kernel.h>
#include <linux/evlog.h>

/*
 * Facility name defaults to the name of the module, as set in the kernel
 * build, or to kern (the kernel default) if the module name is not set.
 * Define EVL_FACILITY_NAME before including this file (or redefine it
 * before calling printkat) if that's unsatisfactory.
 *
 * In a device driver, EVL_FACILITY_NAME should be the driver name (without
 * quotes).
 */
#ifndef EVL_FACILITY_NAME
#ifdef KBUILD_MODNAME
#define EVL_FACILITY_NAME KBUILD_MODNAME
#else
#define EVL_FACILITY_NAME kern
#endif
#endif

/* Bloat doesn't matter: this doesn't end up in vmlinux. */
struct log_position {
   int line;
   char function[64 - sizeof(int)];
   char file[128];
};

#define _LOG_POS { __LINE__, __FUNCTION__, __FILE__ }

/*
 * Information about a printkat() message.
 * Again, bloat doesn't matter: this doesn't end up in vmlinux.
 * Note that, because of default alignment in the .log section,
 * sizeof(struct log_info) should be a multiple of 32.
 */
struct log_info {
   char format[128+64];
   char facility[64];
   struct log_position pos;
};

#define printkat(fmt, arg...) \
({ \
   static struct log_info __attribute__((section(".log"),unused)) ___ \
      = { fmt, __stringify(EVL_FACILITY_NAME), _LOG_POS }; \
   __printkat(__stringify(EVL_FACILITY_NAME) , 1, fmt , ## arg); \
})

/* Same as printkat, but don't call printk. */
#define printkat_noprintk(fmt, arg...) \
({ \
   static struct log_info __attribute__((section(".log"),unused)) ___ \
      = { fmt, __stringify(EVL_FACILITY_NAME), _LOG_POS }; \
   __printkat(__stringify(EVL_FACILITY_NAME) , 0, fmt , ## arg); \
})

#endif /* CONFIG_EVLOG */

#endif /*_LINUX_PRINTKAT_H*/

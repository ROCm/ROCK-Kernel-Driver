/*
 * Linux Event Logging
 * Copyright (C) International Business Machines Corp., 2001
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
#ifndef _LINUX_EVL_LOG_H
#define _LINUX_EVL_LOG_H

#include <linux/evlog.h>

/*
 * This header provides the declarations and definitions for the "legacy"
 * functions defined in early releases of Event Logging.  Consider using
 * the newer functions declared in evlog.h -- e.g., evl_write(), evl_printk(),
 * evl_vprintk().
 */

/*
 * In the current implementation, the header for kernel events contains
 * the facility name, not the facility code.  The conversion to facility code
 * happens in evlogd.  The legacy functions evl_gen_facility_code() and
 * evl_register_facility() still provide a "posix_log_facility_t" that the
 * caller can pass to the other functions (i.e., a kmalloc-ed copy of
 * the facility name).  However, evl_register_facility() no longer logs a
 * "register facility" event.  To register a facility, just use the command
 * "evlfacility -a facname [-k]".
 */
typedef const char *posix_log_facility_t;
typedef int posix_log_severity_t;

#define POSIX_LOG_TRUNCATE	EVL_TRUNCATE

#define POSIX_LOG_NODATA	EVL_NODATA
#define POSIX_LOG_BINARY	EVL_BINARY
#define POSIX_LOG_STRING	EVL_STRING
#define POSIX_LOG_PRINTF	EVL_PRINTF

#define POSIX_LOG_ENTRY_MAXLEN	EVL_ENTRY_MAXLEN

#define POSIX_LOG_MEMSTR_MAXLEN	128

#define LOG_KERN "kern"

#ifdef CONFIG_EVLOG
/* Various kernel implementations provide some or all of these functions. */
extern int evl_writek(posix_log_facility_t facility, int event_type, 
		posix_log_severity_t severity, unsigned int flags, ...);
		
extern int evl_vwritek(posix_log_facility_t facility, int event_type,
		posix_log_severity_t severity, unsigned int flags,va_list args);
		
extern int posix_log_printf(posix_log_facility_t facility, int event_type, 
		posix_log_severity_t severity, unsigned int flags, 
		const char *fmt, ...);

extern int posix_log_vprintf(posix_log_facility_t facility, int event_type,
		posix_log_severity_t severity, unsigned int flags,
		const char *fmt, va_list args);
		
extern int posix_log_write(posix_log_facility_t facility, int event_type,
		posix_log_severity_t severity, const void *buf,
		size_t len, int format, unsigned int flags);

extern int evl_gen_facility_code(const char *fname,
		posix_log_facility_t *fcode);

extern int evl_register_facility(const char *fname,
		posix_log_facility_t *fcode);
#else	/* ! CONFIG_EVLOG */
inline int evl_writek(posix_log_facility_t facility, int event_type, 
		posix_log_severity_t severity, unsigned int flags, ...)
		{ return -ENOSYS; }

inline int evl_vwritek(posix_log_facility_t facility, int event_type,
		posix_log_severity_t severity, unsigned int flags,va_list args)
		{ return -ENOSYS; }
		
inline int posix_log_printf(posix_log_facility_t facility, int event_type, 
		posix_log_severity_t severity, unsigned int flags, 
		const char *fmt, ...)
		{ return -ENOSYS; }

inline int posix_log_vprintf(posix_log_facility_t facility, int event_type,
		posix_log_severity_t severity, unsigned int flags,
		const char *fmt, va_list args)
		{ return -ENOSYS; }

inline int posix_log_write(posix_log_facility_t facility, int event_type,
		posix_log_severity_t severity, const void *buf,
		size_t len, int format, unsigned int flags)
		{ return -ENOSYS; }

inline int evl_gen_facility_code(const char *fname,
		posix_log_facility_t *fcode)
		{ return -ENOSYS; }

inline int evl_register_facility(const char *fname,
		posix_log_facility_t *fcode)
		{ return -ENOSYS; }
#endif	/* CONFIG_EVLOG */

#endif	/* _LINUX_EVL_LOG_H */

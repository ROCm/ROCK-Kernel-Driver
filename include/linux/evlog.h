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

#ifndef _LINUX_EVLOG_H
#define _LINUX_EVLOG_H

#include <stdarg.h>
#include <linux/types.h>
#include <asm/types.h>

/* Values for log_flags member */
#define EVL_TRUNCATE		0x1
#define EVL_KERNEL_EVENT	0x2
#define EVL_INITIAL_BOOT_EVENT	0x4
#define EVL_KERNTIME_LOCAL	0x8
#define EVL_INTERRUPT		0x10	/* Logged from interrupt context */
#define EVL_PRINTK		0x20	/* Strip leading <n> when formatting */
#define EVL_EVTYCRC		0x40	/* Daemon will set event type = CRC */
					/* of format string. */

/* Formats for optional portion of record. */
#define EVL_NODATA    0
#define EVL_BINARY    1
#define EVL_STRING    2
#define EVL_PRINTF    3

/* Maximum length of variable portion of record */
#define EVL_ENTRY_MAXLEN (8 * 1024)

/* Facility (e.g., driver) names are truncated to 15+null. */
#define FACILITY_MAXLEN 16

/* struct kern_log_entry - kernel record header */
struct kern_log_entry {
	__u16	log_kmagic;	/* always LOGREC_KMAGIC */
	__u16	log_kversion;	/* which version of this struct? */
	__u16	log_size;	/* # bytes in variable part of record */
	__s8	log_format;	/* BINARY, STRING, PRINTF, NODATA */
	__s8	log_severity;	/* DEBUG, INFO, NOTICE, WARN, etc. */
	__s32	log_event_type;	/* facility-specific event ID */
	__u32	log_flags;	/* EVL_TRUNCATE, etc. */
	__s32	log_processor;	/* CPU ID */
	time_t	log_time_sec;
	__s32	log_time_nsec;
	uid_t	log_uid;	/* event context... */
	gid_t	log_gid;
	pid_t	log_pid;
	pid_t	log_pgrp;
	char	log_facility[FACILITY_MAXLEN];	/* e.g., driver name */
}; 

#define LOGREC_KMAGIC	0x7af8
#define LOGREC_KVERSION	3

/* Reserved Event Types */
#define EVL_BUFFER_OVERRUN	0x6

#ifdef __KERNEL__
/*
 * severities, AKA priorities
 */
#define LOG_EMERG   0   /* system is unusable */
#define LOG_ALERT   1   /* action must be taken immediately */
#define LOG_CRIT    2   /* critical conditions */
#define LOG_ERR     3   /* error conditions */
#define LOG_WARNING 4   /* warning conditions */
#define LOG_NOTICE  5   /* normal but significant condition */
#define LOG_INFO    6   /* informational */
#define LOG_DEBUG   7   /* debug-level messages */

/*
 * A buffer to pack with data, one value at a time.  By convention, b_tail
 * reflects the total amount you've attempted to add, and so may be past b_end.
 */
struct evl_recbuf {
	char *b_buf;	/* start of buffer */
	char *b_tail;	/* add next data here */
	char *b_end;	/* b_buf + buffer size */
	char *b_argsz;	/* points to argsz word in EVL_PRINTF-format record */
	char *b_zapped_nl;	/* where terminating newline was */
};

#ifdef CONFIG_EVLOG
extern int evl_write(const char *facility, int event_type,
	int severity, const void *buf, size_t len, uint flags, int format);
extern int evl_printk(const char *facility, int event_type, int sev,
	const char *fmt, ...);
extern int evl_vprintk(const char *facility, int event_type, int sev,
	const char *fmt, va_list args);

/* Functions for hand-constructing event records */
extern void evl_init_recbuf(struct evl_recbuf *b, char *buf, size_t size);
extern void evl_put(struct evl_recbuf *b, const void *data, size_t datasz);
extern void evl_puts(struct evl_recbuf *b, const char *s, int null);
extern void evl_zap_newline(struct evl_recbuf *b);
extern void evl_unzap_newline(struct evl_recbuf *b);
extern void evl_end_fmt(struct evl_recbuf *b);
extern void evl_pack_args(struct evl_recbuf *b, const char *fmt, va_list args);
extern void evl_end_args(struct evl_recbuf *b);
extern size_t evl_datasz(struct evl_recbuf *b, uint *flags);
#else	/* ! CONFIG_EVLOG */
static inline int evl_write(const char *facility, int event_type,
	int severity, const void *buf, size_t len, uint flags, int format)
	{ return -ENOSYS; }
static inline int evl_printk(const char *facility, int event_type, int sev,
	const char *fmt, ...)
	{ return -ENOSYS; }
static inline int evl_vprintk(const char *facility, int event_type, int sev,
	const char *fmt, va_list args)
	{ return -ENOSYS; }
#endif	/* CONFIG_EVLOG */

#endif	/* __KERNEL__ */

#endif	/* _LINUX_EVLOG_H */

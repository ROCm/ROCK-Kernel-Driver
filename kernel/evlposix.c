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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/evl_log.h>

/*
 * This file implements the "legacy" functions defined in early releases of
 * Event Logging.  Consider using the newer functions declared in evlog.h --
 * e.g., evl_write(), evl_printk(), evl_vprintk().
 */

extern spinlock_t evl_msgbuf_lock;
extern char evl_msgbuf[];

enum base_type {
	TY_NONE,
	TY_CHAR,
	TY_UCHAR,
	TY_SHORT,
	TY_USHORT,
	TY_INT,
	TY_UINT,
	TY_LONG,
	TY_ULONG,
	TY_LONGLONG,
	TY_ULONGLONG,
	TY_STRING,
	TY_ADDRESS
};

static struct type_info {
	size_t	ti_size;
	char	*ti_name;
} type_info[] = {
	{0,			"none"},
	{sizeof(char),		"char"},
	{sizeof(unsigned char),	"uchar"},
	{sizeof(short),		"short"},
	{sizeof(unsigned short),"ushort"},
	{sizeof(int),		"int"},
	{sizeof(unsigned int),	"uint"},
	{sizeof(long),		"long"},
	{sizeof(unsigned long),	"ulong"},
	{sizeof(long long),	"longlong"},
	{sizeof(unsigned long long),	"ulonglong"},
	{0,			"string"},
	{sizeof(void*),		"address"},
	{0,			NULL}
};

struct att_type_info {
	enum base_type	at_type;    /* TY_INT for "int", "int[]", or "2*int" */
	int		at_nelements;	/* 5 for "5*int */
	int		at_array;	/* 1 (true) for "int[]" */
};

static enum base_type
get_type_by_name(const char *name)
{
	enum base_type i;
	for (i = TY_NONE+1; type_info[i].ti_name; i++) {
		if (!strcmp(name, type_info[i].ti_name)) {
			return i;
		}
	}
	return TY_NONE;
}

/*
 * att_type should be a type spec such as "int", "int[]", or "5*int".  Parse it
 * and fill in *ti accordingly.	 Returns 0 on success, -1 on failure.
 */
static int
parse_att_type(const char *att_type, struct att_type_info *ti)
{
	const char *s, *left_bracket;
	const char *type_name;
#define MAX_TYPE_NAME_LEN 20
	char name_buf[MAX_TYPE_NAME_LEN+1];

	if (isdigit(att_type[0])) {
		/* "5*int" */
		ti->at_nelements =
			(int) simple_strtoul(att_type, (char**) &s, 10);
		if (*s != '*') {
			return -1;
		}
		type_name = s+1;
		ti->at_array = 0;
	} else if ((left_bracket = strchr(att_type, '[')) != NULL) {
		/* int[] */
		size_t name_len;
		ti->at_array = 1;
		ti->at_nelements = 0;
		if (0 != strcmp(left_bracket, "[]")) {
			return -1;
		}
		/* Copy the name to name_buf and point type_name at it. */
		type_name = name_buf;
		name_len = left_bracket - att_type;
		if (name_len == 0 || name_len > MAX_TYPE_NAME_LEN) {
			return -1;
		}
		(void) memcpy(name_buf, att_type, name_len);
		name_buf[name_len] = '\0';
	} else {
		/* "int" */
		type_name = att_type;
		ti->at_array = 0;
		ti->at_nelements = 1;
	}
	ti->at_type = get_type_by_name(type_name);
	return (ti->at_type == TY_NONE ? -1 : 0);
}

/*
 * COPYARGS copies n args of type lt (little type) from the stack into
 * buffer b.  bt (big type) is the type of the arg as it appears on the stack.
 */
#define COPYARGS(lt,bt) \
{ \
	while(n-- > 0) { \
		lt v=(lt)va_arg(args,bt); \
		evl_put(b, &v, sizeof(lt)); \
	} \
}

#ifdef CONFIG_ARCH_S390X
#define INTARG long
#else
#define INTARG int
#endif

/**
 * pack_typed_args() - Does most of the work of evl_writek.
 */
static int
pack_typed_args(struct evl_recbuf *b, va_list args)
{
	char *att_type;

	while ((att_type = va_arg(args, char*)) &&
	    (0 != strcmp(att_type, "endofdata"))) {
		struct att_type_info ti;
		if (parse_att_type(att_type, &ti) == -1) {
			return -EINVAL;
		}
		if (ti.at_array) {
			char *array;
			size_t size = type_info[ti.at_type].ti_size;
			int n;

			/* Next arg is the array size. */
			n = va_arg(args, INTARG);
			/* Next arg is the array address. */
			array = (char*) va_arg(args, void*);

			switch (ti.at_type) {
			case TY_STRING:
			    {
				/* array points to an array of char* */
				char **sarray = (char**)array;
				int i;
				for (i = 0; i < n; i++) {
					evl_puts(b, sarray[i], 1);
				}
				break;
			    }
			default:
				evl_put(b, array, n*size);
				break;
			}
		} else {
			/*
			 * One or more args of the same type.
			 */
			int n = ti.at_nelements;
			switch (ti.at_type) {
			case TY_CHAR:
			case TY_UCHAR:
				COPYARGS(char, INTARG)
				break;
			case TY_SHORT:
			case TY_USHORT:
				COPYARGS(short, INTARG)
				break;
			case TY_INT:
			case TY_UINT:
				COPYARGS(int, INTARG)
				break;
			case TY_LONG:
			case TY_ULONG:
				COPYARGS(long, long)
				break;
			case TY_LONGLONG:
			case TY_ULONGLONG:
				COPYARGS(long long, long long)
				break;
			case TY_ADDRESS:
				COPYARGS(void*, void*)
				break;
			case TY_STRING:
			    {
				char *s;
				while (n-- > 0) {
					s = (char *) va_arg(args, char*);
					evl_puts(b, s, 1);
				}
				break;
			    }
			default:
				break;
			} /* end of switch */
		} /* not array */
	} /* next att_type */

	return 0;
}

/*
 * These functions are used for logging events with log_format of
 * EVL_BINARY.  See event logging specification at
 * http://evlog.sourceforge.net/linuxEvlog.html
 * for details.
 */
int evl_writek(posix_log_facility_t facility, int event_type,
	posix_log_severity_t severity, unsigned int flags, ...)
{
	va_list args;
	int ret = 0;

	va_start(args, flags);
	ret = evl_vwritek(facility, event_type, severity, flags, args);
	va_end(args);

	return ret;
}

int evl_vwritek(posix_log_facility_t facility, int event_type,
	posix_log_severity_t severity, unsigned int flags, va_list args)
{
	unsigned long iflags;
	struct evl_recbuf b;
	int ret;
	size_t reclen;

	spin_lock_irqsave(&evl_msgbuf_lock, iflags);
	evl_init_recbuf(&b, evl_msgbuf, EVL_ENTRY_MAXLEN);
	ret = pack_typed_args(&b, args);
	if (ret == 0) {
		reclen = evl_datasz(&b, &flags);
		ret = evl_write(facility, event_type, severity,
			b.b_buf, reclen, flags, EVL_BINARY);
	}
	spin_unlock_irqrestore(&evl_msgbuf_lock, iflags);
	return ret;
}

/*
 * These functions are used for logging events with log_format of
 * EVL_STRING.  See event logging specification at
 * http://evlog.sourceforge.net/linuxEvlog.html
 * for details.
 */
int posix_log_printf(posix_log_facility_t facility, int event_type,
	posix_log_severity_t severity, unsigned int flags, const char *fmt, ...)
{
	int ret = 0;
	va_list args;

	if (!fmt) {
		return evl_write(facility, event_type, severity, NULL, 0,
			flags, EVL_NODATA);
	}

	va_start(args, fmt);
	ret = posix_log_vprintf(facility, event_type, severity, flags, fmt,
		args);
	va_end(args);
	return ret;
}

int posix_log_vprintf(posix_log_facility_t facility, int event_type,
	posix_log_severity_t severity, unsigned int flags, const char *fmt,
	va_list args)
{
	size_t recsize;
	int ret;
	unsigned long iflags;

	if (!fmt) {
		return evl_write(facility, event_type, severity, NULL, 0,
			flags, EVL_NODATA);
	}

	spin_lock_irqsave(&evl_msgbuf_lock, iflags);
	recsize = 1 + vsnprintf(evl_msgbuf, EVL_ENTRY_MAXLEN, fmt, args);
	if (recsize > EVL_ENTRY_MAXLEN) {
		recsize = EVL_ENTRY_MAXLEN;
		flags |= EVL_TRUNCATE;
	}
	ret = evl_write(facility, event_type, severity, evl_msgbuf, recsize,
		flags, EVL_STRING);
	spin_unlock_irqrestore(&evl_msgbuf_lock, iflags);
	return ret;
}

/*
 * This is the standard POSIX function for writing events to the event log.
 * See event logging specification at:
 * http://evlog.sourceforge.net/linuxEvlog.html
 */
int posix_log_write(posix_log_facility_t facility, int event_type,
	posix_log_severity_t severity, const void *buf,
	size_t recsize, int format, unsigned int flags)
{
	if (!buf || recsize == 0 || format == EVL_NODATA) {
		buf = NULL;
		recsize = 0;
		format = EVL_NODATA;
	}
	if (format == EVL_STRING && strlen((const char*)buf) != recsize-1) {
		return -EBADMSG;
	}

	return evl_write(facility, event_type, severity, buf, recsize, flags,
		format);
}

/**
 * evl_gen_facility_code() - Generate facility "code" from facility name
 * The code is just a strdup of the name.
 */
int evl_gen_facility_code(const char *fname, posix_log_facility_t *fcode)
{
	size_t name_len;
	char *s;

	if (!fname || !fcode) {
		return -EINVAL;
	}

	name_len = strlen(fname);
	if (name_len == 0 || name_len >= POSIX_LOG_MEMSTR_MAXLEN) {
		return -EINVAL;
	}

	s = kmalloc(name_len+1, GFP_KERNEL);
	if (!s) {
		return -ENOMEM;
	}
	(void) strcpy(s, fname);
	*fcode = s;
	return 0;
}

/**
 * evl_register_facility() - Generate "code" from name; pretend to register
 * We no longer register the facility from the kernel.
 */
int evl_register_facility(const char *fname, posix_log_facility_t *fcode)
{
	return evl_gen_facility_code(fname, fcode);
}

EXPORT_SYMBOL(evl_writek);
EXPORT_SYMBOL(evl_vwritek);
EXPORT_SYMBOL(posix_log_write);
EXPORT_SYMBOL(posix_log_printf);
EXPORT_SYMBOL(posix_log_vprintf);
EXPORT_SYMBOL(evl_gen_facility_code);
EXPORT_SYMBOL(evl_register_facility);

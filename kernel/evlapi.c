/*
 * Linux Event Logging
 * Copyright (C) International Business Machines Corp., 2003
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
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/evlog.h>

extern void evl_mk_rechdr(struct kern_log_entry *hdr,
	const char *facility, int event_type, int severity, size_t size,
	uint flags, int format);
extern int evl_writeh(struct kern_log_entry *hdr, const char *vardata);
extern void evl_unbrace(char *dest, const char *src, int bufsz);

/**
 * evl_write() - write header + optional buffer to event handler
 *
 * @buf: optional variable-length data
 * other args as per evl_mk_rechdr()
 */
int
evl_write(const char *facility, int event_type, int severity, const void *buf,
	size_t size, uint flags, int format)
{
	struct kern_log_entry hdr;

	evl_mk_rechdr(&hdr, facility, event_type, severity, size, flags,
		format);
	return evl_writeh(&hdr, buf);
}

void
evl_init_recbuf(struct evl_recbuf *b, char *buf, size_t size)
{
	b->b_buf = buf;
	b->b_tail = buf;
	b->b_end = buf + size;
	b->b_zapped_nl = NULL;
	b->b_argsz = NULL;
}

/**
 * evl_put() - Append data to buffer; handle overflow.
 * @b - describes buffer; updated to reflect data appended
 * @data - data to append
 * @datasz - data length in bytes
 */
void
evl_put(struct evl_recbuf *b, const void *data, size_t datasz)
{
	ptrdiff_t room = b->b_end - b->b_tail;
	if (room > 0) {
		(void) memcpy(b->b_tail, data, min(datasz, (size_t)room));
	}
	b->b_tail += datasz;
}

/**
 * evl_puts() - Append string to buffer; handle overflow.
 * Append a string to the buffer.  If null == 1, we include the terminating
 * null.  If the string extends over the end of the buffer, terminate the
 * buffer with a null.
 *
 * @b - describes buffer; updated to reflect data appended
 * @s - null-terminated string
 * @null - 1 if we append the terminating null, 0 otherwise
 */
void
evl_puts(struct evl_recbuf *b, const char *s, int null)
{
	char *old_tail = b->b_tail;
	evl_put(b, s, strlen(s) + null);
	if (b->b_tail > b->b_end && old_tail < b->b_end) {
		*(b->b_end - 1) = '\0';
	}
}

/**
 * evl_zap_newline() - Delete newline at end of format string.
 * Called after the format string has been copied into b.
 * If the format ends in a newline, remove it.  We remove the
 * terminating newline to increase flexibility when formatting
 * the record for viewing.
 */
void
evl_zap_newline(struct evl_recbuf *b)
{
	char *nl = b->b_tail - 2;
	if (b->b_buf <= nl && nl < b->b_end && *nl == '\n') {
		*nl = '\0';
		b->b_tail--;
		b->b_zapped_nl = nl;
	}
}

/**
 * evl_unzap_newline() - Replace previously zapped newline.
 * NOTE: Replacing the newline (and advancing the terminating null)
 * renders useless the contents of the record beyond the format string.
 */
void
evl_unzap_newline(struct evl_recbuf *b)
{
	if (b->b_zapped_nl) {
		b->b_zapped_nl[0] = '\n';
		b->b_zapped_nl[1] = '\0';
	}
}

/**
 * evl_end_fmt() - Make and remember room for argsz word in EVL_PRINTF rec.
 * Called after the format string has been copied in, but before the args.
 * Store zero for now; evl_end_args() will store the actual size later.
 */
void
evl_end_fmt(struct evl_recbuf *b)
{
	int argsz = 0;
	b->b_argsz = b->b_tail;
	evl_put(b, &argsz, sizeof(int));
}

/**
 * evl_end_args() - For EVL_PRINTF record, store the argsz.
 * Called after the args have been copied in.
 */
void
evl_end_args(struct evl_recbuf *b)
{
	char *args;
	int argsz;

	if (! b->b_argsz) {
		/* Nobody called evl_end_fmt(). */
		return;
	}
	args = b->b_argsz + sizeof(int);
	if (args > b->b_end) {
		/* VERY long format string: even argsz is off end of record. */
		return;
	}
	argsz = b->b_tail - args;
	memcpy(b->b_argsz, &argsz, sizeof(int));
}

static inline void
skip_atoi(const char **s)
{
	while (isdigit(**s)) {
		(*s)++;
	}
}

/**
 * parse_printf_fmt() - Parse printf/printk conversion spec.
 * fmt points to the '%' in a printk conversion specification.  Advance
 * fmt past any flags, width and/or precision specifiers, and qualifiers
 * such as 'l' and 'L'.  Return a pointer to the conversion character.
 * Stores the qualifier character (or -1, if there is none) at *pqualifier.
 * *wp is set to flags indicating whether the width and/or precision are '*'.
 * For example, given
 *      %*.2lx
 * *pqualifier is set to 'l', *wp is set to 0x1, and a pointer to the 'x'
 * is returned.
 *
 * Note: This function is derived from vsnprintf() (see lib/vsprintf.c),
 * and should be kept in sync with that function.
 *
 * @fmt - points to '%' in conversion spec
 * @pqualifier - *pqualifier is set to conversion spec's qualifier, or -1.
 * @wp - Bits in *wp are set if the width or/and precision are '*'.
 */
const char *
parse_printf_fmt(const char *fmt, int *pqualifier, int *wp)
{
	int qualifier = -1;
	*wp = 0;

	/* process flags */
	repeat:
		++fmt;          /* this also skips first '%' */
		switch (*fmt) {
			case '-':
			case '+':
			case ' ':
			case '#':
			case '0':
				goto repeat;
		}

	/* get field width */
	if (isdigit(*fmt))
		skip_atoi(&fmt);
	else if (*fmt == '*') {
		++fmt;
		/* it's the next argument */
		*wp |= 0x1;
	}

	/* get the precision */
	if (*fmt == '.') {
		++fmt;
		if (isdigit(*fmt))
			skip_atoi(&fmt);
		else if (*fmt == '*') {
			++fmt;
			/* it's the next argument */
			*wp |= 0x2;
		}
	}

	/* get the conversion qualifier */
	if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' ||
	    *fmt == 'Z' || *fmt == 'z') {
		qualifier = *fmt;
		++fmt;
		if (qualifier == 'l' && *fmt == 'l') {
			qualifier = 'L';
			++fmt;
		}
	}

	*pqualifier = qualifier;
	return fmt;
}

/**
 * evl_pack_args() - Pack args into buffer, guided by format string.
 * b describes a buffer.  fmt and args are as passed to vsnprintf().  Using
 * fmt as a guide, copy the args into b's buffer.
 *
 * @b - describes buffer; updated to reflect data added
 * @fmt - printf/printk-style format string
 * @args - values to be packed into buffer
 */
void
evl_pack_args(struct evl_recbuf *b, const char *fmt, va_list args)
{
#define COPYARG(type) \
    do { type v=va_arg(args,type); evl_put(b,&v,sizeof(v)); } while(0)

	const char *s;
	int qualifier;

	for (; *fmt ; ++fmt) {
		int wp = 0x0;
		if (*fmt != '%') {
			continue;
		}

		fmt = parse_printf_fmt(fmt, &qualifier, &wp);
		if (wp & 0x1) {
			/* width is '*' (next arg) */
			COPYARG(int);
		}
		if (wp & 0x2) {
			/* ditto precision */
			COPYARG(int);
		}

		switch (*fmt) {
			case 'c':
				COPYARG(int);
				continue;

			case 's':
				s = va_arg(args, char *);
				evl_puts(b, s, 1);
				continue;

			case 'p':
				COPYARG(void*);
				continue;

			case 'n':
				/* Skip over the %n arg. */
				if (qualifier == 'l') {
					(void) va_arg(args, long *);
				} else if (qualifier == 'Z' || qualifier == 'z') {
					(void) va_arg(args, size_t *);
				} else {
					(void) va_arg(args, int *);
				}
				continue;

			case '%':
				continue;

				/* integer number formats - handle outside switch */
			case 'o':
			case 'X':
			case 'x':
			case 'd':
			case 'i':
			case 'u':
				break;

			default:
				/* Bogus conversion.  Pass thru unchanged. */
				if (*fmt == '\0')
					--fmt;
				continue;
		}
		if (qualifier == 'L') {
			COPYARG(long long);
		} else if (qualifier == 'l') {
			COPYARG(long);
		} else if (qualifier == 'Z' || qualifier == 'z') {
			COPYARG(size_t);
		} else if (qualifier == 'h') {
			COPYARG(int);
		} else {
			COPYARG(int);
		}
	}
}

/*
 * Scratch buffer for constructing event records.  This is static because
 * (1) we want events to be logged even in low-memory situations; and
 * (2) the buffer is too big to be an auto variable.
 */
static spinlock_t msgbuf_lock = SPIN_LOCK_UNLOCKED;
static char msgbuf[EVL_ENTRY_MAXLEN];

/**
 * evl_send_printf() - Format and log a PRINTF-format message.
 * Create and log a PRINTF-format event record whose contents are:
 *	format string
 *	int containing args size
 *	args
 * @hdr - pre-constructed record header, which we adjust as needed
 * @fmt - format string
 * @args - arg list
 */
static int
evl_send_printf(struct kern_log_entry *hdr, const char *fmt, va_list args)
{
	int ret;
	struct evl_recbuf b;
	unsigned long iflags;

	spin_lock_irqsave(&msgbuf_lock, iflags);
	evl_init_recbuf(&b, msgbuf, EVL_ENTRY_MAXLEN);
	evl_puts(&b, fmt, 1);
	evl_zap_newline(&b);
	evl_end_fmt(&b);
	evl_pack_args(&b, fmt, args);
	evl_end_args(&b);

	hdr->log_size = b.b_tail - b.b_buf;
	/* Note: If size > EVL_ENTRY_MAXLEN, evl_writeh() will handle it. */
	
	ret = evl_writeh(hdr, b.b_buf);
	spin_unlock_irqrestore(&msgbuf_lock, iflags);
	return ret;
}

/**
 * evl_vprintk() - Format and log a PRINTF-format record.
 * @fmt - format string
 * @args - arg list
 * other args as per evl_mk_rechdr().  If event_type == 0, set flag to
 *	request that recipient set event type.
 */
int
evl_vprintk(const char *facility, int event_type, int severity,
	const char *fmt, va_list args)
{
	struct kern_log_entry hdr;
	unsigned int flags = 0;
	if (event_type == 0) {
		flags |= EVL_EVTYCRC;
	}
	evl_mk_rechdr(&hdr, facility, event_type, severity, 1 /*size TBD*/,
		flags, EVL_PRINTF);
	
	return evl_send_printf(&hdr, fmt, args);
}

/**
 * evl_printk() - Format and log a PRINTF-format record.
 * @fmt - format string
 * other args as per evl_mk_rechdr()
 */
int
evl_printk(const char *facility, int event_type, int severity,
	const char *fmt, ...)
{
	va_list args;
	int ret;
	va_start(args, fmt);
	ret = evl_vprintk(facility, event_type, severity, fmt, args);
	va_end(args);
	return ret;
}

/*** printkat support ***/

static int
try_extract_severity(const char *msg)
{
	if (msg[0] == '<'
	    && msg[1] >= '0' && msg[1] <= '7'
	    && msg[2] == '>') {
		return msg[1] - '0';
	}
	return -1;
}

static int
extract_severity(const char *fmt, va_list args)
{
	int sev = try_extract_severity(fmt);
	if (sev == -1 && (fmt[0] == '<' || fmt[0] == '%')) {
		/* Handle stuff like "<%d>..." and "%s..." */
		char prefix[4];
		(void) vsnprintf(prefix, 4, fmt, args);
		sev = try_extract_severity(prefix);
	}
	return sev;
}

/**
 * evl_printkat() - Log a PRINTF-format record, stripping attribute names.
 * @facility: facility name (e.g., "kern", driver name)
 * @buf, @buflen: a scratch buffer in which we construct the record
 * @fmt: format string, possibly including severity-level prefix.
 *	Any attribute names in curly braces will be stripped out by
 *	evl_unbrace().
 * other args as per printk()
 * 
 * On return, buf contains the format string, purged of {id} constructs
 * and the "{{" trailer, if any.
 */
int
evl_printkat(const char *facility, char *buf, size_t buflen, const char *fmt,
	va_list args)
{
	int ret;
	int severity;
	struct evl_recbuf b;
	struct kern_log_entry hdr;

	evl_init_recbuf(&b, buf, buflen);
	evl_unbrace(b.b_buf, fmt, (int) buflen);
	b.b_tail = b.b_buf + strlen(b.b_buf) + 1;
	evl_zap_newline(&b);
	evl_end_fmt(&b);

	evl_pack_args(&b, fmt, args);
	evl_end_args(&b);

	severity = extract_severity(b.b_buf, args);
	if (severity < 0) {
		/* See kernel.h and printk.c */
		severity = default_message_loglevel;
	}

	evl_mk_rechdr(&hdr, facility, 0, severity, b.b_tail - b.b_buf,
		EVL_EVTYCRC, EVL_PRINTF);
	/* Note: If size > EVL_ENTRY_MAXLEN, evl_writeh() will handle it. */
	
	ret = evl_writeh(&hdr, b.b_buf);

	/* Put the newline back in case caller calls printk(). */
	evl_unzap_newline(&b);
	return ret;
}

EXPORT_SYMBOL(evl_write);
EXPORT_SYMBOL(evl_printk);
EXPORT_SYMBOL(evl_vprintk);
EXPORT_SYMBOL(evl_printkat);

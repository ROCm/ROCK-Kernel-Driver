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
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/evlog.h>

#define REC_HDR_SIZE sizeof(struct kern_log_entry)

extern struct timezone sys_tz;

/* Use same buffer size as printk's, but at least 2x the max rec length. */
#define EVL_BUF_SIZE (1 << CONFIG_LOG_BUF_SHIFT)
#if (EVL_BUF_SIZE < 2*EVL_ENTRY_MAXLEN)
#undef EVL_BUF_SIZE
#define EVL_BUF_SIZE (2*EVL_ENTRY_MAXLEN)
#endif

/*
 * After buffer overflows, require at most this much free space before
 * logging events again.
 */
#define EVL_BUF_DRAINAGE (16*1024U)

/*
 * This data structure describes the circular buffer that is written into
 * by evl_writeh() and drained by evl_kbufread().
 *
 * bf_buf, bf_len, and bf_end are the start, length, and end of the buffer,
 * and in the current implementation these remain constant.
 *
 * bf_tail advances as event records are logged to the buffer, and bf_head
 * advances as records are drained from the buffer.  bf_dropped maintains
 * a count of records that have been dropped due to buffer overrun.
 * By convention:
 * - (bf_head == bf_tail) indicates an empty buffer.
 * - bf_head can take any value from bf_buf through bf_end.
 * - bf_tail starts out equal to bf_buf, but once the first record is written
 *	to the buffer, bf_tail never equals bf_buf.  It can equal bf_end.
 *
 * It is possible for one task to be draining the buffer while another
 * is writing to it.  Only evl_kbufread() advances bf_head, and only
 * copy_rec_to_cbuf() advances bf_tail.  Each advances its respective
 * pointer only after completing its operation.
 */
struct cbuf {
	char		*bf_buf;	/* base buffer address */
	unsigned int	bf_len;		/* buffer length */
	unsigned int	bf_dropped;	/* (internal) dropped count */
	char		*bf_head;	/* head-pointer for circ. buf */
	char		*bf_tail;	/* tail-pointer for circ. buf */
	char		*bf_end;	/* end buffer address */
};
 
static char evl_buffer[EVL_BUF_SIZE + sizeof(long)];

static struct cbuf ebuf = {
	evl_buffer,
	EVL_BUF_SIZE,
	0,
	evl_buffer, 
	evl_buffer,
	evl_buffer + EVL_BUF_SIZE
};

/*
 * evl_read_sem serializes reads of the evlog buffer into user space (although
 * only the logging daemon should be running evl_kbufread()).
 *
 * readq allows the reader to sleep until there's at least one record in
 * the buffer to read.
 *
 * ebuf_lock serializes writes to the evlog buffer.
 */
static DECLARE_MUTEX(evl_read_sem);
static DECLARE_WAIT_QUEUE_HEAD(readq);
static spinlock_t ebuf_lock = SPIN_LOCK_UNLOCKED;

/*
 * A region of the evlog circular buffer, possibly split into 2 chunks
 * due to wraparound.
 */
struct cbregion {
	char	*rg_head;
	char	*rg_tail;
	size_t	rg_chunk1;
	size_t	rg_chunk2;
};

/**
 * set_region() - Establish region to be written to or read from.
 * Caller wants to write to or read from an nbytes-byte region (of the
 * evlog circular buffer) starting at head.  Figure out whether the
 * region needs to be 1 chunk (typical) or 2 (due to wraparound),
 * and populate the region struct accordingly.
 *
 * @rg: region struct to be populated
 * @head: beginning of region to be read/written.  If this is beyond the
 *	end of the buffer, wrap it around to the beginning.
 * @nbytes: size of region
 */
static void
set_region(struct cbregion *rg, char *head, size_t nbytes)
{
	if (head >= ebuf.bf_end) {
		head -= ebuf.bf_len;
	}
	rg->rg_head = head;
	rg->rg_tail = head + nbytes;
	if (rg->rg_tail > ebuf.bf_end) {
		rg->rg_chunk1 = ebuf.bf_end - head;
		rg->rg_chunk2 = nbytes - rg->rg_chunk1;
		rg->rg_tail = ebuf.bf_buf + rg->rg_chunk2;
	} else {
		rg->rg_chunk1 = nbytes;
		rg->rg_chunk2 = 0;
	}
}

static void
copy_from_cbuf(const struct cbregion *rg, char *dest)
{
	memcpy(dest, rg->rg_head, rg->rg_chunk1);
	if (rg->rg_chunk2 != 0) {
		memcpy(dest + rg->rg_chunk1, ebuf.bf_buf, rg->rg_chunk2);
	}
}

static int
copy_cbuf_to_user(const struct cbregion *rg, char *ubuf)
{
	int status;
	status = copy_to_user(ubuf, rg->rg_head, rg->rg_chunk1);
	if (rg->rg_chunk2 != 0 && status == 0) {
		status = copy_to_user(ubuf + rg->rg_chunk1, ebuf.bf_buf,
			rg->rg_chunk2);
	}
	return status;
}

static void
copy_to_cbuf(const struct cbregion *rg, const char *src)
{
	memcpy(rg->rg_head, src, rg->rg_chunk1);
	if (rg->rg_chunk2 != 0) {
		memcpy(ebuf.bf_buf, src + rg->rg_chunk1, rg->rg_chunk2);
	}
}

/**
 * copy_rec_to_cbuf() - Log event (hdr + vardata) to buffer.
 * Caller has verified that there's enough room.
 */
static void
copy_rec_to_cbuf(const struct kern_log_entry *hdr, const char *vardata)
{
	struct cbregion rg;
	char *tail = ebuf.bf_tail;

	set_region(&rg, tail, REC_HDR_SIZE);
	copy_to_cbuf(&rg, (const char*) hdr);

	if (hdr->log_size != 0) {
		set_region(&rg, tail + REC_HDR_SIZE, hdr->log_size);
		copy_to_cbuf(&rg, vardata);
	}

	ebuf.bf_tail = rg.rg_tail;
}

/**
 * evl_mk_rechdr() - Populate evlog record header.
 * @facility: facility name (e.g., "kern", driver name)
 * @event_type: event type (event ID assigned by programmer; may also be
 *	computed by recipient -- e.g., CRC of format string)
 * @severity: severity level (e.g., LOG_INFO)
 * @size: length, in bytes, of variable data
 * @flags: event flags (e.g., EVL_TRUNCATE, EVL_EVTYCRC)
 * @format: format of variable data (e.g., EVL_STRING)
 */
void
evl_mk_rechdr(struct kern_log_entry *rec_hdr,
		const char *facility,
		int	event_type,
		int	severity,
		size_t	size,
		uint	flags,
		int	format)
{
	struct timespec ts;

	flags |= EVL_KERNEL_EVENT;
	if (in_interrupt()) {
		flags |= EVL_INTERRUPT;
	}

	rec_hdr->log_kmagic		=  LOGREC_KMAGIC;
	rec_hdr->log_kversion		=  LOGREC_KVERSION;
	rec_hdr->log_size		=  (__u16) size;
	rec_hdr->log_format		=  (__s8) format;
	rec_hdr->log_event_type		=  (__s32) event_type;
	rec_hdr->log_severity		=  (__s8) severity;
	rec_hdr->log_uid		=  current->uid;
	rec_hdr->log_gid		=  current->gid;
	rec_hdr->log_pid		=  current->pid;
	/* current->signal->xxx pointers may be bad. */
	if (unlikely(current->flags & PF_EXITING))
		rec_hdr->log_pgrp 	=  0;
	else
		rec_hdr->log_pgrp 	=  process_group(current);
	rec_hdr->log_flags		=  (__u32) flags;
	rec_hdr->log_processor		=  (__s32) smp_processor_id();

	strlcpy(rec_hdr->log_facility, facility, FACILITY_MAXLEN);

	if (get_seconds() == 0) {
		rec_hdr->log_flags |= EVL_INITIAL_BOOT_EVENT;
	} else {
#if defined(__i386__)
		if (sys_tz.tz_minuteswest == 0) {
			/* localtime */
			rec_hdr->log_flags |= EVL_KERNTIME_LOCAL;
		}
#endif
	}
	ts = CURRENT_TIME;
	rec_hdr->log_time_sec = (time_t) ts.tv_sec;
	rec_hdr->log_time_nsec = (__s32) ts.tv_nsec;
}

/**
 * normalize_header() - Fix up rec header, handling overflow, null vardata, etc.
 * In case of sloppiness on the part of the caller, we clean it up rather
 * than failing, since the caller is unlikely to handle failure.
 */
static void
normalize_header(struct kern_log_entry *hdr, const void *vardata)
{
	if (hdr->log_severity < 0 || hdr->log_severity > LOG_DEBUG) {
		hdr->log_severity = LOG_WARNING;
	}
	if (vardata == NULL
	    || hdr->log_size == 0
	    || hdr->log_format == EVL_NODATA) {
		hdr->log_size = 0;
		hdr->log_format = EVL_NODATA;
	}
	if (hdr->log_size > EVL_ENTRY_MAXLEN) {
		hdr->log_size = EVL_ENTRY_MAXLEN;
		hdr->log_flags |= EVL_TRUNCATE;
	}
}

/**
 * log_dropped_recs_event() - Log message about previously dropped records.
 * The evlog circular buffer had been full and caused later records to be
 * dropped.  Now the buffer has some free space again.  Log an event reporting
 * the number of records dropped.  Caller has verified that there's at least
 * enough room for this event record.
 */
static void
log_dropped_recs_event(void)
{
#define DROP_MSG_SIZE 80
	char sbuf[DROP_MSG_SIZE];
	struct kern_log_entry drechdr;

	snprintf(sbuf, DROP_MSG_SIZE,
		"%d event records dropped due to EVL buffer overflow.", 
		ebuf.bf_dropped);
	ebuf.bf_dropped = 0;
	evl_mk_rechdr(&drechdr, "kern", EVL_BUFFER_OVERRUN, LOG_INFO,
		strlen(sbuf) + 1, 0, EVL_STRING);
	copy_rec_to_cbuf(&drechdr, sbuf);
}

/**
 * evl_check_buf() - Check for space in evlog buffer.
 * If buffer free space is sufficient to log the indicated record,
 * return 0.  If not, return -1.
 *
 * Once the buffer becomes full and one or more messages are discarded,
 * a significant portion of the buffer must be drained before we permit
 * messages to be buffered again.  We count the number of discards
 * in the meantime and report them when we resume logging events.
 * If we resumed logging with a nearly full buffer, then there could
 * be a thrashing of stops and starts, making the discarded-message
 * reporting annoying.
 *
 * @hdr: The header of the record caller intends to log.
 */
static int
evl_check_buf(const struct kern_log_entry *hdr)
{
	char *head, *tail;
	size_t water_mark, avail, recsize;

	recsize = REC_HDR_SIZE + hdr->log_size;
	head    = ebuf.bf_head;
	tail	= ebuf.bf_tail;
	avail   = (head <= tail) ?
		  (ebuf.bf_len - (tail - head)) :
		  (head - tail);

	if (ebuf.bf_dropped != 0) {
		/*
		 * Still recovering from buffer overflow.
		 * Apply the low water mark.
		 */
		water_mark = min(EVL_BUF_DRAINAGE, ebuf.bf_len / 2);
		/*
		 * Just in case recsize is huge and/or somebody cranks the
		 * buffer size and/or EVL_BUF_DRAINAGE way down, make
		 * sure we have room for this record AND the "records dropped"
		 * message.
		 */
		water_mark = max(water_mark,
			recsize + REC_HDR_SIZE + DROP_MSG_SIZE);
	} else {
		/* +1 because bf_tail must never catch up with bf_head. */
		water_mark = recsize + 1;
	}

	if (avail < water_mark) {
		return -1;
	}

	/* There's enough free buffer space.  Return success. */
	if (ebuf.bf_dropped != 0) {
		log_dropped_recs_event();
	}
	return 0;
}

/**
 * evl_kbufread() - Copy records from evlog circular buffer into user space.
 * If successful, returns the number of bytes copied; else returns a
 * negative error code.
 *
 * @retbuf: pointer to the buffer to be filled with the event records
 * @bufsize: length, in bytes, of retbuf
 */
int
evl_kbufread(char *retbuf, size_t bufsize)
{
	char *rec;
	size_t rec_size;
	int error = 0;
	int retbuflen = 0;
	char *tail, *buf = retbuf;

	if (bufsize < REC_HDR_SIZE) {
		return -EINVAL;
	}

	if (ebuf.bf_head == ebuf.bf_tail && ebuf.bf_dropped != 0) {
		/*
		 * Probable scenario:
		 * 1. Somebody logged a huge burst of events and overflowed
		 * the buffer.  At this point, there was no room for the
		 * "records dropped" message.
		 * 2. evlogd drained the buffer, and is now back for more.
		 */
		unsigned long iflags;
		spin_lock_irqsave(&ebuf_lock, iflags);
		log_dropped_recs_event();
		spin_unlock_irqrestore(&ebuf_lock, iflags);
	}

	/* 
	 * We expect that only the logging daemon will be running here,
	 * but serialize access just in case.
	 */
	error = down_interruptible(&evl_read_sem);
	if (error == -EINTR) {
		return -EINTR;
	}
	/* Go to sleep if the buffer is empty.  */
	error = wait_event_interruptible(readq, 
		(ebuf.bf_head != ebuf.bf_tail));
	if (error) {
		up(&evl_read_sem);
		return error;
	}
	/*
	 * Assemble message(s) into the user buffer, as many as will
	 * fit.  On running out of space in the buffer, try to copy
	 * the header for the overflowing message.  This means that
	 * there will always be at least a header returned.  The caller
	 * must compare the numbers of bytes returned (remaining) with
	 * the length of the message to see if the entire message is
	 * present.  A subsequent read will get the entire message,
	 * including the header (again).
	 *
	 * For simplicity, take a snapshot of bf_tail, and don't read
	 * past that even if evl_writeh() pours in more records while
	 * we're draining.  We'll get those new records next time around.
	 */
	tail = ebuf.bf_tail;
	rec = ebuf.bf_head;
	if (rec == tail) { 
		/* Should not happen. Buffer must have at least one record. */
		error = -EFAULT;
		goto out;
	}

	do {
		struct cbregion rg;
		__u16 vardata_size;	/* type must match rec.log_size */

		if (bufsize < REC_HDR_SIZE) {
			break;
		}

		/*
		 * Extract log_size from header, which could be split due to
		 * wraparound, or misaligned.
		 */
		set_region(&rg, rec+offsetof(struct kern_log_entry, log_size),
			sizeof(vardata_size));
		copy_from_cbuf(&rg, (char*) &vardata_size);
		rec_size = REC_HDR_SIZE + vardata_size;

		if (bufsize < rec_size) {
			/* 
			 * Copyout only the header 'cause user buffer can't
			 * hold full record.
			 */
			set_region(&rg, rec, REC_HDR_SIZE);
			error = copy_cbuf_to_user(&rg, buf);
			if (error) {
				error = -EFAULT;
				break;
			}
			bufsize -= REC_HDR_SIZE;
			retbuflen += REC_HDR_SIZE;
			break;
		}
		set_region(&rg, rec, rec_size);
		error = copy_cbuf_to_user(&rg, buf);
		if (error) {
			error = -EFAULT;
			break;
		}
		rec = rg.rg_tail;
		buf += rec_size;
		bufsize -= rec_size;
		retbuflen += rec_size;
	} while (rec != tail);

	if (error == 0) {
		ebuf.bf_head = rec;
		error = retbuflen;
	}

out:
	up(&evl_read_sem);
	return(error);
}

/**
 * evl_writeh() - Log event, given a pre-constructed header.
 * Returns 0 on success, or a negative error code otherwise.
 * For caller's convenience, we normalize the header as needed.
 */
int
evl_writeh(struct kern_log_entry *hdr, const char *vardata)
{
	char *oldtail = ebuf.bf_tail;
	unsigned long iflags;	/* for spin_lock_irqsave() */

	normalize_header(hdr, vardata);
	
	spin_lock_irqsave(&ebuf_lock, iflags);
	if (evl_check_buf(hdr) < 0) {
		ebuf.bf_dropped++;
		spin_unlock_irqrestore(&ebuf_lock, iflags);
		return -ENOSPC;
	}

	copy_rec_to_cbuf(hdr, vardata);
	/*
	 * If the variable portion is a truncated string, make sure it
	 * ends with a null character.
	 */
	if ((hdr->log_flags & EVL_TRUNCATE) && hdr->log_format == EVL_STRING) {
		*(ebuf.bf_tail - 1) = '\0';
	}

	if ((ebuf.bf_head == oldtail) &&
	    (ebuf.bf_head != ebuf.bf_tail)) {
		wake_up_interruptible(&readq);
	}
	spin_unlock_irqrestore(&ebuf_lock, iflags);
	return 0;
}

#ifndef _TRACE_FS_H
#define _TRACE_FS_H

#include <linux/buffer_head.h>
#include <linux/time.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(fs_buffer_wait_start,
	TP_PROTO(struct buffer_head *bh),
	TP_ARGS(bh));
DECLARE_TRACE(fs_buffer_wait_end,
	TP_PROTO(struct buffer_head *bh),
	TP_ARGS(bh));
DECLARE_TRACE(fs_exec,
	TP_PROTO(char *filename),
	TP_ARGS(filename));
DECLARE_TRACE(fs_ioctl,
	TP_PROTO(unsigned int fd, unsigned int cmd, unsigned long arg),
	TP_ARGS(fd, cmd, arg));
DECLARE_TRACE(fs_open,
	TP_PROTO(int fd, char *filename),
	TP_ARGS(fd, filename));
DECLARE_TRACE(fs_close,
	TP_PROTO(unsigned int fd),
	TP_ARGS(fd));
DECLARE_TRACE(fs_lseek,
	TP_PROTO(unsigned int fd, long offset, unsigned int origin),
	TP_ARGS(fd, offset, origin));
DECLARE_TRACE(fs_llseek,
	TP_PROTO(unsigned int fd, loff_t offset, unsigned int origin),
	TP_ARGS(fd, offset, origin));

/*
 * Probes must be aware that __user * may be modified by concurrent userspace
 * or kernel threads.
 */
DECLARE_TRACE(fs_read,
	TP_PROTO(unsigned int fd, char __user *buf, size_t count, ssize_t ret),
	TP_ARGS(fd, buf, count, ret));
DECLARE_TRACE(fs_write,
	TP_PROTO(unsigned int fd, const char __user *buf, size_t count,
		ssize_t ret),
	TP_ARGS(fd, buf, count, ret));
DECLARE_TRACE(fs_pread64,
	TP_PROTO(unsigned int fd, char __user *buf, size_t count, loff_t pos,
		ssize_t ret),
	TP_ARGS(fd, buf, count, pos, ret));
DECLARE_TRACE(fs_pwrite64,
	TP_PROTO(unsigned int fd, const char __user *buf, size_t count,
		loff_t pos, ssize_t ret),
	TP_ARGS(fd, buf, count, pos, ret));
DECLARE_TRACE(fs_readv,
	TP_PROTO(unsigned long fd, const struct iovec __user *vec,
		unsigned long vlen, ssize_t ret),
	TP_ARGS(fd, vec, vlen, ret));
DECLARE_TRACE(fs_writev,
	TP_PROTO(unsigned long fd, const struct iovec __user *vec,
		unsigned long vlen, ssize_t ret),
	TP_ARGS(fd, vec, vlen, ret));
DECLARE_TRACE(fs_select,
	TP_PROTO(int fd, struct timespec *end_time),
	TP_ARGS(fd, end_time));
DECLARE_TRACE(fs_poll,
	TP_PROTO(int fd),
	TP_ARGS(fd));
#endif

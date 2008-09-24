#ifndef _TRACE_FS_H
#define _TRACE_FS_H

#include <linux/buffer_head.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(fs_buffer_wait_start,
	TPPROTO(struct buffer_head *bh),
	TPARGS(bh));
DEFINE_TRACE(fs_buffer_wait_end,
	TPPROTO(struct buffer_head *bh),
	TPARGS(bh));
DEFINE_TRACE(fs_exec,
	TPPROTO(char *filename),
	TPARGS(filename));
DEFINE_TRACE(fs_ioctl,
	TPPROTO(unsigned int fd, unsigned int cmd, unsigned long arg),
	TPARGS(fd, cmd, arg));
DEFINE_TRACE(fs_open,
	TPPROTO(int fd, char *filename),
	TPARGS(fd, filename));
DEFINE_TRACE(fs_close,
	TPPROTO(unsigned int fd),
	TPARGS(fd));
DEFINE_TRACE(fs_lseek,
	TPPROTO(unsigned int fd, long offset, unsigned int origin),
	TPARGS(fd, offset, origin));
DEFINE_TRACE(fs_llseek,
	TPPROTO(unsigned int fd, loff_t offset, unsigned int origin),
	TPARGS(fd, offset, origin));

/*
 * Probes must be aware that __user * may be modified by concurrent userspace
 * or kernel threads.
 */
DEFINE_TRACE(fs_read,
	TPPROTO(unsigned int fd, char __user *buf, size_t count, ssize_t ret),
	TPARGS(fd, buf, count, ret));
DEFINE_TRACE(fs_write,
	TPPROTO(unsigned int fd, const char __user *buf, size_t count,
		ssize_t ret),
	TPARGS(fd, buf, count, ret));
DEFINE_TRACE(fs_pread64,
	TPPROTO(unsigned int fd, char __user *buf, size_t count, loff_t pos,
		ssize_t ret),
	TPARGS(fd, buf, count, pos, ret));
DEFINE_TRACE(fs_pwrite64,
	TPPROTO(unsigned int fd, const char __user *buf, size_t count,
		loff_t pos, ssize_t ret),
	TPARGS(fd, buf, count, pos, ret));
DEFINE_TRACE(fs_readv,
	TPPROTO(unsigned long fd, const struct iovec __user *vec,
		unsigned long vlen, ssize_t ret),
	TPARGS(fd, vec, vlen, ret));
DEFINE_TRACE(fs_writev,
	TPPROTO(unsigned long fd, const struct iovec __user *vec,
		unsigned long vlen, ssize_t ret),
	TPARGS(fd, vec, vlen, ret));
DEFINE_TRACE(fs_select,
	TPPROTO(int fd, s64 timeout),
	TPARGS(fd, timeout));
DEFINE_TRACE(fs_poll,
	TPPROTO(int fd),
	TPARGS(fd));
#endif

/* 
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include "os.h"
#include "helper.h"
#include "aio.h"
#include "init.h"
#include "user.h"
#include "mode.h"

struct aio_thread_req {
	enum aio_type type;
	int io_fd;
	unsigned long long offset;
	char *buf;
	int len;
	int reply_fd;
	void *data;
};

static int aio_req_fd_r = -1;
static int aio_req_fd_w = -1;

#if defined(HAVE_AIO_ABI)
#include <linux/aio_abi.h>

/* If we have the headers, we are going to build with AIO enabled.
 * If we don't have aio in libc, we define the necessary stubs here.
 */

#if !defined(HAVE_AIO_LIBC)

#define __NR_io_setup 245
#define __NR_io_getevents 247
#define __NR_io_submit 248

static long io_setup(int n, aio_context_t *ctxp)
{
  return(syscall(__NR_io_setup, n, ctxp));
}

static long io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp)
{
  return(syscall(__NR_io_submit, ctx, nr, iocbpp));
}

static long io_getevents(aio_context_t ctx_id, long min_nr, long nr,
			 struct io_event *events, struct timespec *timeout)
{
  return(syscall(__NR_io_getevents, ctx_id, min_nr, nr, events, timeout));
}

#endif

/* The AIO_MMAP cases force the mmapped page into memory here
 * rather than in whatever place first touches the data.  I used
 * to do this by touching the page, but that's delicate because
 * gcc is prone to optimizing that away.  So, what's done here
 * is we read from the descriptor from which the page was 
 * mapped.  The caller is required to pass an offset which is
 * inside the page that was mapped.  Thus, when the read 
 * returns, we know that the page is in the page cache, and
 * that it now backs the mmapped area.
 */

static int do_aio(aio_context_t ctx, enum aio_type type, int fd, char *buf, 
		  int len, unsigned long long offset, void *data)
{
	struct iocb iocb, *iocbp = &iocb;
	char c;
	int err;

	iocb = ((struct iocb) { .aio_data 	= (unsigned long) data,
				.aio_reqprio	= 0,
				.aio_fildes	= fd,
				.aio_buf	= (unsigned long) buf,
				.aio_nbytes	= len,
				.aio_offset	= offset,
				.aio_reserved1	= 0,
				.aio_reserved2	= 0,
				.aio_reserved3	= 0 });

	switch(type){
	case AIO_READ:
		iocb.aio_lio_opcode = IOCB_CMD_PREAD;
		err = io_submit(ctx, 1, &iocbp);
		break;
	case AIO_WRITE:
		iocb.aio_lio_opcode = IOCB_CMD_PWRITE;
		err = io_submit(ctx, 1, &iocbp);
		break;
	case AIO_MMAP:
		iocb.aio_lio_opcode = IOCB_CMD_PREAD;
		iocb.aio_buf = (unsigned long) &c;
		iocb.aio_nbytes = sizeof(c);
		err = io_submit(ctx, 1, &iocbp);
		break;
	default:
		printk("Bogus op in do_aio - %d\n", type);
		err = -EINVAL;
		break;
	}
	if(err > 0)
		err = 0;

	return(err);	
}

static aio_context_t ctx = 0;

static int aio_thread(void *arg)
{
	struct aio_thread_reply reply;
	struct io_event event;
	int err, n, reply_fd;

	signal(SIGWINCH, SIG_IGN);

	while(1){
		n = io_getevents(ctx, 1, 1, &event, NULL);
		if(n < 0){
			if(errno == EINTR)
				continue;
			printk("aio_thread - io_getevents failed, "
			       "errno = %d\n", errno);
		}
		else {
			reply = ((struct aio_thread_reply) 
				{ .data = (void *) event.data,
				  .err	= event.res });
			reply_fd = 
				((struct aio_context *) event.data)->reply_fd;
			err = os_write_file(reply_fd, &reply, sizeof(reply));
			if(err != sizeof(reply))
				printk("not_aio_thread - write failed, "
				       "fd = %d, err = %d\n", 
				       aio_req_fd_r, -err);
		}
	}
	return(0);
}

#endif

static int do_not_aio(struct aio_thread_req *req)
{
	char c;
	int err;

	switch(req->type){
	case AIO_READ:
		err = os_seek_file(req->io_fd, req->offset);
		if(err)
			goto out;

		err = os_read_file(req->io_fd, req->buf, req->len);
		break;
	case AIO_WRITE:
		err = os_seek_file(req->io_fd, req->offset);
		if(err)
			goto out;

		err = os_write_file(req->io_fd, req->buf, req->len);
		break;
	case AIO_MMAP:
		err = os_seek_file(req->io_fd, req->offset);
		if(err)
			goto out;

		err = os_read_file(req->io_fd, &c, sizeof(c));
		break;
	default:
		printk("do_not_aio - bad request type : %d\n", req->type);
		err = -EINVAL;
		break;
	}

 out:
	return(err);
}

static int not_aio_thread(void *arg)
{
	struct aio_thread_req req;
	struct aio_thread_reply reply;
	int err;

	signal(SIGWINCH, SIG_IGN);
	while(1){
		err = os_read_file(aio_req_fd_r, &req, sizeof(req));
		if(err != sizeof(req)){
			if(err < 0)
				printk("not_aio_thread - read failed, fd = %d, "
				       "err = %d\n", aio_req_fd_r, -err);
			else {
				printk("not_aio_thread - short read, fd = %d, "
				       "length = %d\n", aio_req_fd_r, err);
			}
			continue;
		}
		err = do_not_aio(&req);
		reply = ((struct aio_thread_reply) { .data 	= req.data,
						     .err	= err });
		err = os_write_file(req.reply_fd, &reply, sizeof(reply));
		if(err != sizeof(reply))
			printk("not_aio_thread - write failed, fd = %d, "
			       "err = %d\n", aio_req_fd_r, -err);
	}
}

static int aio_pid = -1;

static int init_aio_24(void)
{
	unsigned long stack;
	int fds[2], err;
	
	err = os_pipe(fds, 1, 1);
	if(err)
		goto out;

	aio_req_fd_w = fds[0];
	aio_req_fd_r = fds[1];
	err = run_helper_thread(not_aio_thread, NULL, 
				CLONE_FILES | CLONE_VM | SIGCHLD, &stack, 0);
	if(err < 0)
		goto out_close_pipe;

	aio_pid = err;
	goto out;

 out_close_pipe:
	os_close_file(fds[0]);
	os_close_file(fds[1]);
	aio_req_fd_w = -1;
	aio_req_fd_r = -1;	
 out:
	return(0);
}

#ifdef HAVE_AIO_ABI
#define DEFAULT_24_AIO 0
static int init_aio_26(void)
{
	unsigned long stack;
	int err;
	
	if(io_setup(256, &ctx)){
		printk("aio_thread failed to initialize context, err = %d\n",
		       errno);
		return(-errno);
	}

	err = run_helper_thread(aio_thread, NULL, 
				CLONE_FILES | CLONE_VM | SIGCHLD, &stack, 0);
	if(err < 0)
		return(-errno);

	aio_pid = err;
	err = 0;
 out:
	return(err);
}

int submit_aio_26(enum aio_type type, int io_fd, char *buf, int len, 
		  unsigned long long offset, int reply_fd, void *data)
{
	struct aio_thread_reply reply;
	int err;

	((struct aio_context *) data)->reply_fd = reply_fd;

	err = do_aio(ctx, type, io_fd, buf, len, offset, data);
	if(err){
		reply = ((struct aio_thread_reply) { .data = data,
						     .err  = err });
		err = os_write_file(reply_fd, &reply, sizeof(reply));
		if(err != sizeof(reply))
			printk("submit_aio_26 - write failed, "
			       "fd = %d, err = %d\n", reply_fd, -err);
		else err = 0;
	}

	return(err);
}

#else
#define DEFAULT_24_AIO 1
static int init_aio_26(void)
{
	return(-ENOSYS);
}

int submit_aio_26(enum aio_type type, int io_fd, char *buf, int len, 
		  unsigned long long offset, int reply_fd, void *data)
{
	return(-ENOSYS);
}
#endif

static int aio_24 = DEFAULT_24_AIO;

static int __init set_aio_24(char *name, int *add)
{
	aio_24 = 1;
	return(0);
}

__uml_setup("aio=2.4", set_aio_24,
"aio=2.4\n"
"    This is used to force UML to use 2.4-style AIO even when 2.6 AIO is\n"
"    available.  2.4 AIO is a single thread that handles one request at a\n"
"    time, synchronously.  2.6 AIO is a thread which uses 2.5 AIO interface\n"
"    to handle an arbitrary number of pending requests.  2.6 AIO is not\n"
"    available in tt mode, on 2.4 hosts, or when UML is built with\n"
"    /usr/include/linux/aio_abi no available.\n\n"
);

static int init_aio(void)
{
	int err;

	CHOOSE_MODE(({ 
		if(!aio_24){ 
			printk("Disabling 2.6 AIO in tt mode\n");
			aio_24 = 1;
		} }), (void) 0);

	if(!aio_24){
		err = init_aio_26();
		if(err && (errno == ENOSYS)){
			printk("2.6 AIO not supported on the host - "
			       "reverting to 2.4 AIO\n");
			aio_24 = 1;
		}
		else return(err);
	}

	if(aio_24)
		return(init_aio_24());

	return(0);
}

__initcall(init_aio);

static void exit_aio(void)
{
	if(aio_pid != -1)
		os_kill_process(aio_pid, 1);
}

__uml_exitcall(exit_aio);

int submit_aio_24(enum aio_type type, int io_fd, char *buf, int len, 
		  unsigned long long offset, int reply_fd, void *data)
{
	struct aio_thread_req req = { .type 		= type,
				      .io_fd		= io_fd,
				      .offset		= offset,
				      .buf		= buf,
				      .len		= len,
				      .reply_fd		= reply_fd,
				      .data		= data,
	};
	int err;

	err = os_write_file(aio_req_fd_w, &req, sizeof(req));
	if(err == sizeof(req))
		err = 0;

	return(err);
}

int submit_aio(enum aio_type type, int io_fd, char *buf, int len, 
	       unsigned long long offset, int reply_fd, void *data)
{
	if(aio_24)
		return(submit_aio_24(type, io_fd, buf, len, offset, reply_fd, 
				     data));
	else {
		return(submit_aio_26(type, io_fd, buf, len, offset, reply_fd, 
				     data));
	}
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

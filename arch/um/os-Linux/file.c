/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include "os.h"
#include "user.h"
#include "kern_util.h"

int os_file_type(char *file)
{
	struct stat64 buf;

	if(stat64(file, &buf) == -1)
		return(-errno);

	if(S_ISDIR(buf.st_mode)) return(OS_TYPE_DIR);
	else if(S_ISLNK(buf.st_mode)) return(OS_TYPE_SYMLINK);
	else if(S_ISCHR(buf.st_mode)) return(OS_TYPE_CHARDEV);
	else if(S_ISBLK(buf.st_mode)) return(OS_TYPE_BLOCKDEV);
	else if(S_ISFIFO(buf.st_mode)) return(OS_TYPE_FIFO);
	else if(S_ISSOCK(buf.st_mode)) return(OS_TYPE_SOCK);
	else return(OS_TYPE_FILE);
}

int os_file_mode(char *file, struct openflags *mode_out)
{
	*mode_out = OPENFLAGS();

	if(!access(file, W_OK)) *mode_out = of_write(*mode_out);
	else if(errno != EACCES) 
		return(-errno);

	if(!access(file, R_OK)) *mode_out = of_read(*mode_out);
	else if(errno != EACCES) 
		return(-errno);

	return(0);
}

int os_open_file(char *file, struct openflags flags, int mode)
{
	int fd, f = 0;

	if(flags.r && flags.w) f = O_RDWR;
	else if(flags.r) f = O_RDONLY;
	else if(flags.w) f = O_WRONLY;
	else f = 0;

	if(flags.s) f |= O_SYNC;
	if(flags.c) f |= O_CREAT;
	if(flags.t) f |= O_TRUNC;
	if(flags.e) f |= O_EXCL;

	fd = open64(file, f, mode);
	if(fd < 0) return(-errno);
	
	if(flags.cl){
		if(fcntl(fd, F_SETFD, 1)){
			close(fd);
			return(-errno);
		}
	}

 	return(fd);
	return(fd);
}

int os_connect_socket(char *name)
{
	struct sockaddr_un sock;
	int fd, err;

	sock.sun_family = AF_UNIX;
	snprintf(sock.sun_path, sizeof(sock.sun_path), "%s", name);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd < 0)
		return(fd);

	err = connect(fd, (struct sockaddr *) &sock, sizeof(sock));
	if(err)
		return(err);

	return(fd);
}

void os_close_file(int fd)
{
	close(fd);
}

int os_seek_file(int fd, __u64 offset)
{
	__u64 actual;

	actual = lseek64(fd, offset, SEEK_SET);
	if(actual != offset) return(-errno);
	return(0);
}

int os_read_file(int fd, void *buf, int len)
{
	int n;

	/* Force buf into memory if it's not already. */

	/* XXX This fails if buf is kernel memory */
#ifdef notdef
	if(copy_to_user_proc(buf, &c, sizeof(c)))
		return(-EFAULT);
#endif

	n = read(fd, buf, len);
	if(n < 0)
		return(-errno);
	return(n);
}

int os_write_file(int fd, void *buf, int count)
{
	int n;

	/* Force buf into memory if it's not already. */
	
	/* XXX This fails if buf is kernel memory */
#ifdef notdef
	if(copy_to_user_proc(buf, buf, buf[0]))
		return(-EFAULT);
#endif

	n = write(fd, buf, count);
	if(n < 0)
		return(-errno);
	return(n);
}

int os_file_size(char *file, long long *size_out)
{
	struct stat64 buf;

	if(stat64(file, &buf) == -1){
		printk("Couldn't stat \"%s\" : errno = %d\n", file, errno);
		return(-errno);
	}
	if(S_ISBLK(buf.st_mode)){
		int fd, blocks;

		if((fd = open64(file, O_RDONLY)) < 0){
			printk("Couldn't open \"%s\", errno = %d\n", file,
			       errno);
			return(-errno);
		}
		if(ioctl(fd, BLKGETSIZE, &blocks) < 0){
			printk("Couldn't get the block size of \"%s\", "
			       "errno = %d\n", file, errno);
			close(fd);
			return(-errno);
		}
		*size_out = ((long long) blocks) * 512;
		close(fd);
		return(0);
	}
	*size_out = buf.st_size;
	return(0);
}

int os_pipe(int *fds, int stream, int close_on_exec)
{
	int err, type = stream ? SOCK_STREAM : SOCK_DGRAM;

	err = socketpair(AF_UNIX, type, 0, fds);
	if(err) 
		return(-errno);

	if(!close_on_exec)
		return(0);

	if((fcntl(fds[0], F_SETFD, 1) < 0) || (fcntl(fds[1], F_SETFD, 1) < 0))
		printk("os_pipe : Setting FD_CLOEXEC failed, errno = %d", 
		       errno);

	return(0);
}

int os_set_fd_async(int fd, int owner)
{
	/* XXX This should do F_GETFL first */
	if(fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK) < 0){
		printk("os_set_fd_async : failed to set O_ASYNC and "
		       "O_NONBLOCK on fd # %d, errno = %d\n", fd, errno);
		return(-errno);
	}
#ifdef notdef
	if(fcntl(fd, F_SETFD, 1) < 0){
		printk("os_set_fd_async : Setting FD_CLOEXEC failed, "
		       "errno = %d\n", errno);
	}
#endif

	if((fcntl(fd, F_SETSIG, SIGIO) < 0) ||
	   (fcntl(fd, F_SETOWN, owner) < 0)){
		printk("os_set_fd_async : Failed to fcntl F_SETOWN "
		       "(or F_SETSIG) fd %d to pid %d, errno = %d\n", fd, 
		       owner, errno);
		return(-errno);
	}

	return(0);
}

int os_set_fd_block(int fd, int blocking)
{
	int flags;

	flags = fcntl(fd, F_GETFL);

	if(blocking) flags &= ~O_NONBLOCK;
	else flags |= O_NONBLOCK;

	if(fcntl(fd, F_SETFL, flags) < 0){
		printk("Failed to change blocking on fd # %d, errno = %d\n",
		       fd, errno);
		return(-errno);
	}
	return(0);
}

int os_accept_connection(int fd)
{
	int new;

	new = accept(fd, NULL, 0);
	if(new < 0) 
		return(-errno);
	return(new);
}

#ifndef SHUT_RD
#define SHUT_RD 0
#endif

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

int os_shutdown_socket(int fd, int r, int w)
{
	int what, err;

	if(r && w) what = SHUT_RDWR;
	else if(r) what = SHUT_RD;
	else if(w) what = SHUT_WR;
	else {
		printk("os_shutdown_socket : neither r or w was set\n");
		return(-EINVAL);
	}
	err = shutdown(fd, what);
	if(err)
		return(-errno);
	return(0);
}

int os_rcv_fd(int fd, int *helper_pid_out)
{
	int new, n;
	char buf[CMSG_SPACE(sizeof(new))];
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	iov = ((struct iovec) { .iov_base  = helper_pid_out,
				.iov_len   = sizeof(*helper_pid_out) });
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_flags = 0;

	n = recvmsg(fd, &msg, 0);
	if(n < 0)
		return(-errno);

	else if(n != sizeof(iov.iov_len))
		*helper_pid_out = -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == NULL){
		printk("rcv_fd didn't receive anything, error = %d\n", errno);
		return(-1);
	}
	if((cmsg->cmsg_level != SOL_SOCKET) || 
	   (cmsg->cmsg_type != SCM_RIGHTS)){
		printk("rcv_fd didn't receive a descriptor\n");
		return(-1);
	}

	new = ((int *) CMSG_DATA(cmsg))[0];
	return(new);
}

int create_unix_socket(char *file, int len)
{
	struct sockaddr_un addr;
	int sock, err;

	sock = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0){
		printk("create_unix_socket - socket failed, errno = %d\n",
		       errno);
		return(-errno);
	}

	addr.sun_family = AF_UNIX;

	/* XXX Be more careful about overflow */
	snprintf(addr.sun_path, len, "%s", file);

	err = bind(sock, (struct sockaddr *) &addr, sizeof(addr));
	if (err < 0){
		printk("create_listening_socket - bind failed, errno = %d\n",
		       errno);
		return(-errno);
	}

	return(sock);
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

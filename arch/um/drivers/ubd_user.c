/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Copyright (C) 2001 Ridgerun,Inc (glonnon@ridgerun.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include "asm/types.h"
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "ubd_user.h"
#include "os.h"
#include "cow.h"

#include <endian.h>
#include <byteswap.h>

static int same_backing_files(char *from_cmdline, char *from_cow, char *cow)
{
	struct stat64 buf1, buf2;

	if(from_cmdline == NULL) return(1);
	if(!strcmp(from_cmdline, from_cow)) return(1);

	if(stat64(from_cmdline, &buf1) < 0){
		printk("Couldn't stat '%s', errno = %d\n", from_cmdline, 
		       errno);
		return(1);
	}
	if(stat64(from_cow, &buf2) < 0){
		printk("Couldn't stat '%s', errno = %d\n", from_cow, errno);
		return(1);
	}
	if((buf1.st_dev == buf2.st_dev) && (buf1.st_ino == buf2.st_ino))
		return(1);

	printk("Backing file mismatch - \"%s\" requested,\n"
	       "\"%s\" specified in COW header of \"%s\"\n",
	       from_cmdline, from_cow, cow);
	return(0);
}

static int backing_file_mismatch(char *file, __u64 size, time_t mtime)
{
	struct stat64 buf;
	long long actual;
	int err;

  	if(stat64(file, &buf) < 0){
		printk("Failed to stat backing file \"%s\", errno = %d\n",
		       file, errno);
		return(-errno);
	}

	err = os_file_size(file, &actual);
	if(err){
		printk("Failed to get size of backing file \"%s\", "
		       "errno = %d\n", file, -err);
		return(err);
	}

  	if(actual != size){
		printk("Size mismatch (%ld vs %ld) of COW header vs backing "
		       "file\n", size, actual);
		return(-EINVAL);
	}
	if(buf.st_mtime != mtime){
		printk("mtime mismatch (%ld vs %ld) of COW header vs backing "
		       "file\n", mtime, buf.st_mtime);
		return(-EINVAL);
	}
	return(0);
}

int read_cow_bitmap(int fd, void *buf, int offset, int len)
{
	int err;

	err = os_seek_file(fd, offset);
	if(err != 0) return(-errno);
	err = read(fd, buf, len);
	if(err < 0) return(-errno);
	return(0);
}

int open_ubd_file(char *file, struct openflags *openflags, 
		  char **backing_file_out, int *bitmap_offset_out, 
		  unsigned long *bitmap_len_out, int *data_offset_out, 
		  int *create_cow_out)
{
	time_t mtime;
	__u64 size;
	char *backing_file;
        int fd, err, sectorsize, magic, same, mode = 0644;

        if((fd = os_open_file(file, *openflags, mode)) < 0){
		if((fd == -ENOENT) && (create_cow_out != NULL))
			*create_cow_out = 1;
                if(!openflags->w ||
                   ((errno != EROFS) && (errno != EACCES))) return(-errno);
		openflags->w = 0;
                if((fd = os_open_file(file, *openflags, mode)) < 0) 
			return(fd);
        }

	err = os_lock_file(fd, openflags->w);
	if(err){
		printk("Failed to lock '%s', errno = %d\n", file, -err);
		goto error;
	}
	
	if(backing_file_out == NULL) return(fd);

	err = read_cow_header(file_reader, &fd, &magic, &backing_file, &mtime, 
			      &size, &sectorsize, bitmap_offset_out);
	if(err && (*backing_file_out != NULL)){
		printk("Failed to read COW header from COW file \"%s\", "
		       "errno = %d\n", file, err);
		goto error;
	}
	if(err) return(fd);

	if(backing_file_out == NULL) return(fd);
	
	same = same_backing_files(*backing_file_out, backing_file, file);

	if(!same && !backing_file_mismatch(*backing_file_out, size, mtime)){
		printk("Switching backing file to '%s'\n", *backing_file_out);
		err = write_cow_header(file, fd, *backing_file_out, 
				       sectorsize, &size);
		if(err){
			printk("Switch failed, errno = %d\n", err);
			return(err);
		}
	}
	else {
		*backing_file_out = backing_file;
		err = backing_file_mismatch(*backing_file_out, size, mtime);
		if(err) goto error;
	}

	cow_sizes(size, sectorsize, *bitmap_offset_out, bitmap_len_out, 
		  data_offset_out);

        return(fd);
 error:
	os_close_file(fd);
	return(err);
}

int create_cow_file(char *cow_file, char *backing_file, struct openflags flags,
		    int sectorsize, int *bitmap_offset_out, 
		    unsigned long *bitmap_len_out, int *data_offset_out)
{
	int err, fd;

	flags.c = 1;
	fd = open_ubd_file(cow_file, &flags, NULL, NULL, NULL, NULL, NULL);
	if(fd < 0){
		err = fd;
		printk("Open of COW file '%s' failed, errno = %d\n", cow_file,
		       -err);
		goto out;
	}

	err = init_cow_file(fd, cow_file, backing_file, sectorsize, 
			    bitmap_offset_out, bitmap_len_out, 
			    data_offset_out);
	if(!err)
		return(fd);
	os_close_file(fd);
 out:
	return(err);
}

int read_ubd_fs(int fd, void *buffer, int len)
{
	int n;

	n = read(fd, buffer, len);
	if(n < 0) return(-errno);
	else return(n);
}

int write_ubd_fs(int fd, char *buffer, int len)
{
	int n;

	n = write(fd, buffer, len);
	if(n < 0) return(-errno);
	else return(n);
}

void do_io(struct io_thread_req *req)
{
	char *buf;
	unsigned long len;
	int n, nsectors, start, end, bit;
	__u64 off;

	nsectors = req->length / req->sectorsize;
	start = 0;
	do {
		bit = ubd_test_bit(start, (unsigned char *) &req->sector_mask);
		end = start;
		while((end < nsectors) && 
		      (ubd_test_bit(end, (unsigned char *) 
				    &req->sector_mask) == bit))
			end++;

		if(end != nsectors)
			printk("end != nsectors\n");
		off = req->offset + req->offsets[bit] + 
			start * req->sectorsize;
		len = (end - start) * req->sectorsize;
		buf = &req->buffer[start * req->sectorsize];

		if(os_seek_file(req->fds[bit], off) != 0){
			printk("do_io - lseek failed : errno = %d\n", errno);
			req->error = 1;
			return;
		}
		if(req->op == UBD_READ){
			n = 0;
			do {
				buf = &buf[n];
				len -= n;
				n = read(req->fds[bit], buf, len);
				if (n < 0) {
					printk("do_io - read returned %d : "
					       "errno = %d fd = %d\n", n,
					       errno, req->fds[bit]);
					req->error = 1;
					return;
				}
			} while((n < len) && (n != 0));
			if (n < len) memset(&buf[n], 0, len - n);
		}
		else {
			n = write(req->fds[bit], buf, len);
			if(n != len){
				printk("do_io - write returned %d : "
				       "errno = %d fd = %d\n", n, 
				       errno, req->fds[bit]);
				req->error = 1;
				return;
			}
		}

		start = end;
	} while(start < nsectors);

	if(req->cow_offset != -1){
		if(os_seek_file(req->fds[1], req->cow_offset) != 0){
			printk("do_io - bitmap lseek failed : errno = %d\n",
			       errno);
			req->error = 1;
			return;
		}
		n = write(req->fds[1], &req->bitmap_words, 
			  sizeof(req->bitmap_words));
		if(n != sizeof(req->bitmap_words)){
			printk("do_io - bitmap update returned %d : "
			       "errno = %d fd = %d\n", n, errno, req->fds[1]);
			req->error = 1;
			return;
		}
	}
	req->error = 0;
	return;
}

/* Changed in start_io_thread, which is serialized by being called only
 * from ubd_init, which is an initcall.
 */
int kernel_fd = -1;

/* Only changed by the io thread */
int io_count = 0;

int io_thread(void *arg)
{
	struct io_thread_req req;
	int n;

	signal(SIGWINCH, SIG_IGN);
	while(1){
		n = read(kernel_fd, &req, sizeof(req));
		if(n < 0) printk("io_thread - read returned %d, errno = %d\n",
				 n, errno);
		else if(n < sizeof(req)){
			printk("io_thread - short read : length = %d\n", n);
			continue;
		}
		io_count++;
		do_io(&req);
		n = write(kernel_fd, &req, sizeof(req));
		if(n != sizeof(req))
			printk("io_thread - write failed, errno = %d\n",
			       errno);
	}
}

int start_io_thread(unsigned long sp, int *fd_out)
{
	int pid, fds[2], err;

	err = os_pipe(fds, 1, 1);
	if(err){
		printk("start_io_thread - os_pipe failed, errno = %d\n", -err);
		return(-1);
	}
	kernel_fd = fds[0];
	*fd_out = fds[1];

	pid = clone(io_thread, (void *) sp, CLONE_FILES | CLONE_VM | SIGCHLD,
		    NULL);
	if(pid < 0){
		printk("start_io_thread - clone failed : errno = %d\n", errno);
		return(-errno);
	}
	return(pid);
}

#ifdef notdef
int start_io_thread(unsigned long sp, int *fd_out)
{
	int pid;

	if((kernel_fd = get_pty()) < 0) return(-1);
	raw(kernel_fd, 0);
	if((*fd_out = open(ptsname(kernel_fd), O_RDWR)) < 0){
		printk("Couldn't open tty for IO\n");
		return(-1);
	}

	pid = clone(io_thread, (void *) sp, CLONE_FILES | CLONE_VM | SIGCHLD,
		    NULL);
	if(pid < 0){
		printk("start_io_thread - clone failed : errno = %d\n", errno);
		return(-errno);
	}
	return(pid);
}
#endif

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

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

#include <endian.h>
#include <byteswap.h>
#if __BYTE_ORDER == __BIG_ENDIAN
# define ntohll(x) (x)
# define htonll(x) (x)
#elif __BYTE_ORDER == __LITTLE_ENDIAN
# define ntohll(x)  bswap_64(x)
# define htonll(x)  bswap_64(x)
#else
#error "__BYTE_ORDER not defined"
#endif

#define PATH_LEN_V1 256

struct cow_header_v1 {
	int magic;
	int version;
	char backing_file[PATH_LEN_V1];
	time_t mtime;
	__u64 size;
	int sectorsize;
};

#define PATH_LEN_V2 MAXPATHLEN

struct cow_header_v2 {
	unsigned long magic;
	unsigned long version;
	char backing_file[PATH_LEN_V2];
	time_t mtime;
	__u64 size;
	int sectorsize;
};

union cow_header {
	struct cow_header_v1 v1;
	struct cow_header_v2 v2;
};

#define COW_MAGIC 0x4f4f4f4d  /* MOOO */
#define COW_VERSION 2

static void sizes(__u64 size, int sectorsize, int bitmap_offset, 
		  unsigned long *bitmap_len_out, int *data_offset_out)
{
	*bitmap_len_out = (size + sectorsize - 1) / (8 * sectorsize);

	*data_offset_out = bitmap_offset + *bitmap_len_out;
	*data_offset_out = (*data_offset_out + sectorsize - 1) / sectorsize;
	*data_offset_out *= sectorsize;
}

static int read_cow_header(int fd, int *magic_out, char **backing_file_out, 
			   time_t *mtime_out, __u64 *size_out, 
			   int *sectorsize_out, int *bitmap_offset_out)
{
	union cow_header *header;
	char *file;
	int err, n;
	unsigned long version, magic;

	header = um_kmalloc(sizeof(*header));
	if(header == NULL){
		printk("read_cow_header - Failed to allocate header\n");
		return(-ENOMEM);
	}
	err = -EINVAL;
	n = read(fd, header, sizeof(*header));
	if(n < offsetof(typeof(header->v1), backing_file)){
		printk("read_cow_header - short header\n");
		goto out;
	}

	magic = header->v1.magic;
	if(magic == COW_MAGIC) {
		version = header->v1.version;
	}
	else if(magic == ntohl(COW_MAGIC)){
		version = ntohl(header->v1.version);
	}
	else goto out;

	*magic_out = COW_MAGIC;

	if(version == 1){
		if(n < sizeof(header->v1)){
			printk("read_cow_header - failed to read V1 header\n");
			goto out;
		}
		*mtime_out = header->v1.mtime;
		*size_out = header->v1.size;
		*sectorsize_out = header->v1.sectorsize;
		*bitmap_offset_out = sizeof(header->v1);
		file = header->v1.backing_file;
	}
	else if(version == 2){
		if(n < sizeof(header->v2)){
			printk("read_cow_header - failed to read V2 header\n");
			goto out;
		}
		*mtime_out = ntohl(header->v2.mtime);
		*size_out = ntohll(header->v2.size);
		*sectorsize_out = ntohl(header->v2.sectorsize);
		*bitmap_offset_out = sizeof(header->v2);
		file = header->v2.backing_file;
	}
	else {
		printk("read_cow_header - invalid COW version\n");
		goto out;
	}
	err = -ENOMEM;
	*backing_file_out = uml_strdup(file);
	if(*backing_file_out == NULL){
		printk("read_cow_header - failed to allocate backing file\n");
		goto out;
	}
	err = 0;
 out:
	kfree(header);
	return(err);
}

static int same_backing_files(char *from_cmdline, char *from_cow, char *cow)
{
	struct stat buf1, buf2;

	if(from_cmdline == NULL) return(1);
	if(!strcmp(from_cmdline, from_cow)) return(1);

	if(stat(from_cmdline, &buf1) < 0){
		printk("Couldn't stat '%s', errno = %d\n", from_cmdline, 
		       errno);
		return(1);
	}
	if(stat(from_cow, &buf2) < 0){
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

static int absolutize(char *to, int size, char *from)
{
	char save_cwd[256], *slash;
	int remaining;

	if(getcwd(save_cwd, sizeof(save_cwd)) == NULL) {
		printk("absolutize : unable to get cwd - errno = %d\n", errno);
		return(-1);
	}
	slash = strrchr(from, '/');
	if(slash != NULL){
		*slash = '\0';
		if(chdir(from)){
			*slash = '/';
			printk("absolutize : Can't cd to '%s' - errno = %d\n",
			       from, errno);
			return(-1);
		}
		*slash = '/';
		if(getcwd(to, size) == NULL){
			printk("absolutize : unable to get cwd of '%s' - "
			       "errno = %d\n", from, errno);
			return(-1);
		}
		remaining = size - strlen(to);
		if(strlen(slash) + 1 > remaining){
			printk("absolutize : unable to fit '%s' into %d "
			       "chars\n", from, size);
			return(-1);
		}
		strcat(to, slash);
	}
	else {
		if(strlen(save_cwd) + 1 + strlen(from) + 1 > size){
			printk("absolutize : unable to fit '%s' into %d "
			       "chars\n", from, size);
			return(-1);
		}
		strcpy(to, save_cwd);
		strcat(to, "/");
		strcat(to, from);
	}
	chdir(save_cwd);
	return(0);
}

static int write_cow_header(char *cow_file, int fd, char *backing_file, 
			    int sectorsize, long long *size)
{
        struct cow_header_v2 *header;
	struct stat64 buf;
	int err;

	err = os_seek_file(fd, 0);
	if(err != 0){
		printk("write_cow_header - lseek failed, errno = %d\n", errno);
		return(-errno);
	}

	err = -ENOMEM;
	header = um_kmalloc(sizeof(*header));
	if(header == NULL){
		printk("Failed to allocate COW V2 header\n");
		goto out;
	}
	header->magic = htonl(COW_MAGIC);
	header->version = htonl(COW_VERSION);

	err = -EINVAL;
	if(strlen(backing_file) > sizeof(header->backing_file) - 1){
		printk("Backing file name \"%s\" is too long - names are "
		       "limited to %d characters\n", backing_file, 
		       sizeof(header->backing_file) - 1);
		goto out_free;
	}

	if(absolutize(header->backing_file, sizeof(header->backing_file), 
		      backing_file))
		goto out_free;

	err = stat64(header->backing_file, &buf);
	if(err < 0){
		printk("Stat of backing file '%s' failed, errno = %d\n",
		       header->backing_file, errno);
		err = -errno;
		goto out_free;
	}

	err = os_file_size(header->backing_file, size);
	if(err){
		printk("Couldn't get size of backing file '%s', errno = %d\n",
		       header->backing_file, -*size);
		goto out_free;
	}

	header->mtime = htonl(buf.st_mtime);
	header->size = htonll(*size);
	header->sectorsize = htonl(sectorsize);

	err = write(fd, header, sizeof(*header));
	if(err != sizeof(*header)){
		printk("Write of header to new COW file '%s' failed, "
		       "errno = %d\n", cow_file, errno);
		goto out_free;
	}
	err = 0;
 out_free:
	kfree(header);
 out:
	return(err);
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
	if(backing_file_out == NULL) return(fd);

	err = read_cow_header(fd, &magic, &backing_file, &mtime, &size, 
			      &sectorsize, bitmap_offset_out);
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

	sizes(size, sectorsize, *bitmap_offset_out, bitmap_len_out, 
	      data_offset_out);

        return(fd);
 error:
	close(fd);
	return(err);
}

int create_cow_file(char *cow_file, char *backing_file, struct openflags flags,
		    int sectorsize, int *bitmap_offset_out, 
		    unsigned long *bitmap_len_out, int *data_offset_out)
{
	__u64 blocks;
	long zero;
	int err, fd, i;
	long long size;

	flags.c = 1;
	fd = open_ubd_file(cow_file, &flags, NULL, NULL, NULL, NULL, NULL);
	if(fd < 0){
		err = fd;
		printk("Open of COW file '%s' failed, errno = %d\n", cow_file,
		       -err);
		goto out;
	}

	err = write_cow_header(cow_file, fd, backing_file, sectorsize, &size);
	if(err) goto out_close;

	blocks = (size + sectorsize - 1) / sectorsize;
	blocks = (blocks + sizeof(long) * 8 - 1) / (sizeof(long) * 8);
	zero = 0;
	for(i = 0; i < blocks; i++){
		err = write(fd, &zero, sizeof(zero));
		if(err != sizeof(zero)){
			printk("Write of bitmap to new COW file '%s' failed, "
			       "errno = %d\n", cow_file, errno);
			goto out_close;
		}
	}

	sizes(size, sectorsize, sizeof(struct cow_header_v2), 
	      bitmap_len_out, data_offset_out);
	*bitmap_offset_out = sizeof(struct cow_header_v2);

	return(fd);

 out_close:
	close(fd);
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

int ubd_is_dir(char *file)
{
	struct stat64 buf;

	if(stat64(file, &buf) < 0) return(0);
	return(S_ISDIR(buf.st_mode));
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

/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __OS_H__
#define __OS_H__

#include "asm/types.h"
#include "../os/include/file.h"

#define OS_TYPE_FILE 1 
#define OS_TYPE_DIR 2 
#define OS_TYPE_SYMLINK 3 
#define OS_TYPE_CHARDEV 4
#define OS_TYPE_BLOCKDEV 5
#define OS_TYPE_FIFO 6
#define OS_TYPE_SOCK 7

struct openflags {
	unsigned int r : 1;
	unsigned int w : 1;
	unsigned int s : 1;	/* O_SYNC */
	unsigned int c : 1;	/* O_CREAT */
	unsigned int t : 1;	/* O_TRUNC */
	unsigned int a : 1;	/* O_APPEND */
	unsigned int e : 1;	/* O_EXCL */
	unsigned int cl : 1;    /* FD_CLOEXEC */
};

#define OPENFLAGS() ((struct openflags) { .r = 0, .w = 0, .s = 0, .c = 0, \
 					  .t = 0, .a = 0, .e = 0, .cl = 0 })

static inline struct openflags of_read(struct openflags flags)
{
	flags.r = 1; 
	return(flags);
}

static inline struct openflags of_write(struct openflags flags)
{
	flags.w = 1; 
	return(flags); 
}

static inline struct openflags of_rdwr(struct openflags flags)
{
	return(of_read(of_write(flags)));
}

static inline struct openflags of_set_rw(struct openflags flags, int r, int w)
{
	flags.r = r;
	flags.w = w;
	return(flags);
}

static inline struct openflags of_sync(struct openflags flags)
{ 
	flags.s = 1; 
	return(flags); 
}

static inline struct openflags of_create(struct openflags flags)
{ 
	flags.c = 1; 
	return(flags); 
}
 
static inline struct openflags of_trunc(struct openflags flags)
{ 
	flags.t = 1; 
	return(flags); 
}
 
static inline struct openflags of_append(struct openflags flags)
{ 
	flags.a = 1; 
	return(flags); 
}
 
static inline struct openflags of_excl(struct openflags flags)
{ 
	flags.e = 1; 
	return(flags); 
}
 
static inline struct openflags of_cloexec(struct openflags flags)
{ 
	flags.cl = 1; 
	return(flags); 
}
  
extern int os_seek_file(int fd, __u64 offset);
extern int os_open_file(char *file, struct openflags flags, int mode);
extern int os_read_file(int fd, void *buf, int len);
extern int os_write_file(int fd, void *buf, int count);
extern int os_file_size(char *file, long long *size_out);
extern int os_pipe(int *fd, int stream, int close_on_exec);
extern int os_set_fd_async(int fd, int owner);
extern int os_set_fd_block(int fd, int blocking);
extern int os_accept_connection(int fd);
extern int os_shutdown_socket(int fd, int r, int w);
extern void os_close_file(int fd);
extern int os_rcv_fd(int fd, int *helper_pid_out);
extern int create_unix_socket(char *file, int len);
extern int os_connect_socket(char *name);
extern int os_file_type(char *file);
extern int os_file_mode(char *file, struct openflags *mode_out);

extern unsigned long os_process_pc(int pid);
extern int os_process_parent(int pid);
extern void os_stop_process(int pid);
extern void os_kill_process(int pid, int reap_child);
extern void os_usr1_process(int pid);
extern int os_getpid(void);

extern int os_map_memory(void *virt, int fd, unsigned long off, 
			 unsigned long len, int r, int w, int x);
extern int os_protect_memory(void *addr, unsigned long len, 
			     int r, int w, int x);
extern int os_unmap_memory(void *addr, int len);

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

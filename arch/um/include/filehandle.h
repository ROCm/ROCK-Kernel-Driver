/* 
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __FILEHANDLE_H__
#define __FILEHANDLE_H__

#include "linux/list.h"
#include "linux/fs.h"
#include "os.h"

struct file_handle {
	struct list_head list;
	int fd;
	char *(*get_name)(struct inode *);
	struct inode *inode;
	struct openflags flags;
};

extern struct file_handle bad_filehandle;

extern int open_file(char *name, struct openflags flags, int mode);
extern void *open_dir(char *file);
extern int open_filehandle(char *name, struct openflags flags, int mode, 
			   struct file_handle *fh);
extern int read_file(struct file_handle *fh, unsigned long long offset, 
		     char *buf, int len);
extern int write_file(struct file_handle *fh, unsigned long long offset, 
		      const char *buf, int len);
extern int truncate_file(struct file_handle *fh, unsigned long long size);
extern int close_file(struct file_handle *fh);
extern void not_reclaimable(struct file_handle *fh);
extern void is_reclaimable(struct file_handle *fh, 
			   char *(name_proc)(struct inode *),
			   struct inode *inode);
extern int filehandle_fd(struct file_handle *fh);
extern int make_pipe(struct file_handle *fhs);

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

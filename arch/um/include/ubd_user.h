/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Copyright (C) 2001 RidgeRun, Inc (glonnon@ridgerun.com)
 * Licensed under the GPL
 */

#ifndef __UM_UBD_USER_H
#define __UM_UBD_USER_H

#include "os.h"

enum ubd_req { UBD_READ, UBD_WRITE };

struct io_thread_req {
	enum ubd_req op;
	int fds[2];
	unsigned long offsets[2];
	unsigned long long offset;
	unsigned long length;
	char *buffer;
	int sectorsize;
	unsigned long sector_mask;
	unsigned long cow_offset;
	unsigned long bitmap_words[2];
	int error;
};

extern int open_ubd_file(char *file, struct openflags *openflags, 
			 char **backing_file_out, int *bitmap_offset_out, 
			 unsigned long *bitmap_len_out, int *data_offset_out,
			 int *create_cow_out);
extern int create_cow_file(char *cow_file, char *backing_file, 
			   struct openflags flags, int sectorsize, 
			   int *bitmap_offset_out, 
			   unsigned long *bitmap_len_out,
			   int *data_offset_out);
extern int read_cow_bitmap(int fd, void *buf, int offset, int len);
extern int read_ubd_fs(int fd, void *buffer, int len);
extern int write_ubd_fs(int fd, char *buffer, int len);
extern int start_io_thread(unsigned long sp, int *fds_out);
extern void do_io(struct io_thread_req *req);
extern int ubd_is_dir(char *file);

static inline int ubd_test_bit(__u64 bit, unsigned char *data)
{
	__u64 n;
	int bits, off;

	bits = sizeof(data[0]) * 8;
	n = bit / bits;
	off = bit % bits;
	return((data[n] & (1 << off)) != 0);
}

static inline void ubd_set_bit(__u64 bit, unsigned char *data)
{
	__u64 n;
	int bits, off;

	bits = sizeof(data[0]) * 8;
	n = bit / bits;
	off = bit % bits;
	data[n] |= (1 << off);
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

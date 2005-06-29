/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __READAHEAD_H__
#define __READAHEAD_H__

#include "key.h"

typedef enum {
	RA_ADJACENT_ONLY = 1,       /* only requests nodes which are adjacent. Default is NO (not only adjacent) */
} ra_global_flags;

/* reiser4 super block has a field of this type. It controls readahead during tree traversals */
typedef struct formatted_read_ahead_params {
	unsigned long max; /* request not more than this amount of nodes. Default is totalram_pages / 4 */
	int flags;
} ra_params_t;


typedef struct {
	reiser4_key key_to_stop;
} ra_info_t;

void formatted_readahead(znode *, ra_info_t *);
void init_ra_info(ra_info_t * rai);

struct reiser4_file_ra_state {
	loff_t  start;		/* Current window */
	loff_t  size;
	loff_t  next_size;	/* Next window size */
	loff_t  ahead_start;	/* Ahead window */
	loff_t  ahead_size;
	loff_t  max_window_size; /* Maximum readahead window */
	loff_t  slow_start;      /* enlarging r/a size algorithm. */
};

extern int reiser4_file_readahead(struct file *, loff_t, size_t);
extern void reiser4_readdir_readahead_init(struct inode *dir, tap_t *tap);

/* __READAHEAD_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/

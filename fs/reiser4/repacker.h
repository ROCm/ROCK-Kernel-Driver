/* Copyright 2003 by Hans Reiser */

#ifndef __FS_REISER4_REPACKER_H__
#define __FS_REISER4_REPACKER_H__

/* Repacker per tread state and statistics. */
struct repacker_cursor {
	reiser4_blocknr_hint hint;
	int count;
	struct  {
		long znodes_dirtied;
		long jnodes_dirtied;
	} stats;
};

extern int  init_reiser4_repacker(struct super_block *);
extern void done_reiser4_repacker(struct super_block *);

extern int reiser4_repacker (struct repacker * repacker);
extern int repacker_d(void *arg);

#endif /* __FS_REISER4_REPACKER_H__ */

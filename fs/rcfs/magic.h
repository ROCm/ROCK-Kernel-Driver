#ifndef _LINUX_RCFS_MAGIC_H
#define _LINUX_RCFS_MAGIC_H

#include <linux/fs.h>
#include <linux/dcache.h>

#define MAGF_NAMELEN 20

struct magf_t {
	char name[MAGF_NAMELEN];
	int mode;
	struct inode_operations *i_op;
	struct file_operations *i_fop;
};


/* Simpler to index by enum and initialize directly */



enum MAGF_IDX {
	MAGF_TARGET = 0,
	MAGF_SHARES,
	MAGF_STATS,
	MAGF_CONFIG,
	MAGF_MEMBERS,
	NR_MAGF,	// always the last. Number of entries.
};

extern struct magf_t magf[NR_MAGF];
extern int RCFS_IS_MAGIC;


int rcfs_create_magic(struct dentry *parent, struct magf_t *magf);
int rcfs_delete_all_magic(struct dentry *parent);


#define rcfs_is_magic(dentry)  ((dentry)->d_fsdata == &RCFS_IS_MAGIC)



#endif /* _LINUX_RCFS_MAGIC_H */

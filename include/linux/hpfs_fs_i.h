#ifndef _HPFS_FS_I
#define _HPFS_FS_I

struct hpfs_inode_info {
	loff_t mmu_private;
	ino_t i_parent_dir;	/* (directories) gives fnode of parent dir */
	unsigned i_dno;		/* (directories) root dnode */
	unsigned i_dpos;	/* (directories) temp for readdir */
	unsigned i_dsubdno;	/* (directories) temp for readdir */
	unsigned i_file_sec;	/* (files) minimalist cache of alloc info */
	unsigned i_disk_sec;	/* (files) minimalist cache of alloc info */
	unsigned i_n_secs;	/* (files) minimalist cache of alloc info */
	unsigned i_ea_size;	/* size of extended attributes */
	unsigned i_conv : 2;	/* (files) crlf->newline hackery */
	unsigned i_ea_mode : 1;	/* file's permission is stored in ea */
	unsigned i_ea_uid : 1;	/* file's uid is stored in ea */
	unsigned i_ea_gid : 1;	/* file's gid is stored in ea */
	unsigned i_dirty : 1;
	struct semaphore i_sem;	/* semaphore */
	loff_t **i_rddir_off;
	struct inode vfs_inode;
};

#endif

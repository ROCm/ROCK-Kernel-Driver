#ifndef _HPFS_FS_SB
#define _HPFS_FS_SB

struct hpfs_sb_info {
	ino_t sb_root;			/* inode number of root dir */
	unsigned sb_fs_size;		/* file system size, sectors */
	unsigned sb_bitmaps;		/* sector number of bitmap list */
	unsigned sb_dirband_start;	/* directory band start sector */
	unsigned sb_dirband_size;	/* directory band size, dnodes */
	unsigned sb_dmap;		/* sector number of dnode bit map */
	unsigned sb_n_free;		/* free blocks for statfs, or -1 */
	unsigned sb_n_free_dnodes;	/* free dnodes for statfs, or -1 */
	uid_t sb_uid;			/* uid from mount options */
	gid_t sb_gid;			/* gid from mount options */
	umode_t sb_mode;		/* mode from mount options */
	unsigned sb_conv : 2;		/* crlf->newline hackery */
	unsigned sb_eas : 2;		/* eas: 0-ignore, 1-ro, 2-rw */
	unsigned sb_err : 2;		/* on errs: 0-cont, 1-ro, 2-panic */
	unsigned sb_chk : 2;		/* checks: 0-no, 1-normal, 2-strict */
	unsigned sb_lowercase : 1;	/* downcase filenames hackery */
	unsigned sb_was_error : 1;	/* there was an error, set dirty flag */
	unsigned sb_chkdsk : 2;		/* chkdsk: 0-no, 1-on errs, 2-allways */
	unsigned sb_rd_fnode : 2;	/* read fnode 0-no 1-dirs 2-all */
	unsigned sb_rd_inode : 2;	/* lookup tells read_inode: 1-read fnode
					   2-don't read fnode, file
					   3-don't read fnode, direcotry */
	wait_queue_head_t sb_iget_q;
	unsigned char *sb_cp_table;	/* code page tables: */
					/* 	128 bytes uppercasing table & */
					/*	128 bytes lowercasing table */
	unsigned *sb_bmp_dir;		/* main bitmap directory */
	unsigned sb_c_bitmap;		/* current bitmap */
	struct semaphore hpfs_creation_de; /* when creating dirents, nobody else
					   can alloc blocks */
	/*unsigned sb_mounting : 1;*/
	int sb_timeshift;
};

#endif

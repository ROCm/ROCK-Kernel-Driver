/*
 *  linux/fs/sysv/ialloc.c
 *
 *  minix/bitmap.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext/freelists.c
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *
 *  xenix/alloc.c
 *  Copyright (C) 1992  Doug Evans
 *
 *  coh/alloc.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/ialloc.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  This file contains code for allocating/freeing inodes.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/stddef.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>

/* We don't trust the value of
   sb->sv_sbd2->s_tinode = *sb->sv_sb_total_free_inodes
   but we nevertheless keep it up to date. */

/* An inode on disk is considered free if both i_mode == 0 and i_nlink == 0. */

/* return &sb->sv_sb_fic_inodes[i] = &sbd->s_inode[i]; */
static inline sysv_ino_t * sv_sb_fic_inode (struct super_block * sb, unsigned int i)
{
	if (sb->sv_bh1 == sb->sv_bh2)
		return &sb->sv_sb_fic_inodes[i];
	else {
		/* 512 byte Xenix FS */
		unsigned int offset = offsetof(struct xenix_super_block, s_inode[i]);
		if (offset < 512)
			return (sysv_ino_t*)(sb->sv_sbd1 + offset);
		else
			return (sysv_ino_t*)(sb->sv_sbd2 + offset);
	}
}

void sysv_free_inode(struct inode * inode)
{
	struct super_block * sb;
	unsigned int ino;
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;

	sb = inode->i_sb;
	ino = inode->i_ino;
	if (ino <= SYSV_ROOT_INO || ino > sb->sv_ninodes) {
		printk("sysv_free_inode: inode 0,1,2 or nonexistent inode\n");
		return;
	}
	if (!(bh = sv_bread(sb, inode->i_dev, sb->sv_firstinodezone + ((ino-1) >> sb->sv_inodes_per_block_bits)))) {
		printk("sysv_free_inode: unable to read inode block on device "
		       "%s\n", kdevname(inode->i_dev));
		clear_inode(inode);
		return;
	}
	raw_inode = (struct sysv_inode *) bh->b_data + ((ino-1) & sb->sv_inodes_per_block_1);
	clear_inode(inode);
	lock_super(sb);
	if (*sb->sv_sb_fic_count < sb->sv_fic_size)
		*sv_sb_fic_inode(sb,(*sb->sv_sb_fic_count)++) = ino;
	(*sb->sv_sb_total_free_inodes)++;
	mark_buffer_dirty(sb->sv_bh1); /* super-block has been modified */
	if (sb->sv_bh1 != sb->sv_bh2) mark_buffer_dirty(sb->sv_bh2);
	sb->s_dirt = 1; /* and needs time stamp */
	memset(raw_inode, 0, sizeof(struct sysv_inode));
	mark_buffer_dirty(bh);
	unlock_super(sb);
	brelse(bh);
}

struct inode * sysv_new_inode(const struct inode * dir)
{
	struct inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;
	int i,j,ino,block;

	if (!dir)
		return NULL;
	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return NULL;
	lock_super(sb);		/* protect against task switches */
	if ((*sb->sv_sb_fic_count == 0)
	    || (*sv_sb_fic_inode(sb,(*sb->sv_sb_fic_count)-1) == 0) /* Applies only to SystemV2 FS */
	   ) {
		/* Rebuild cache of free inodes: */
		/* i : index into cache slot being filled	     */
		/* ino : inode we are trying			     */
		/* block : firstinodezone + (ino-1)/inodes_per_block */
		/* j : (ino-1)%inodes_per_block			     */
		/* bh : buffer for block			     */
		/* raw_inode : pointer to inode ino in the block     */
		for (i = 0, ino = SYSV_ROOT_INO+1, block = sb->sv_firstinodezone, j = SYSV_ROOT_INO ; i < sb->sv_fic_size && block < sb->sv_firstdatazone ; block++, j = 0) {
			if (!(bh = sv_bread(sb, sb->s_dev, block))) {
				printk("sysv_new_inode: unable to read inode table\n");
				break;	/* go with what we've got */
				/* FIXME: Perhaps try the next block? */
			}
			raw_inode = (struct sysv_inode *) bh->b_data + j;
			for (; j < sb->sv_inodes_per_block && i < sb->sv_fic_size; ino++, j++, raw_inode++) {
				if (raw_inode->i_mode == 0 && raw_inode->i_nlink == 0)
					*sv_sb_fic_inode(sb,i++) = ino;
			}
			brelse(bh);
		}
		if (i == 0) {
			iput(inode);
			unlock_super(sb);
			return NULL;	/* no inodes available */
		}
		*sb->sv_sb_fic_count = i;
	}
	/* Now *sb->sv_sb_fic_count > 0. */
	ino = *sv_sb_fic_inode(sb,--(*sb->sv_sb_fic_count));
	mark_buffer_dirty(sb->sv_bh1); /* super-block has been modified */
	if (sb->sv_bh1 != sb->sv_bh2) mark_buffer_dirty(sb->sv_bh2);
	sb->s_dirt = 1; /* and needs time stamp */
	inode->i_uid = current->fsuid;
	inode->i_gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	inode->i_ino = ino;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = inode->i_blksize = 0;
	insert_inode_hash(inode);
	mark_inode_dirty(inode);
	/* Change directory entry: */
	inode->i_mode = 0;		/* for sysv_write_inode() */
	sysv_write_inode(inode, 0);	/* ensure inode not allocated again */
					/* FIXME: caller may call this too. */
	mark_inode_dirty(inode);	/* cleared by sysv_write_inode() */
	/* That's it. */
	(*sb->sv_sb_total_free_inodes)--;
	mark_buffer_dirty(sb->sv_bh2); /* super-block has been modified again */
	sb->s_dirt = 1; /* and needs time stamp again */
	unlock_super(sb);
	return inode;
}

unsigned long sysv_count_free_inodes(struct super_block * sb)
{
#if 1 /* test */
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;
	int j,block,count;

	/* this causes a lot of disk traffic ... */
	count = 0;
	lock_super(sb);
	/* i : index into cache slot being filled	     */
	/* ino : inode we are trying			     */
	/* block : firstinodezone + (ino-1)/inodes_per_block */
	/* j : (ino-1)%inodes_per_block			     */
	/* bh : buffer for block			     */
	/* raw_inode : pointer to inode ino in the block     */
	for (block = sb->sv_firstinodezone, j = SYSV_ROOT_INO ; block < sb->sv_firstdatazone ; block++, j = 0) {
		if (!(bh = sv_bread(sb, sb->s_dev, block))) {
			printk("sysv_count_free_inodes: unable to read inode table\n");
			break;	/* go with what we've got */
			/* FIXME: Perhaps try the next block? */
		}
		raw_inode = (struct sysv_inode *) bh->b_data + j;
		for (; j < sb->sv_inodes_per_block ; j++, raw_inode++)
			if (raw_inode->i_mode == 0 && raw_inode->i_nlink == 0)
				count++;
		brelse(bh);
	}
	if (count != *sb->sv_sb_total_free_inodes) {
		printk("sysv_count_free_inodes: free inode count was %d, correcting to %d\n",(short)(*sb->sv_sb_total_free_inodes),count);
		if (!(sb->s_flags & MS_RDONLY)) {
			*sb->sv_sb_total_free_inodes = count;
			mark_buffer_dirty(sb->sv_bh2); /* super-block has been modified */
			sb->s_dirt = 1; /* and needs time stamp */
		}
	}
	unlock_super(sb);
	return count;
#else
	return *sb->sv_sb_total_free_inodes;
#endif
}


/*
 *  linux/fs/sysv/inode.c
 *
 *  minix/inode.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  xenix/inode.c
 *  Copyright (C) 1992  Doug Evans
 *
 *  coh/inode.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/inode.c
 *  Copyright (C) 1993  Paul B. Monday
 *
 *  sysv/inode.c
 *  Copyright (C) 1993  Bruno Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Baranowski
 *
 *  This file contains code for allocating/freeing inodes and for read/writing
 *  the superblock.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/highuid.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#if 0
void sysv_print_inode(struct inode * inode)
{
        printk("ino %lu  mode 0%6.6o  lk %d  uid %d  gid %d"
               "  sz %lu  blks %lu  cnt %u\n",
               inode->i_ino, inode->i_mode, inode->i_nlink, inode->i_uid,
               inode->i_gid, inode->i_size, inode->i_blocks,
               atomic_read(&inode->i_count));
        printk("  db <0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx"
               " 0x%lx 0x%lx>\n",
                inode->u.sysv_i.i_data[0], inode->u.sysv_i.i_data[1],
                inode->u.sysv_i.i_data[2], inode->u.sysv_i.i_data[3],
                inode->u.sysv_i.i_data[4], inode->u.sysv_i.i_data[5],
                inode->u.sysv_i.i_data[6], inode->u.sysv_i.i_data[7],
                inode->u.sysv_i.i_data[8], inode->u.sysv_i.i_data[9]);
        printk("  ib <0x%lx 0x%lx 0x%lx>\n",
                inode->u.sysv_i.i_data[10],
                inode->u.sysv_i.i_data[11],
                inode->u.sysv_i.i_data[12]);
}
#endif

static void sysv_delete_inode(struct inode *inode)
{
	lock_kernel();
	inode->i_size = 0;
	sysv_truncate(inode);
	sysv_free_inode(inode);
	unlock_kernel();
}

static void sysv_put_super(struct super_block *);
static void sysv_write_super(struct super_block *);
static void sysv_read_inode(struct inode *);
static int sysv_statfs(struct super_block *, struct statfs *);

static struct super_operations sysv_sops = {
	read_inode:	sysv_read_inode,
	write_inode:	sysv_write_inode,
	delete_inode:	sysv_delete_inode,
	put_super:	sysv_put_super,
	write_super:	sysv_write_super,
	statfs:		sysv_statfs,
};

/* The following functions try to recognize specific filesystems.
 * We recognize:
 * - Xenix FS by its magic number.
 * - SystemV FS by its magic number.
 * - Coherent FS by its funny fname/fpack field.
 * We discriminate among SystemV4 and SystemV2 FS by the assumption that
 * the time stamp is not < 01-01-1980.
 */

static void detected_bs (u_char type, struct super_block *sb)
{
	u_char n_bits = type+8;
	int bsize = 1 << n_bits;
	int bsize_4 = bsize >> 2;
	
	sb->sv_block_size = bsize;
	sb->sv_block_size_1 = bsize-1;
	sb->sv_block_size_bits = n_bits;
	sb->sv_block_size_dec_bits = (bsize==512) ? 1 : 0;
	sb->sv_block_size_inc_bits = (bsize==2048) ? 1 : 0;
	sb->sv_inodes_per_block = bsize >> 6;
	sb->sv_inodes_per_block_1 = (bsize >> 6)-1;
	sb->sv_inodes_per_block_bits = n_bits-6;
	sb->sv_toobig_block = 10 +
	  (sb->sv_ind_per_block = bsize_4) +
	  (sb->sv_ind_per_block_2 = bsize_4*bsize_4) +
	  (sb->sv_ind_per_block_3 = bsize_4*bsize_4*bsize_4);
	sb->sv_ind_per_block_1 = bsize_4-1;
	sb->sv_ind_per_block_2_1 = bsize_4*bsize_4-1;
	sb->sv_ind_per_block_2_bits = 2 *
	  (sb->sv_ind_per_block_bits = n_bits-2);
	sb->sv_ind_per_block_block_size_1 = bsize_4*bsize-1;
	sb->sv_ind_per_block_block_size_bits = 2*n_bits-2;
	sb->sv_ind_per_block_2_block_size_1 = bsize_4*bsize_4*bsize-1;
	sb->sv_ind_per_block_2_block_size_bits = 3*n_bits-4;
	sb->sv_ind0_size = 10 * bsize;
	sb->sv_ind1_size = (10 + bsize_4)* bsize;
	sb->sv_ind2_size = (10 + bsize_4 + bsize_4*bsize_4) * bsize;
}

static const char* detect_xenix (struct super_block *sb, struct buffer_head *bh)
{
	struct xenix_super_block * sbd;

	sbd = (struct xenix_super_block *) bh->b_data;
	if (sbd->s_magic != 0x2b5544)
		return NULL;
	if (sbd->s_type > 2 || sbd->s_type < 1)
		return NULL;
	detected_bs(sbd->s_type, sb);
	sb->sv_type = FSTYPE_XENIX;
	return "Xenix";
}
static struct super_block * detected_xenix (struct super_block *sb, struct buffer_head *bh1, struct buffer_head *bh2)
{
	struct xenix_super_block * sbd1;
	struct xenix_super_block * sbd2;

	if (sb->sv_block_size >= BLOCK_SIZE)
		/* block size >= 1024, so bh1 = bh2 */
		sbd1 = sbd2 = (struct xenix_super_block *) bh1->b_data;
	else {
		/* block size = 512, so bh1 != bh2 */
		sbd1 = (struct xenix_super_block *) bh1->b_data;
		sbd2 = (struct xenix_super_block *) (bh2->b_data - BLOCK_SIZE/2);
		/* sanity check */
		if (sbd2->s_magic != 0x2b5544)
			return NULL;
	}

	sb->sv_convert = 0;
	sb->sv_kludge_symlinks = 1;
	sb->sv_truncate = 1;
	sb->sv_link_max = XENIX_LINK_MAX;
	sb->sv_fic_size = XENIX_NICINOD;
	sb->sv_flc_size = XENIX_NICFREE;
	sb->sv_bh1 = bh1;
	sb->sv_bh2 = bh2;
	sb->sv_sbd1 = (char *) sbd1;
	sb->sv_sbd2 = (char *) sbd2;
	sb->sv_sb_fic_count = &sbd1->s_ninode;
	sb->sv_sb_fic_inodes = &sbd1->s_inode[0];
	sb->sv_sb_total_free_inodes = &sbd2->s_tinode;
	sb->sv_sb_flc_count = &sbd1->s_nfree;
	sb->sv_sb_flc_blocks = &sbd1->s_free[0];
	sb->sv_sb_total_free_blocks = &sbd2->s_tfree;
	sb->sv_sb_time = &sbd2->s_time;
	sb->sv_firstinodezone = 2;
	sb->sv_firstdatazone = sbd1->s_isize;
	sb->sv_nzones = sbd1->s_fsize;
	sb->sv_ndatazones = sb->sv_nzones - sb->sv_firstdatazone;
	return sb;
}

static const char* detect_sysv4 (struct super_block *sb, struct buffer_head *bh)
{
	struct sysv4_super_block * sbd;

	sbd = (struct sysv4_super_block *) (bh->b_data + BLOCK_SIZE/2);
	if (sbd->s_magic != 0xfd187e20)
		return NULL;
	if (sbd->s_time < 315532800) /* this is likely to happen on SystemV2 FS */
		return NULL;
	if ((sbd->s_type > 3 || sbd->s_type < 1) && (sbd->s_type > 0x30 || sbd->s_type < 0x10))
		return NULL;

	/* On Interactive Unix (ISC) Version 4.0/3.x s_type field = 0x10,
	   0x20 or 0x30 indicates that symbolic links and the 14-character
	   filename limit is gone. Due to lack of information about this
           feature read-only mode seems to be a reasonable approach... -KGB */

	if (sbd->s_type >= 0x10) {
		printk("SysV FS: can't handle long file names on %s, "
		       "forcing read-only mode.\n", kdevname(sb->s_dev));
		sb->s_flags |= MS_RDONLY;
	}

	detected_bs(sbd->s_type >= 0x10 ? (sbd->s_type >> 4) : sbd->s_type, sb);
	sb->sv_type = FSTYPE_SYSV4;
	return "SystemV";
}

static struct super_block * detected_sysv4 (struct super_block *sb, struct buffer_head *bh)
{
	struct sysv4_super_block * sbd;

	if (sb->sv_block_size >= BLOCK_SIZE)
		sbd = (struct sysv4_super_block *) (bh->b_data + BLOCK_SIZE/2);
	else {
		sbd = (struct sysv4_super_block *) bh->b_data;
		/* sanity check */
		if (sbd->s_magic != 0xfd187e20)
			return NULL;
		if (sbd->s_time < 315532800)
			return NULL;
	}

	sb->sv_convert = 0;
	sb->sv_kludge_symlinks = 0; /* ?? */
	sb->sv_truncate = 1;
	sb->sv_link_max = SYSV_LINK_MAX;
	sb->sv_fic_size = SYSV_NICINOD;
	sb->sv_flc_size = SYSV_NICFREE;
	sb->sv_bh1 = bh;
	sb->sv_bh2 = bh;
	sb->sv_sbd1 = (char *) sbd;
	sb->sv_sbd2 = (char *) sbd;
	sb->sv_sb_fic_count = &sbd->s_ninode;
	sb->sv_sb_fic_inodes = &sbd->s_inode[0];
	sb->sv_sb_total_free_inodes = &sbd->s_tinode;
	sb->sv_sb_flc_count = &sbd->s_nfree;
	sb->sv_sb_flc_blocks = &sbd->s_free[0];
	sb->sv_sb_total_free_blocks = &sbd->s_tfree;
	sb->sv_sb_time = &sbd->s_time;
	sb->sv_sb_state = &sbd->s_state;
	sb->sv_firstinodezone = 2;
	sb->sv_firstdatazone = sbd->s_isize;
	sb->sv_nzones = sbd->s_fsize;
	sb->sv_ndatazones = sb->sv_nzones - sb->sv_firstdatazone;
	return sb;
}

static const char* detect_sysv2 (struct super_block *sb, struct buffer_head *bh)
{
	struct sysv2_super_block * sbd;

	sbd = (struct sysv2_super_block *) (bh->b_data + BLOCK_SIZE/2);
	if (sbd->s_magic != 0xfd187e20)
		return NULL;
	if (sbd->s_time < 315532800) /* this is likely to happen on SystemV4 FS */
		return NULL;
	if (sbd->s_type > 3 || sbd->s_type < 1)
		return NULL;
	detected_bs(sbd->s_type, sb);
	sb->sv_type = FSTYPE_SYSV2;
	return "SystemV Release 2";
}
static struct super_block * detected_sysv2 (struct super_block *sb, struct buffer_head *bh)
{
	struct sysv2_super_block * sbd;

	if (sb->sv_block_size >= BLOCK_SIZE)
		sbd = (struct sysv2_super_block *) (bh->b_data + BLOCK_SIZE/2);
	else {
		sbd = (struct sysv2_super_block *) bh->b_data;
		/* sanity check */
		if (sbd->s_magic != 0xfd187e20)
			return NULL;
		if (sbd->s_time < 315532800)
			return NULL;
	}

	sb->sv_convert = 0;
	sb->sv_kludge_symlinks = 0; /* ?? */
	sb->sv_truncate = 1;
	sb->sv_link_max = SYSV_LINK_MAX;
	sb->sv_fic_size = SYSV_NICINOD;
	sb->sv_flc_size = SYSV_NICFREE;
	sb->sv_bh1 = bh;
	sb->sv_bh2 = bh;
	sb->sv_sbd1 = (char *) sbd;
	sb->sv_sbd2 = (char *) sbd;
	sb->sv_sb_fic_count = &sbd->s_ninode;
	sb->sv_sb_fic_inodes = &sbd->s_inode[0];
	sb->sv_sb_total_free_inodes = &sbd->s_tinode;
	sb->sv_sb_flc_count = &sbd->s_nfree;
	sb->sv_sb_flc_blocks = &sbd->s_free[0];
	sb->sv_sb_total_free_blocks = &sbd->s_tfree;
	sb->sv_sb_time = &sbd->s_time;
	sb->sv_sb_state = &sbd->s_state;
	sb->sv_firstinodezone = 2;
	sb->sv_firstdatazone = sbd->s_isize;
	sb->sv_nzones = sbd->s_fsize;
	sb->sv_ndatazones = sb->sv_nzones - sb->sv_firstdatazone;
	return sb;
}

static const char* detect_coherent (struct super_block *sb, struct buffer_head *bh)
{
	struct coh_super_block * sbd;

	sbd = (struct coh_super_block *) (bh->b_data + BLOCK_SIZE/2);
	if ((memcmp(sbd->s_fname,"noname",6) && memcmp(sbd->s_fname,"xxxxx ",6))
	    || (memcmp(sbd->s_fpack,"nopack",6) && memcmp(sbd->s_fpack,"xxxxx\n",6)))
		return NULL;
	detected_bs(1, sb);
	sb->sv_type = FSTYPE_COH;
	return "Coherent";
}
static struct super_block * detected_coherent (struct super_block *sb, struct buffer_head *bh)
{
	struct coh_super_block * sbd;

	sbd = (struct coh_super_block *) bh->b_data;
	/* sanity check */
	if ((memcmp(sbd->s_fname,"noname",6) && memcmp(sbd->s_fname,"xxxxx ",6))
	    || (memcmp(sbd->s_fpack,"nopack",6) && memcmp(sbd->s_fpack,"xxxxx\n",6)))
		return NULL;

	sb->sv_convert = 1;
	sb->sv_kludge_symlinks = 1;
	sb->sv_truncate = 1;
	sb->sv_link_max = COH_LINK_MAX;
	sb->sv_fic_size = COH_NICINOD;
	sb->sv_flc_size = COH_NICFREE;
	sb->sv_bh1 = bh;
	sb->sv_bh2 = bh;
	sb->sv_sbd1 = (char *) sbd;
	sb->sv_sbd2 = (char *) sbd;
	sb->sv_sb_fic_count = &sbd->s_ninode;
	sb->sv_sb_fic_inodes = &sbd->s_inode[0];
	sb->sv_sb_total_free_inodes = &sbd->s_tinode;
	sb->sv_sb_flc_count = &sbd->s_nfree;
	sb->sv_sb_flc_blocks = &sbd->s_free[0];
	sb->sv_sb_total_free_blocks = &sbd->s_tfree;
	sb->sv_sb_time = &sbd->s_time;
	sb->sv_firstinodezone = 2;
	sb->sv_firstdatazone = sbd->s_isize;
	sb->sv_nzones = from_coh_ulong(sbd->s_fsize);
	sb->sv_ndatazones = sb->sv_nzones - sb->sv_firstdatazone;
	return sb;
}

static struct super_block *sysv_read_super(struct super_block *sb,
					   void *data, int silent)
{
	struct buffer_head *bh;
	const char *found;
	kdev_t dev = sb->s_dev;
	struct inode *root_inode;
	unsigned long blocknr;
	
	if (1024 != sizeof (struct xenix_super_block))
		panic("Xenix FS: bad super-block size");
	if ((512 != sizeof (struct sysv4_super_block))
            || (512 != sizeof (struct sysv2_super_block)))
		panic("SystemV FS: bad super-block size");
	if (500 != sizeof (struct coh_super_block))
		panic("Coherent FS: bad super-block size");
	if (64 != sizeof (struct sysv_inode))
		panic("sysv fs: bad i-node size");
	set_blocksize(dev,BLOCK_SIZE);
	sb->sv_block_base = 0;

	/* Try to read Xenix superblock */
	if ((bh = bread(dev, 1, BLOCK_SIZE)) != NULL) {
		if ((found = detect_xenix(sb,bh)) != NULL)
			goto ok;
		brelse(bh);
	}
	if ((bh = bread(dev, 0, BLOCK_SIZE)) != NULL) {
		/* Try to recognize SystemV superblock */
		if ((found = detect_sysv4(sb,bh)) != NULL)
			goto ok;
		if ((found = detect_sysv2(sb,bh)) != NULL)
			goto ok;
		/* Try to recognize Coherent superblock */
		if ((found = detect_coherent(sb,bh)) != NULL)
			goto ok;
		brelse(bh);
	}
	/* Try to recognize SystemV superblock */
	/* Offset by 1 track, i.e. most probably 9, 15, or 18 kilobytes. */
	/* 2kB blocks with offset of 9 and 15 kilobytes are not supported. */
	/* Maybe we should also check the device geometry ? */
	{	static int offsets[] = { 9, 15, 18, };
		int i;
		for (i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++)
			if ((bh = bread(dev, offsets[i], BLOCK_SIZE)) != NULL) {
				/* Try to recognize SystemV superblock */
				if ((found = detect_sysv4(sb,bh)) != NULL) {
					if (sb->sv_block_size>BLOCK_SIZE && (offsets[i] % 2))
						goto bad_shift;
					sb->sv_block_base = (offsets[i] << sb->sv_block_size_dec_bits) >> sb->sv_block_size_inc_bits;
					goto ok;
				}
				if ((found = detect_sysv2(sb,bh)) != NULL) {
					if (sb->sv_block_size>BLOCK_SIZE && (offsets[i] % 2))
						goto bad_shift;
					sb->sv_block_base = (offsets[i] << sb->sv_block_size_dec_bits) >> sb->sv_block_size_inc_bits;
					goto ok;
				}
				brelse(bh);
			}
	}
	bad_shift:
	if (!silent)
		printk("VFS: unable to read Xenix/SystemV/Coherent superblock on device "
		       "%s\n", kdevname(dev));
	failed:
	return NULL;

	ok:
	if (sb->sv_block_size >= BLOCK_SIZE) {
		if (sb->sv_block_size != BLOCK_SIZE) {
			brelse(bh);
			set_blocksize(dev, sb->sv_block_size);
			blocknr = (bh->b_blocknr << sb->sv_block_size_dec_bits) >> sb->sv_block_size_inc_bits;
			if ((bh = bread(dev, blocknr, sb->sv_block_size)) == NULL)
				goto bad_superblock;
		}
		switch (sb->sv_type) {
			case FSTYPE_XENIX:
				if (!detected_xenix(sb,bh,bh))
					goto bad_superblock;
				break;
			case FSTYPE_SYSV4:
				if (!detected_sysv4(sb,bh))
					goto bad_superblock;
				break;
			case FSTYPE_SYSV2:
				if (!detected_sysv2(sb,bh))
					goto bad_superblock;
				break;
			default: goto bad_superblock;
		goto superblock_ok;
		bad_superblock:
			brelse(bh);
			printk("SysV FS: cannot read superblock in %d byte mode\n", sb->sv_block_size);
			goto failed;
		superblock_ok:
		}
	} else {
		/* Switch to 512 block size. Unfortunately, we have to
		   release the block bh and read it again. */
		struct buffer_head *bh1, *bh2;
		unsigned long blocknr = (bh->b_blocknr << sb->sv_block_size_dec_bits) >> sb->sv_block_size_inc_bits;

		brelse(bh);
		set_blocksize(dev,sb->sv_block_size);
		bh1 = NULL; bh2 = NULL;
		switch (sb->sv_type) {
			case FSTYPE_XENIX:
				if ((bh1 = bread(dev, blocknr, sb->sv_block_size)) == NULL)
					goto bad_superblock2;
				if ((bh2 = bread(dev, blocknr+1, sb->sv_block_size)) == NULL)
					goto bad_superblock2;
				if (!detected_xenix(sb,bh1,bh2))
					goto bad_superblock2;
				break;
			case FSTYPE_SYSV4:
				if ((bh2 = bread(dev, blocknr+1, sb->sv_block_size)) == NULL)
					goto bad_superblock2;
				if (!detected_sysv4(sb,bh2))
					goto bad_superblock2;
				break;
			case FSTYPE_SYSV2:
				if ((bh2 = bread(dev, blocknr+1, sb->sv_block_size)) == NULL)
					goto bad_superblock2;
				if (!detected_sysv2(sb,bh2))
					goto bad_superblock2;
				break;
			case FSTYPE_COH:
				if ((bh2 = bread(dev, blocknr+1, sb->sv_block_size)) == NULL)
					goto bad_superblock2;
				if (!detected_coherent(sb,bh2))
					goto bad_superblock2;
				break;
			default:
			bad_superblock2:
				brelse(bh1);
				brelse(bh2);
				set_blocksize(sb->s_dev,BLOCK_SIZE);
				printk("SysV FS: cannot read superblock in 512 byte mode\n");
				goto failed;
		}
	}
	sb->sv_ninodes = (sb->sv_firstdatazone - sb->sv_firstinodezone) << sb->sv_inodes_per_block_bits;
	if (!silent)
		printk("VFS: Found a %s FS (block size = %d) on device %s\n",
		       found, sb->sv_block_size, kdevname(dev));
	sb->s_magic = SYSV_MAGIC_BASE + sb->sv_type;
	/* The buffer code now supports block size 512 as well as 1024. */
	sb->s_blocksize = sb->sv_block_size;
	sb->s_blocksize_bits = sb->sv_block_size_bits;
	/* set up enough so that it can read an inode */
	sb->s_op = &sysv_sops;
	root_inode = iget(sb,SYSV_ROOT_INO);
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root) {
		printk("SysV FS: get root inode failed\n");
		sysv_put_super(sb);
		return NULL;
	}
#ifndef CONFIG_SYSV_FS_WRITE
	sb->s_flags |= MS_RDONLY;
#endif
	sb->s_dirt = 1;
	/* brelse(bh);  resp.  brelse(bh1); brelse(bh2);
	   occurs when the disk is unmounted. */
	return sb;
}

/* This is only called on sync() and umount(), when s_dirt=1. */
static void sysv_write_super(struct super_block *sb)
{
	if (buffer_dirty(sb->sv_bh1) || buffer_dirty(sb->sv_bh2)) {
		/* If we are going to write out the super block,
		   then attach current time stamp.
		   But if the filesystem was marked clean, keep it clean. */
		unsigned long time = CURRENT_TIME;
		unsigned long old_time = *sb->sv_sb_time;
		if (sb->sv_convert)
			old_time = from_coh_ulong(old_time);
		if (sb->sv_type == FSTYPE_SYSV4)
			if (*sb->sv_sb_state == 0x7c269d38 - old_time)
				*sb->sv_sb_state = 0x7c269d38 - time;
		if (sb->sv_convert)
			time = to_coh_ulong(time);
		*sb->sv_sb_time = time;
		mark_buffer_dirty(sb->sv_bh2);
	}
	sb->s_dirt = 0;
}

static void sysv_put_super(struct super_block *sb)
{
	/* we can assume sysv_write_super() has already been called,
	   and that the superblock is locked */
	brelse(sb->sv_bh1);
	if (sb->sv_bh1 != sb->sv_bh2) brelse(sb->sv_bh2);
	/* switch back to default block size */
	if (sb->s_blocksize != BLOCK_SIZE)
		set_blocksize(sb->s_dev,BLOCK_SIZE);
}

static int sysv_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = sb->s_magic;			/* type of filesystem */
	buf->f_bsize = sb->sv_block_size;		/* block size */
	buf->f_blocks = sb->sv_ndatazones;		/* total data blocks in file system */
	buf->f_bfree = sysv_count_free_blocks(sb);	/* free blocks in fs */
	buf->f_bavail = buf->f_bfree;			/* free blocks available to non-superuser */
	buf->f_files = sb->sv_ninodes;			/* total file nodes in file system */
	buf->f_ffree = sysv_count_free_inodes(sb);	/* free file nodes in fs */
	buf->f_namelen = SYSV_NAMELEN;
	/* Don't know what value to put in buf->f_fsid */ /* file system id */
	return 0;
}


/* bmap support for running executables and shared libraries. */

static inline int inode_bmap(struct super_block * sb, struct inode * inode, int nr)
{
	int tmp = inode->u.sysv_i.i_data[nr];
	if (!tmp)
		return 0;
	return tmp + sb->sv_block_base;
}

static int block_bmap(struct super_block * sb, struct buffer_head * bh, int nr, int convert)
{
	int tmp;

	if (!bh)
		return 0;
	tmp = ((sysv_zone_t *) bh->b_data) [nr];
	if (convert)
		tmp = from_coh_ulong(tmp);
	brelse(bh);
	if (!tmp)
		return 0;
	return tmp + sb->sv_block_base;
}

static unsigned int sysv_block_map(struct inode *inode, unsigned int block)
{
	struct super_block *sb;
	int i, ret, convert;

	ret = 0;
	lock_kernel();
	sb = inode->i_sb;
	if (block < 10) {
		ret = inode_bmap(sb, inode, block);
		goto out;
	}
	block -= 10;
	convert = sb->sv_convert;
	if (block < sb->sv_ind_per_block) {
		i = inode_bmap(sb, inode, 10);
		if (!i)
			goto out;
		ret = block_bmap(sb,
				 bread(inode->i_dev, i, sb->sv_block_size),
				 block, convert);
		goto out;
	}
	block -= sb->sv_ind_per_block;
	if (block < sb->sv_ind_per_block_2) {
		i = inode_bmap(sb, inode, 11);
		if (!i)
			goto out;
		i = block_bmap(sb,
			       bread(inode->i_dev, i, sb->sv_block_size),
			       (block >> sb->sv_ind_per_block_bits), convert);
		if (!i)
			goto out;
		ret = block_bmap(sb,
				 bread(inode->i_dev, i, sb->sv_block_size),
				 (block & sb->sv_ind_per_block_1), convert);
		goto out;
	}
	block -= sb->sv_ind_per_block_2;
	if (block < sb->sv_ind_per_block_3) {
		i = inode_bmap(sb, inode, 12);
		if (!i)
			goto out;
		i = block_bmap(sb,
			       bread(inode->i_dev, i, sb->sv_block_size),
			       (block >> sb->sv_ind_per_block_2_bits), convert);
		if (!i)
			goto out;
		ret = block_bmap(sb,
				 bread(inode->i_dev, i, sb->sv_block_size),
				 ((block >> sb->sv_ind_per_block_bits) &
				  sb->sv_ind_per_block_1), convert);
		if (!i)
			goto out;
		ret = block_bmap(sb,
				 bread(inode->i_dev, i, sb->sv_block_size),
				 (block & sb->sv_ind_per_block_1), convert);
		goto out;
	}
	if ((int)block < 0)
		printk("sysv_block_map: block < 0\n");
	else
		printk("sysv_block_map: block > big\n");
out:
	unlock_kernel();
	return ret;
}

/* End of bmap support. */


/* Access selected blocks of regular files (or directories) */

static struct buffer_head *inode_getblk(struct inode *inode, int nr, int new_block,
	int *err, int metadata, long *phys, int *new)
{
	struct super_block *sb;
	u32 tmp;
	u32 *p;
	struct buffer_head * result;

	sb = inode->i_sb;
	p = inode->u.sysv_i.i_data + nr;
repeat:
	tmp = *p;
	if (tmp) {
		if (metadata) {
			result = sv_getblk(sb, inode->i_dev, tmp);
			if (tmp == *p)
				return result;
			brelse(result);
			goto repeat;
		} else {
			*phys = tmp;
			return NULL;
		}
	}

	tmp = sysv_new_block(sb);
	if (!tmp) {
		*err = -ENOSPC;
		return NULL;
	}
	if (metadata) {
		result = sv_getblk(sb, inode->i_dev, tmp);
		if (*p) {
			sysv_free_block(sb, tmp);
			brelse(result);
			goto repeat;
		}
	} else {
		if (*p) {
			/*
			 * Nobody is allowed to change block allocation
			 * state from under us:
			 */
			BUG();
			sysv_free_block(sb, tmp);
			goto repeat;
		}
		*phys = tmp;
		result = NULL;
		*err = 0;
		*new = 1;
	}
	*p = tmp;

	inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	return result;
}

static struct buffer_head *block_getblk(struct inode *inode,
	struct buffer_head *bh, int nr, int new_block, int *err,
	int metadata, long *phys, int *new)
{
	struct super_block *sb;
	u32 tmp, block;
	sysv_zone_t *p;
	struct buffer_head * result;

	result = NULL;
	if (!bh)
		goto out;
	if (!buffer_uptodate(bh)) {
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			goto out;
	}
	sb = inode->i_sb;
	p = nr + (sysv_zone_t *) bh->b_data;
repeat:
	block = tmp = *p;
	if (sb->sv_convert)
		block = from_coh_ulong(block);
	if (tmp) {
		if (metadata) {
			result = sv_getblk(sb, bh->b_dev, block);
			if (tmp == *p)
				goto out;
			brelse(result);
			goto repeat;
		} else {
			*phys = tmp;
			goto out;
		}
	}

	block = sysv_new_block(sb);
	if (!block)
		goto out;
	if (metadata) {
		result = sv_getblk(sb, bh->b_dev, block);
		if (*p) {
			sysv_free_block(sb, block);
			brelse(result);
			goto repeat;
		}
		memset(result->b_data, 0, sb->sv_block_size);
		mark_buffer_uptodate(result, 1);
		mark_buffer_dirty(result);
	} else {
		*phys = tmp;
		*new = 1;
	}
	if (*p) {
		sysv_free_block(sb, block);
		brelse(result);
		goto repeat;
	}
	*p = (sb->sv_convert ? to_coh_ulong(block) : block);
	mark_buffer_dirty(bh);
	*err = 0;
out:
	brelse(bh);
	return result;
}

static int sysv_get_block(struct inode *inode, long iblock, struct buffer_head *bh_result, int create)
{
	struct super_block *sb;
	int ret, err, new;
	struct buffer_head *bh;
	unsigned long ptr, phys;

	if (!create) {
		phys = sysv_block_map(inode, iblock);
		if (phys) {
			bh_result->b_dev = inode->i_dev;
			bh_result->b_blocknr = phys;
			bh_result->b_state |= (1UL << BH_Mapped);
		}
		return 0;
	}

	err = -EIO;
	new = 0;
	ret = 0;
	bh = NULL;

	lock_kernel();
	sb = inode->i_sb;
	if (iblock < 0)
		goto abort_negative;
	if (iblock > sb->sv_ind_per_block_3)
		goto abort_too_big;

	err = 0;
	ptr = iblock;

	/*
	 * ok, these macros clean the logic up a bit and make
	 * it much more readable:
	 */
#define GET_INODE_DATABLOCK(x) \
		inode_getblk(inode, x, iblock, &err, 0, &phys, &new)
#define GET_INODE_PTR(x) \
		inode_getblk(inode, x, iblock, &err, 1, NULL, NULL)
#define GET_INDIRECT_DATABLOCK(x) \
		block_getblk (inode, bh, x, iblock, &err, 0, &phys, &new);
#define GET_INDIRECT_PTR(x) \
		block_getblk (inode, bh, x, iblock, &err, 1, NULL, NULL);

	if (ptr < 10) {
		bh = GET_INODE_DATABLOCK(ptr);
		goto out;
	}
	ptr -= 10;
	if (ptr < sb->sv_ind_per_block) {
		bh = GET_INODE_PTR(10);
		goto get_indirect;
	}
	ptr -= sb->sv_ind_per_block;
	if (ptr < sb->sv_ind_per_block_2) {
		bh = GET_INODE_PTR(11);
		goto get_double;
	}
	ptr -= sb->sv_ind_per_block_2;
	bh = GET_INODE_PTR(12);
	bh = GET_INDIRECT_PTR(ptr >> sb->sv_ind_per_block_2_bits);
get_double:
	bh = GET_INDIRECT_PTR((ptr >> sb->sv_ind_per_block_bits) & sb->sv_ind_per_block_1);
get_indirect:
	bh = GET_INDIRECT_DATABLOCK(ptr & sb->sv_ind_per_block_1);

#undef GET_INODE_DATABLOCK
#undef GET_INODE_PTR
#undef GET_INDIRECT_DATABLOCK
#undef GET_INDIRECT_PTR

out:
	if (err)
		goto abort;
	bh_result->b_dev = inode->i_dev;
	bh_result->b_blocknr = phys;
	bh_result->b_state |= (1UL << BH_Mapped);
	if (new)
		bh_result->b_state |= (1UL << BH_New);
abort:
	unlock_kernel();
	return err;

abort_negative:
	printk("sysv_get_block: block < 0\n");
	goto abort;

abort_too_big:
	printk("sysv_get_block: block > big\n");
	goto abort;
}

static struct buffer_head *sysv_getblk(struct inode *inode, unsigned int block, int create)
{
	struct buffer_head dummy;
	int error;

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	error = sysv_get_block(inode, block, &dummy, create);
	if (!error && buffer_mapped(&dummy)) {
		struct buffer_head *bh;
		bh = getblk(dummy.b_dev, dummy.b_blocknr, inode->i_sb->sv_block_size);
		if (buffer_new(&dummy)) {
			memset(bh->b_data, 0, inode->i_sb->sv_block_size);
			mark_buffer_uptodate(bh, 1);
			mark_buffer_dirty(bh);
		}
		return bh;
	}
	return NULL;
}

struct buffer_head *sysv_file_bread(struct inode *inode, int block, int create)
{
	struct buffer_head *bh;

	bh = sysv_getblk(inode, block, create);
	if (!bh || buffer_uptodate(bh))
		return bh;
	ll_rw_block(READ, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse(bh);
	return NULL;
}

static int sysv_writepage(struct page *page)
{
	return block_write_full_page(page,sysv_get_block);
}
static int sysv_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,sysv_get_block);
}
static int sysv_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return block_prepare_write(page,from,to,sysv_get_block);
}
static int sysv_bmap(struct address_space *mapping, long block)
{
	return generic_block_bmap(mapping,block,sysv_get_block);
}
struct address_space_operations sysv_aops = {
	readpage: sysv_readpage,
	writepage: sysv_writepage,
	sync_page: block_sync_page,
	prepare_write: sysv_prepare_write,
	commit_write: generic_commit_write,
	bmap: sysv_bmap
};

#ifdef __BIG_ENDIAN

static inline unsigned long read3byte (unsigned char * p)
{
	return (p[2] | (p[1]<<8) | (p[0]<<16));
}

static inline void write3byte (unsigned char *p , unsigned long val)
{
	p[2]=val&0xFF;
	p[1]=(val>>8)&0xFF;
	p[0]=(val>>16)&0xFF;
}

#else

static inline unsigned long read3byte (unsigned char * p)
{
	return (unsigned long)(*(unsigned short *)p)
	     | (unsigned long)(*(unsigned char *)(p+2)) << 16;
}

static inline void write3byte (unsigned char * p, unsigned long val)
{
	*(unsigned short *)p = (unsigned short) val;
	*(unsigned char *)(p+2) = val >> 16;
}

#endif

static inline unsigned long coh_read3byte (unsigned char * p)
{
	return (unsigned long)(*(unsigned char *)p) << 16
	     | (unsigned long)(*(unsigned short *)(p+1));
}

static inline void coh_write3byte (unsigned char * p, unsigned long val)
{
	*(unsigned char *)p = val >> 16;
	*(unsigned short *)(p+1) = (unsigned short) val;
}

struct inode_operations sysv_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	setattr:	sysv_notify_change,
};

static void sysv_read_inode(struct inode *inode)
{
	struct super_block * sb = inode->i_sb;
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;
	unsigned int block, ino;
	umode_t mode;

	ino = inode->i_ino;
	inode->i_mode = 0;
	if (!ino || ino > sb->sv_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
		       kdevname(inode->i_dev), ino);
		return;
	}
	block = sb->sv_firstinodezone + ((ino-1) >> sb->sv_inodes_per_block_bits);
	if (!(bh = sv_bread(sb,inode->i_dev,block))) {
		printk("Major problem: unable to read inode from dev "
		       "%s\n",
		       kdevname(inode->i_dev));
		return;
	}
	raw_inode = (struct sysv_inode *) bh->b_data + ((ino-1) & sb->sv_inodes_per_block_1);
	mode = raw_inode->i_mode;
	if (sb->sv_kludge_symlinks)
		mode = from_coh_imode(mode);
	/* SystemV FS: kludge permissions if ino==SYSV_ROOT_INO ?? */
	inode->i_mode = mode;
	inode->i_uid = (uid_t)raw_inode->i_uid;
	inode->i_gid = (gid_t)raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlink;
	if (sb->sv_convert) {
		inode->i_size = from_coh_ulong(raw_inode->i_size);
		inode->i_atime = from_coh_ulong(raw_inode->i_atime);
		inode->i_mtime = from_coh_ulong(raw_inode->i_mtime);
		inode->i_ctime = from_coh_ulong(raw_inode->i_ctime);
	} else {
		inode->i_size = raw_inode->i_size;
		inode->i_atime = raw_inode->i_atime;
		inode->i_mtime = raw_inode->i_mtime;
		inode->i_ctime = raw_inode->i_ctime;
	}
	inode->i_blocks = inode->i_blksize = 0;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		;
	else
	if (sb->sv_convert)
		for (block = 0; block < 10+1+1+1; block++)
			inode->u.sysv_i.i_data[block] =
				coh_read3byte(&raw_inode->i_a.i_addb[3*block]);
	else
		for (block = 0; block < 10+1+1+1; block++)
			inode->u.sysv_i.i_data[block] =
				read3byte(&raw_inode->i_a.i_addb[3*block]);
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &sysv_file_inode_operations;
		inode->i_fop = &sysv_file_operations;
		inode->i_mapping->a_ops = &sysv_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &sysv_dir_inode_operations;
		inode->i_fop = &sysv_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &sysv_symlink_inode_operations;
		inode->i_mapping->a_ops = &sysv_aops;
	} else
		init_special_inode(inode, inode->i_mode,raw_inode->i_a.i_rdev);
	brelse(bh);
}

/* To avoid inconsistencies between inodes in memory and inodes on disk. */
int sysv_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	if ((error = inode_change_ok(inode, attr)) != 0)
		return error;

	if (attr->ia_valid & ATTR_MODE)
		if (inode->i_sb->sv_kludge_symlinks)
			if (attr->ia_mode == COH_KLUDGE_SYMLINK_MODE)
				attr->ia_mode = COH_KLUDGE_NOT_SYMLINK;

	inode_setattr(inode, attr);

	return 0;
}

static struct buffer_head * sysv_update_inode(struct inode * inode)
{
	struct super_block * sb = inode->i_sb;
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;
	unsigned int ino, block;
	umode_t mode;

	ino = inode->i_ino;
	if (!ino || ino > sb->sv_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
		       kdevname(inode->i_dev), ino);
		return 0;
	}
	block = sb->sv_firstinodezone + ((ino-1) >> sb->sv_inodes_per_block_bits);
	if (!(bh = sv_bread(sb,inode->i_dev,block))) {
		printk("unable to read i-node block\n");
		return 0;
	}
	raw_inode = (struct sysv_inode *) bh->b_data + ((ino-1) & sb->sv_inodes_per_block_1);
	mode = inode->i_mode;
	if (sb->sv_kludge_symlinks)
		mode = to_coh_imode(mode);
	raw_inode->i_mode = mode;
	raw_inode->i_uid = fs_high2lowuid(inode->i_uid);
	raw_inode->i_gid = fs_high2lowgid(inode->i_gid);
	raw_inode->i_nlink = inode->i_nlink;
	if (sb->sv_convert) {
		raw_inode->i_size = to_coh_ulong(inode->i_size);
		raw_inode->i_atime = to_coh_ulong(inode->i_atime);
		raw_inode->i_mtime = to_coh_ulong(inode->i_mtime);
		raw_inode->i_ctime = to_coh_ulong(inode->i_ctime);
	} else {
		raw_inode->i_size = inode->i_size;
		raw_inode->i_atime = inode->i_atime;
		raw_inode->i_mtime = inode->i_mtime;
		raw_inode->i_ctime = inode->i_ctime;
	}
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_a.i_rdev = kdev_t_to_nr(inode->i_rdev); /* write 2 or 3 bytes ?? */
	else
	if (sb->sv_convert)
		for (block = 0; block < 10+1+1+1; block++)
			coh_write3byte(&raw_inode->i_a.i_addb[3*block],inode->u.sysv_i.i_data[block]);
	else
		for (block = 0; block < 10+1+1+1; block++)
			write3byte(&raw_inode->i_a.i_addb[3*block],inode->u.sysv_i.i_data[block]);
	mark_buffer_dirty(bh);
	return bh;
}

void sysv_write_inode(struct inode * inode, int wait)
{
	struct buffer_head *bh;
	lock_kernel();
	bh = sysv_update_inode(inode);
	brelse(bh);
	unlock_kernel();
}

int sysv_sync_inode(struct inode * inode)
{
        int err = 0;
        struct buffer_head *bh;

        bh = sysv_update_inode(inode);
        if (bh && buffer_dirty(bh)) {
                ll_rw_block(WRITE, 1, &bh);
                wait_on_buffer(bh);
                if (buffer_req(bh) && !buffer_uptodate(bh))
                {
                        printk ("IO error syncing sysv inode ["
				"%s:%08lx]\n",
                                kdevname(inode->i_dev), inode->i_ino);
                        err = -1;
                }
        }
        else if (!bh)
                err = -1;
        brelse (bh);
        return err;
}

/* Every kernel module contains stuff like this. */

static DECLARE_FSTYPE_DEV(sysv_fs_type, "sysv", sysv_read_super);

static int __init init_sysv_fs(void)
{
	return register_filesystem(&sysv_fs_type);
}

static void __exit exit_sysv_fs(void)
{
	unregister_filesystem(&sysv_fs_type);
}

EXPORT_NO_SYMBOLS;

module_init(init_sysv_fs)
module_exit(exit_sysv_fs)

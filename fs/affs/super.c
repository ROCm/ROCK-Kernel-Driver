/*
 *  linux/fs/affs/inode.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#define DEBUG 0
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/genhd.h>
#include <linux/amigaffs.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/uaccess.h>

extern int *blk_size[];
extern struct timezone sys_tz;

#define MIN(a,b) (((a)<(b))?(a):(b))

static int affs_statfs(struct super_block *sb, struct statfs *buf);
static int affs_remount (struct super_block *sb, int *flags, char *data);

static void
affs_put_super(struct super_block *sb)
{
	int	 i;

	pr_debug("AFFS: put_super()\n");

	for (i = 0; i < sb->u.affs_sb.s_bm_count; i++)
		affs_brelse(sb->u.affs_sb.s_bitmap[i].bm_bh);
	if (!(sb->s_flags & MS_RDONLY)) {
		ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->bm_flag = be32_to_cpu(1);
		secs_to_datestamp(CURRENT_TIME,
				  &ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->disk_altered);
		affs_fix_checksum(sb->s_blocksize,sb->u.affs_sb.s_root_bh->b_data,5);
		mark_buffer_dirty(sb->u.affs_sb.s_root_bh);
	}

	if (sb->u.affs_sb.s_prefix)
		kfree(sb->u.affs_sb.s_prefix);
	kfree(sb->u.affs_sb.s_bitmap);
	affs_brelse(sb->u.affs_sb.s_root_bh);

	/*
	 * Restore the previous value of this device's blksize_size[][]
	 */
	set_blocksize(sb->s_dev, sb->u.affs_sb.s_blksize);

	return;
}

static void
affs_write_super(struct super_block *sb)
{
	int	 i, clean = 2;

	if (!(sb->s_flags & MS_RDONLY)) {
		lock_super(sb);
		for (i = 0, clean = 1; i < sb->u.affs_sb.s_bm_count; i++) {
			if (sb->u.affs_sb.s_bitmap[i].bm_bh) {
				if (buffer_dirty(sb->u.affs_sb.s_bitmap[i].bm_bh)) {
					clean = 0;
					break;
				}
			}
		}
		unlock_super(sb);
		ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->bm_flag = be32_to_cpu(clean);
		secs_to_datestamp(CURRENT_TIME,
				  &ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->disk_altered);
		affs_fix_checksum(sb->s_blocksize,sb->u.affs_sb.s_root_bh->b_data,5);
		mark_buffer_dirty(sb->u.affs_sb.s_root_bh);
		sb->s_dirt = !clean;	/* redo until bitmap synced */
	} else
		sb->s_dirt = 0;

	pr_debug("AFFS: write_super() at %lu, clean=%d\n", CURRENT_TIME, clean);
}

static struct super_operations affs_sops = {
	read_inode:	affs_read_inode,
	write_inode:	affs_write_inode,
	put_inode:	affs_put_inode,
	delete_inode:	affs_delete_inode,
	put_super:	affs_put_super,
	write_super:	affs_write_super,
	statfs:		affs_statfs,
	remount_fs:	affs_remount,
};

static int
parse_options(char *options, uid_t *uid, gid_t *gid, int *mode, int *reserved, s32 *root,
		int *blocksize, char **prefix, char *volume, unsigned long *mount_opts)
{
	char	*this_char, *value, *optn;
	int	 f;

	/* Fill in defaults */

	*uid        = current->uid;
	*gid        = current->gid;
	*reserved   = 2;
	*root       = -1;
	*blocksize  = -1;
	volume[0]   = ':';
	volume[1]   = 0;
	*mount_opts = 0;
	if (!options)
		return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		f = 0;
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if ((optn = "protect") && !strcmp(this_char, optn)) {
			if (value)
				goto out_inv_arg;
			*mount_opts |= SF_IMMUTABLE;
		} else if ((optn = "verbose") && !strcmp(this_char, optn)) {
			if (value)
				goto out_inv_arg;
			*mount_opts |= SF_VERBOSE;
		} else if ((optn = "mufs") && !strcmp(this_char, optn)) {
			if (value)
				goto out_inv_arg;
			*mount_opts |= SF_MUFS;
		} else if ((f = !strcmp(this_char,"setuid")) || !strcmp(this_char,"setgid")) {
			if (value) {
				if (!*value) {
					printk("AFFS: Argument for set[ug]id option missing\n");
					return 0;
				} else {
					(f ? *uid : *gid) = simple_strtoul(value,&value,0);
					if (*value) {
						printk("AFFS: Bad set[ug]id argument\n");
						return 0;
					}
					*mount_opts |= f ? SF_SETUID : SF_SETGID;
				}
			}
		} else if (!strcmp(this_char,"prefix")) {
			optn = "prefix";
			if (!value || !*value)
				goto out_no_arg;
			if (*prefix) {		/* Free any previous prefix */
				kfree(*prefix);
				*prefix = NULL;
			}
			*prefix = kmalloc(strlen(value) + 1,GFP_KERNEL);
			if (!*prefix)
				return 0;
			strcpy(*prefix,value);
			*mount_opts |= SF_PREFIX;
		} else if (!strcmp(this_char,"volume")) {
			optn = "volume";
			if (!value || !*value)
				goto out_no_arg;
			if (strlen(value) > 30)
				value[30] = 0;
			strncpy(volume,value,30);
		} else if (!strcmp(this_char,"mode")) {
			optn = "mode";
			if (!value || !*value)
				goto out_no_arg;
			*mode = simple_strtoul(value,&value,8) & 0777;
			if (*value)
				return 0;
			*mount_opts |= SF_SETMODE;
		} else if (!strcmp(this_char,"reserved")) {
			optn = "reserved";
			if (!value || !*value)
				goto out_no_arg;
			*reserved = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		} else if (!strcmp(this_char,"root")) {
			optn = "root";
			if (!value || !*value)
				goto out_no_arg;
			*root = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		} else if (!strcmp(this_char,"bs")) {
			optn = "bs";
			if (!value || !*value)
				goto out_no_arg;
			*blocksize = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
			if (*blocksize != 512 && *blocksize != 1024 && *blocksize != 2048
			    && *blocksize != 4096) {
				printk ("AFFS: Invalid blocksize (512, 1024, 2048, 4096 allowed)\n");
				return 0;
			}
		} else if (!strcmp (this_char, "grpquota")
			 || !strcmp (this_char, "noquota")
			 || !strcmp (this_char, "quota")
			 || !strcmp (this_char, "usrquota"))
			 /* Silently ignore the quota options */
			;
		else {
			printk("AFFS: Unrecognized mount option %s\n", this_char);
			return 0;
		}
	}
	return 1;

out_no_arg:
	printk("AFFS: The %s option requires an argument\n", optn);
	return 0;
out_inv_arg:
	printk("AFFS: Option %s does not take an argument\n", optn);
	return 0;
}

/* This function definitely needs to be split up. Some fine day I'll
 * hopefully have the guts to do so. Until then: sorry for the mess.
 */

static struct super_block *
affs_read_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head	*bh = NULL;
	struct buffer_head	*bb;
	struct inode		*root_inode;
	kdev_t			 dev = s->s_dev;
	s32			 root_block;
	int			 blocks, size, blocksize;
	u32			 chksum;
	u32			*bm;
	s32			 ptype, stype;
	int			 mapidx;
	int			 num_bm;
	int			 i, j;
	s32			 key;
	uid_t			 uid;
	gid_t			 gid;
	int			 reserved;
	int			 az_no;
	int			 bmalt = 0;
	unsigned long		 mount_flags;
	unsigned long		 offset;

	pr_debug("AFFS: read_super(%s)\n",data ? (const char *)data : "no options");

	s->s_magic             = AFFS_SUPER_MAGIC;
	s->s_op                = &affs_sops;
	s->u.affs_sb.s_bitmap  = NULL;
	s->u.affs_sb.s_root_bh = NULL;
	s->u.affs_sb.s_prefix  = NULL;
	s->u.affs_sb.s_hashsize= 0;

	if (!parse_options(data,&uid,&gid,&i,&reserved,&root_block,
				&blocksize,&s->u.affs_sb.s_prefix,
				s->u.affs_sb.s_volume, &mount_flags))
		goto out_bad_opts;
	/* N.B. after this point s_prefix must be released */

	s->u.affs_sb.s_flags   = mount_flags;
	s->u.affs_sb.s_mode    = i;
	s->u.affs_sb.s_uid     = uid;
	s->u.affs_sb.s_gid     = gid;
	s->u.affs_sb.s_reserved= reserved;

	/* Get the size of the device in 512-byte blocks.
	 * If we later see that the partition uses bigger
	 * blocks, we will have to change it.
	 */

	blocks = blk_size[MAJOR(dev)][MINOR(dev)];
	if (blocks == 0)
		goto out_bad_size;
	s->u.affs_sb.s_blksize = blksize_size[MAJOR(dev)][MINOR(dev)];
	if (!s->u.affs_sb.s_blksize)
		s->u.affs_sb.s_blksize = BLOCK_SIZE;
	size = (s->u.affs_sb.s_blksize / 512) * blocks;
	pr_debug("AFFS: initial blksize=%d, blocks=%d\n",
		s->u.affs_sb.s_blksize, blocks);

	/* Try to find root block. Its location depends on the block size. */

	i = 512;
	j = 4096;
	if (blocksize > 0) {
		i = j = blocksize;
		size = size / (blocksize / 512);
	}
	for (blocksize = i, key = 0; blocksize <= j; blocksize <<= 1, size >>= 1) {
		s->u.affs_sb.s_root_block = root_block;
		if (root_block < 0)
			s->u.affs_sb.s_root_block = (reserved + size - 1) / 2;
		pr_debug("AFFS: setting blocksize to %d\n", blocksize);
		set_blocksize(dev, blocksize);

		/* The root block location that was calculated above is not
		 * correct if the partition size is an odd number of 512-
		 * byte blocks, which will be rounded down to a number of
		 * 1024-byte blocks, and if there were an even number of
		 * reserved blocks. Ideally, all partition checkers should
		 * report the real number of blocks of the real blocksize,
		 * but since this just cannot be done, we have to try to
		 * find the root block anyways. In the above case, it is one
		 * block behind the calculated one. So we check this one, too.
		 */
		for (num_bm = 0; num_bm < 2; num_bm++) {
			pr_debug("AFFS: Dev %s, trying root=%u, bs=%d, "
				"size=%d, reserved=%d\n",
				kdevname(dev),
				s->u.affs_sb.s_root_block + num_bm,
				blocksize, size, reserved);
			bh = affs_bread(dev, s->u.affs_sb.s_root_block + num_bm,
					blocksize);
			if (!bh)
				continue;
			if (!affs_checksum_block(blocksize,bh->b_data,&ptype,&stype) &&
			    ptype == T_SHORT && stype == ST_ROOT) {
				s->s_blocksize             = blocksize;
				s->u.affs_sb.s_hashsize    = blocksize / 4 - 56;
				s->u.affs_sb.s_root_block += num_bm;
				key                        = 1;
				goto got_root;
			}
			affs_brelse(bh);
			bh = NULL;
		}
	}
	goto out_no_valid_block;

	/* N.B. after this point bh must be released */
got_root:
	root_block = s->u.affs_sb.s_root_block;

	s->u.affs_sb.s_partition_size   = size;
	s->s_blocksize_bits             = blocksize == 512 ? 9 :
					  blocksize == 1024 ? 10 :
					  blocksize == 2048 ? 11 : 12;

	/* Find out which kind of FS we have */
	bb = affs_bread(dev,0,s->s_blocksize);
	if (!bb)
		goto out_no_root_block;
	chksum = be32_to_cpu(*(u32 *)bb->b_data);
	affs_brelse(bb);

	/* Dircache filesystems are compatible with non-dircache ones
	 * when reading. As long as they aren't supported, writing is
	 * not recommended.
	 */
	if ((chksum == FS_DCFFS || chksum == MUFS_DCFFS || chksum == FS_DCOFS
	     || chksum == MUFS_DCOFS) && !(s->s_flags & MS_RDONLY)) {
		printk(KERN_NOTICE "AFFS: Dircache FS - mounting %s read only\n",
			kdevname(dev));
		s->s_flags |= MS_RDONLY;
		s->u.affs_sb.s_flags |= SF_READONLY;
	}
	switch (chksum) {
		case MUFS_FS:
		case MUFS_INTLFFS:
			s->u.affs_sb.s_flags |= SF_MUFS;
			/* fall thru */
		case FS_INTLFFS:
			s->u.affs_sb.s_flags |= SF_INTL;
			break;
		case MUFS_DCFFS:
		case MUFS_FFS:
			s->u.affs_sb.s_flags |= SF_MUFS;
			break;
		case FS_DCFFS:
		case FS_FFS:
			break;
		case MUFS_OFS:
			s->u.affs_sb.s_flags |= SF_MUFS;
			/* fall thru */
		case FS_OFS:
			s->u.affs_sb.s_flags |= SF_OFS;
			s->s_flags |= MS_NOEXEC;
			break;
		case MUFS_DCOFS:
		case MUFS_INTLOFS:
			s->u.affs_sb.s_flags |= SF_MUFS;
		case FS_DCOFS:
		case FS_INTLOFS:
			s->u.affs_sb.s_flags |= SF_INTL | SF_OFS;
			s->s_flags |= MS_NOEXEC;
			break;
		default:
			goto out_unknown_fs;
	}

	if (mount_flags & SF_VERBOSE) {
		chksum = cpu_to_be32(chksum);
		printk(KERN_NOTICE "AFFS: Mounting volume \"%*s\": Type=%.3s\\%c, Blocksize=%d\n",
			GET_END_PTR(struct root_end,bh->b_data,blocksize)->disk_name[0],
			&GET_END_PTR(struct root_end,bh->b_data,blocksize)->disk_name[1],
			(char *)&chksum,((char *)&chksum)[3] + '0',blocksize);
	}

	s->s_flags |= MS_NODEV | MS_NOSUID;

	/* Keep super block in cache */
	bb = affs_bread(dev,root_block,s->s_blocksize);
	if (!bb)
		goto out_no_root_block;
	s->u.affs_sb.s_root_bh = bb;
	/* N.B. after this point s_root_bh must be released */

	/* Allocate space for bitmaps, zones and others */

	size   = s->u.affs_sb.s_partition_size - reserved;
	num_bm = (size + s->s_blocksize * 8 - 32 - 1) / (s->s_blocksize * 8 - 32);
	az_no  = (size + AFFS_ZONE_SIZE - 1) / (AFFS_ZONE_SIZE - 32);
	ptype  = num_bm * sizeof(struct affs_bm_info) +
		 az_no * sizeof(struct affs_alloc_zone) +
		 MAX_ZONES * sizeof(struct affs_zone);
	pr_debug("AFFS: num_bm=%d, az_no=%d, sum=%d\n",num_bm,az_no,ptype);
	if (!(s->u.affs_sb.s_bitmap = kmalloc(ptype, GFP_KERNEL)))
		goto out_no_bitmap;
	memset(s->u.affs_sb.s_bitmap,0,ptype);
	/* N.B. after the point s_bitmap must be released */

	s->u.affs_sb.s_zones   = (struct affs_zone *)&s->u.affs_sb.s_bitmap[num_bm];
	s->u.affs_sb.s_alloc   = (struct affs_alloc_zone *)&s->u.affs_sb.s_zones[MAX_ZONES];
	s->u.affs_sb.s_num_az  = az_no;

	mapidx = 0;

	if (ROOT_END_S(bh->b_data,s)->bm_flag == 0) {
		if (!(s->s_flags & MS_RDONLY)) {
			printk(KERN_NOTICE "AFFS: Bitmap invalid - mounting %s read only\n",
				kdevname(dev));
			s->s_flags |= MS_RDONLY;
		}
		affs_brelse(bh);
		bh = NULL;
		goto nobitmap;
	}

	/* The following section is ugly, I know. Especially because of the
	 * reuse of some variables that are not named properly.
	 */

	key    = root_block;
	ptype  = s->s_blocksize / 4 - 49;
	stype  = ptype + 25;
	offset = s->u.affs_sb.s_reserved;
	az_no  = 0;
	while (bh) {
		bm = (u32 *)bh->b_data;
		for (i = ptype; i < stype && bm[i]; i++, mapidx++) {
			if (mapidx >= num_bm) {
				printk(KERN_ERR "AFFS: Extraneous bitmap pointer - "
					       "mounting %s read only.\n",kdevname(dev));
				s->s_flags |= MS_RDONLY;
				s->u.affs_sb.s_flags |= SF_READONLY;
				continue;
			}
			bb = affs_bread(dev,be32_to_cpu(bm[i]),s->s_blocksize);
			if (!bb)
				goto out_no_read_bm;
			if (affs_checksum_block(s->s_blocksize,bb->b_data,NULL,NULL) &&
			    !(s->s_flags & MS_RDONLY)) {
				printk(KERN_WARNING "AFFS: Bitmap (%d,key=%u) invalid - "
				       "mounting %s read only.\n",mapidx,be32_to_cpu(bm[i]),
					kdevname(dev));
				s->s_flags |= MS_RDONLY;
				s->u.affs_sb.s_flags |= SF_READONLY;
			}
			/* Mark unused bits in the last word as allocated */
			if (size <= s->s_blocksize * 8 - 32) {	/* last bitmap */
				ptype = size / 32 + 1;		/* word number */
				key   = size & 0x1F;		/* used bits */
				if (key && !(s->s_flags & MS_RDONLY)) {
					chksum = cpu_to_be32(0x7FFFFFFF >> (31 - key));
					((u32 *)bb->b_data)[ptype] &= chksum;
					affs_fix_checksum(s->s_blocksize,bb->b_data,0);
					mark_buffer_dirty(bb);
					bmalt = 1;
				}
				ptype = (size + 31) & ~0x1F;
				size  = 0;
				s->u.affs_sb.s_flags |= SF_BM_VALID;
			} else {
				ptype = s->s_blocksize * 8 - 32;
				size -= ptype;
			}
			s->u.affs_sb.s_bitmap[mapidx].bm_firstblk = offset;
			s->u.affs_sb.s_bitmap[mapidx].bm_bh       = NULL;
			s->u.affs_sb.s_bitmap[mapidx].bm_key      = be32_to_cpu(bm[i]);
			s->u.affs_sb.s_bitmap[mapidx].bm_count    = 0;
			offset += ptype;

			for (j = 0; ptype > 0; j++, az_no++, ptype -= key) {
				key = MIN(ptype,AFFS_ZONE_SIZE);	/* size in bits */
				s->u.affs_sb.s_alloc[az_no].az_size = key / 32;
				s->u.affs_sb.s_alloc[az_no].az_free =
					affs_count_free_bits(key / 8,bb->b_data +
					     j * (AFFS_ZONE_SIZE / 8) + 4);
			}
			affs_brelse(bb);
		}
		key   = be32_to_cpu(bm[stype]);		/* Next block of bitmap pointers	*/
		ptype = 0;
		stype = s->s_blocksize / 4 - 1;
		affs_brelse(bh);
		bh = NULL;
		if (key) {
			bh = affs_bread(dev,key,s->s_blocksize);
			if (!bh)
				goto out_no_bm_ext;
		}
	}
	if (mapidx < num_bm)
		goto out_bad_num;

nobitmap:
	s->u.affs_sb.s_bm_count = num_bm;

	/* set up enough so that it can read an inode */

	s->s_dirt  = 1;
	root_inode = iget(s,root_block);
	if (!root_inode)
		goto out_no_root;
	s->s_root  = d_alloc_root(root_inode);
	if (!s->s_root)
		goto out_no_root;
	s->s_root->d_op = &affs_dentry_operations;

	/* Record date of last change if the bitmap was truncated and
	 * create data zones if the volume is writable.
	 */

	if (!(s->s_flags & MS_RDONLY)) {
		if (bmalt) {
			secs_to_datestamp(CURRENT_TIME,&ROOT_END(
				s->u.affs_sb.s_root_bh->b_data,root_inode)->disk_altered);
			affs_fix_checksum(s->s_blocksize,s->u.affs_sb.s_root_bh->b_data,5);
			mark_buffer_dirty(s->u.affs_sb.s_root_bh);
		}
		affs_make_zones(s);
	}

	pr_debug("AFFS: s_flags=%lX\n",s->s_flags);
	return s;

out_bad_opts:
	printk(KERN_ERR "AFFS: Error parsing options\n");
	goto out_fail;
out_bad_size:
	printk(KERN_ERR "AFFS: Could not determine device size\n");
	goto out_free_prefix;
out_no_valid_block:
	if (!silent)
		printk(KERN_ERR "AFFS: No valid root block on device %s\n",
			kdevname(dev));
	goto out_restore;
out_unknown_fs:
	printk(KERN_ERR "AFFS: Unknown filesystem on device %s: %08X\n",
		kdevname(dev), chksum);
	goto out_free_bh;
out_no_root_block:
	printk(KERN_ERR "AFFS: Cannot read root block\n");
	goto out_free_bh;
out_no_bitmap:
	printk(KERN_ERR "AFFS: Bitmap allocation failed\n");
	goto out_free_root_block;
out_no_read_bm:
	printk(KERN_ERR "AFFS: Cannot read bitmap\n");
	goto out_free_bitmap;
out_no_bm_ext:
	printk(KERN_ERR "AFFS: Cannot read bitmap extension\n");
	goto out_free_bitmap;
out_bad_num:
	printk(KERN_ERR "AFFS: Got only %d bitmap blocks, expected %d\n",
		mapidx, num_bm);
	goto out_free_bitmap;
out_no_root:
	printk(KERN_ERR "AFFS: Get root inode failed\n");

	/*
	 * Begin the cascaded cleanup ...
	 */
	iput(root_inode);
out_free_bitmap:
	kfree(s->u.affs_sb.s_bitmap);
out_free_root_block:
	affs_brelse(s->u.affs_sb.s_root_bh);
out_free_bh:
	affs_brelse(bh);
out_restore:
	set_blocksize(dev, s->u.affs_sb.s_blksize);
out_free_prefix:
	if (s->u.affs_sb.s_prefix)
		kfree(s->u.affs_sb.s_prefix);
out_fail:
	return NULL;
}

static int
affs_remount(struct super_block *sb, int *flags, char *data)
{
	int			 blocksize;
	uid_t			 uid;
	gid_t			 gid;
	int			 mode;
	int			 reserved;
	int			 root_block;
	unsigned long		 mount_flags;
	unsigned long		 read_only = sb->u.affs_sb.s_flags & SF_READONLY;

	pr_debug("AFFS: remount(flags=0x%x,opts=\"%s\")\n",*flags,data);

	if (!parse_options(data,&uid,&gid,&mode,&reserved,&root_block,
	    &blocksize,&sb->u.affs_sb.s_prefix,sb->u.affs_sb.s_volume,&mount_flags))
		return -EINVAL;
	sb->u.affs_sb.s_flags = mount_flags | read_only;
	sb->u.affs_sb.s_mode  = mode;
	sb->u.affs_sb.s_uid   = uid;
	sb->u.affs_sb.s_gid   = gid;

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY) {
		sb->s_dirt = 1;
		while (sb->s_dirt)
			affs_write_super(sb);
		sb->s_flags |= MS_RDONLY;
	} else if (!(sb->u.affs_sb.s_flags & SF_READONLY)) {
		sb->s_flags &= ~MS_RDONLY;
		affs_make_zones(sb);
	} else {
		affs_warning(sb,"remount","Cannot remount fs read/write because of errors");
		return -EINVAL;
	}
	return 0;
}

static int
affs_statfs(struct super_block *sb, struct statfs *buf)
{
	int		 free;

	pr_debug("AFFS: statfs() partsize=%d, reserved=%d\n",sb->u.affs_sb.s_partition_size,
	     sb->u.affs_sb.s_reserved);

	free          = affs_count_free_blocks(sb);
	buf->f_type    = AFFS_SUPER_MAGIC;
	buf->f_bsize   = sb->s_blocksize;
	buf->f_blocks  = sb->u.affs_sb.s_partition_size - sb->u.affs_sb.s_reserved;
	buf->f_bfree   = free;
	buf->f_bavail  = free;
	return 0;
}

static DECLARE_FSTYPE_DEV(affs_fs_type, "affs", affs_read_super);

static int __init init_affs_fs(void)
{
	return register_filesystem(&affs_fs_type);
}

static void __exit exit_affs_fs(void)
{
	unregister_filesystem(&affs_fs_type);
}

EXPORT_NO_SYMBOLS;

module_init(init_affs_fs)
module_exit(exit_affs_fs)

/*
 *  linux/fs/affs/amigaffs.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Amiga FFS filesystem.
 *
 *  Please send bug reports to: hjw@zvw.de
 */

#define DEBUG 0
#include <stdarg.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/amigaffs.h>

extern struct timezone sys_tz;

static char ErrorBuffer[256];

/*
 * Functions for accessing Amiga-FFS structures.
 */

/* Set *NAME to point to the file name in a file header block in memory
   pointed to by FH_DATA.  The length of the name is returned. */

int
affs_get_file_name(int bsize, void *fh_data, unsigned char **name)
{
	struct file_end *file_end;

	file_end = GET_END_PTR(struct file_end, fh_data, bsize);
	if (file_end->file_name[0] == 0
	    || file_end->file_name[0] > 30) {
		printk(KERN_WARNING "AFFS: bad filename (length=%d chars)\n",
			file_end->file_name[0]);
		*name = "***BAD_FILE***";
		return 14;
        }
	*name = (unsigned char *)&file_end->file_name[1];
        return file_end->file_name[0];
}

/* Insert a header block (file) into the directory (next).
 * This routine assumes that the caller has the superblock locked.
 */

int
affs_insert_hash(unsigned long next, struct buffer_head *file, struct inode *inode)
{
	struct buffer_head	*bh;
	s32			 ino;
	int			 offset;

	offset = affs_hash_name(FILE_END(file->b_data,inode)->file_name+1,
				FILE_END(file->b_data,inode)->file_name[0],
				AFFS_I2FSTYPE(inode),AFFS_I2HSIZE(inode)) + 6;
	ino    = be32_to_cpu(((struct dir_front *)file->b_data)->own_key);

	pr_debug("AFFS: insert_hash(dir_ino=%lu,ino=%d)\n",next,ino);

	FILE_END(file->b_data,inode)->parent = cpu_to_be32(next);

	while (1) {
		if (!(bh = affs_bread(inode->i_dev,next,AFFS_I2BSIZE(inode))))
			return -EIO;
		next = be32_to_cpu(((s32 *)bh->b_data)[offset]);
		if (!next || next > ino)
			break;
		offset = AFFS_I2BSIZE(inode) / 4 - 4;
		affs_brelse(bh);
	}

	DIR_END(file->b_data,inode)->hash_chain = cpu_to_be32(next);
	((s32 *)bh->b_data)[offset]             = cpu_to_be32(ino);
	affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
	mark_buffer_dirty(bh);
	affs_brelse(bh);

	return 0;
}
/* Remove a header block from its hash table (directory).
 * 'inode' may be any inode on the partition, it's only
 * used for calculating the block size and superblock
 * reference.
 */

int
affs_remove_hash(struct buffer_head *dbh, struct inode *inode)
{
	s32			 ownkey;
	s32			 key;
	s32			 ptype;
	s32			 stype;
	int			 offset;
	int			 retval;
	struct buffer_head	*bh;

	ownkey = be32_to_cpu(((struct dir_front *)dbh->b_data)->own_key);
	key    = be32_to_cpu(FILE_END(dbh->b_data,inode)->parent);
	offset = affs_hash_name(FILE_END(dbh->b_data,inode)->file_name+1,
				FILE_END(dbh->b_data,inode)->file_name[0],
				AFFS_I2FSTYPE(inode),AFFS_I2HSIZE(inode)) + 6;
	pr_debug("AFFS: remove_hash(dir=%d, ino=%d, hashval=%d)\n",key,ownkey,offset-6);
	retval = -ENOENT;

	lock_super(inode->i_sb);
	while (key) {
		if (!(bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode)))) {
			retval = -EIO;
			break;
		}
		if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&ptype,&stype)
		    || ptype != T_SHORT || (stype != ST_FILE && stype != ST_USERDIR &&
					    stype != ST_LINKFILE && stype != ST_LINKDIR &&
					    stype != ST_ROOT && stype != ST_SOFTLINK)) {
			affs_error(inode->i_sb,"affs_remove_hash",
				"Bad block in hash chain (key=%d, ptype=%d, stype=%d, ownkey=%d)",
				key,ptype,stype,ownkey);
			affs_brelse(bh);
			retval = -EINVAL;
			break;
		}
		key = be32_to_cpu(((s32 *)bh->b_data)[offset]);
		if (ownkey == key) {
			((s32 *)bh->b_data)[offset] = FILE_END(dbh->b_data,inode)->hash_chain;
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh);
			affs_brelse(bh);
			retval = 0;
			break;
		}
		affs_brelse(bh);
		offset = AFFS_I2BSIZE(inode) / 4 - 4;
	}
	unlock_super(inode->i_sb);

	return retval;
}

/* Remove header from link chain */

int
affs_remove_link(struct buffer_head *dbh, struct inode *inode)
{
	int			 retval;
	s32			 key;
	s32			 ownkey;
	s32			 ptype;
	s32			 stype;
	struct buffer_head	*bh;

	ownkey = be32_to_cpu((DIR_FRONT(dbh)->own_key));
	key    = be32_to_cpu(FILE_END(dbh->b_data,inode)->original);
	retval = -ENOENT;

	pr_debug("AFFS: remove_link(link=%d, original=%d)\n",ownkey,key);

	lock_super(inode->i_sb);
	while (key) {
		if (!(bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode)))) {
			retval = -EIO;
			break;
		}
		if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&ptype,&stype)) {
			affs_error(inode->i_sb,"affs_remove_link","Checksum error (block %d)",key);
			affs_brelse(bh);
			retval = -EINVAL;
			break;
		}
		key = be32_to_cpu(FILE_END(bh->b_data,inode)->link_chain);
		if (ownkey == key) {
			FILE_END(bh->b_data,inode)->link_chain =
						FILE_END(dbh->b_data,inode)->link_chain;
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh);
			affs_brelse(bh);
			retval = 0;
			break;
		}
		affs_brelse(bh);
	}
	unlock_super(inode->i_sb);

	return retval;
}

/* Remove a filesystem object. If the object to be removed has
 * links to it, one of the links must be changed to inherit
 * the file or directory. As above, any inode will do.
 * The buffer will not be freed. If the header is a link, the
 * block will be marked as free.
 * This function returns a negative error number in case of
 * an error, else 0 if the inode is to be deleted or 1 if not.
 */

int
affs_remove_header(struct buffer_head *bh, struct inode *inode)
{
	struct buffer_head	*link_bh;
	struct inode		*dir;
	unsigned long		 link_ino;
	unsigned long		 orig_ino;
	unsigned int		 dir_ino;
	int			 error;

	pr_debug("AFFS: remove_header(key=%ld)\n",be32_to_cpu(DIR_FRONT(bh)->own_key));

	/* Mark directory as changed.  We do this before anything else,
	 * as it must be done anyway and doesn't hurt even if an
	 * error occurs later.
	 */
	dir = iget(inode->i_sb,be32_to_cpu(FILE_END(bh->b_data,inode)->parent));
	if (!dir)
		return -EIO;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_version++;
	mark_inode_dirty(dir);
	iput(dir);

	orig_ino = be32_to_cpu(FILE_END(bh->b_data,inode)->original);
	if (orig_ino) {		/* This is just a link. Nothing much to do. */
		pr_debug("AFFS: Removing link.\n");
		if ((error = affs_remove_link(bh,inode)))
			return error;
		if ((error = affs_remove_hash(bh,inode)))
			return error;
		affs_free_block(inode->i_sb,be32_to_cpu(DIR_FRONT(bh)->own_key));
		return 1;
	}
	
	link_ino = be32_to_cpu(FILE_END(bh->b_data,inode)->link_chain);
	if (link_ino) {		/* This is the complicated case. Yuck. */
		pr_debug("AFFS: Removing original with links to it.\n");
		/* Unlink the object and its first link from their directories. */
		if ((error = affs_remove_hash(bh,inode)))
			return error;
		if (!(link_bh = affs_bread(inode->i_dev,link_ino,AFFS_I2BSIZE(inode))))
			return -EIO;
		if ((error = affs_remove_hash(link_bh,inode))) {
			affs_brelse(link_bh);
			return error;
		}
		/* Fix link chain. */
		if ((error = affs_remove_link(link_bh,inode))) {
			affs_brelse(link_bh);
			return error;
		}
		/* Rename link to object. */
		memcpy(FILE_END(bh->b_data,inode)->file_name,
			FILE_END(link_bh->b_data,inode)->file_name,32);
		/* Insert object into dir the link was in. */
		dir_ino = be32_to_cpu(FILE_END(link_bh->b_data,inode)->parent);
		if ((error = affs_insert_hash(dir_ino,bh,inode))) {
			affs_brelse(link_bh);
			return error;
		}
		affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
		mark_buffer_dirty(bh);
		affs_brelse(link_bh);
		affs_free_block(inode->i_sb,link_ino);
		/* Mark the link's parent dir as changed, too. */
		if (!(dir = iget(inode->i_sb,dir_ino)))
			return -EIO;
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		dir->i_version++;
		mark_inode_dirty(dir);
		iput(dir);
		return 1;
	}
	/* Plain file/dir. This is the simplest case. */
	pr_debug("AFFS: Removing plain file/dir.\n");
	if ((error = affs_remove_hash(bh,inode)))
		return error;
	return 0;
}


/* Checksum a block, do various consistency checks and optionally return
   the blocks type number.  DATA points to the block.  If their pointers
   are non-null, *PTYPE and *STYPE are set to the primary and secondary
   block types respectively, *HASHSIZE is set to the size of the hashtable
   (which lets us calculate the block size).
   Returns non-zero if the block is not consistent. */

u32
affs_checksum_block(int bsize, void *data, s32 *ptype, s32 *stype)
{
	u32	 sum;
	u32	*p;

	bsize /= 4;
	if (ptype)
		*ptype = be32_to_cpu(((s32 *)data)[0]);
	if (stype)
		*stype = be32_to_cpu(((s32 *)data)[bsize - 1]);

	sum    = 0;
	p      = data;
	while (bsize--)
		sum += be32_to_cpu(*p++);
	return sum;
}

/*
 * Calculate the checksum of a disk block and store it
 * at the indicated position.
 */

void
affs_fix_checksum(int bsize, void *data, int cspos)
{
	u32	 ocs;
	u32	 cs;

	cs   = affs_checksum_block(bsize,data,NULL,NULL);
	ocs  = be32_to_cpu(((u32 *)data)[cspos]);
	ocs -= cs;
	((u32 *)data)[cspos] = be32_to_cpu(ocs);
}

void
secs_to_datestamp(time_t secs, struct DateStamp *ds)
{
	u32	 days;
	u32	 minute;

	secs -= sys_tz.tz_minuteswest * 60 + ((8 * 365 + 2) * 24 * 60 * 60);
	if (secs < 0)
		secs = 0;
	days    = secs / 86400;
	secs   -= days * 86400;
	minute  = secs / 60;
	secs   -= minute * 60;

	ds->ds_Days   = be32_to_cpu(days);
	ds->ds_Minute = be32_to_cpu(minute);
	ds->ds_Tick   = be32_to_cpu(secs * 50);
}

int
prot_to_mode(u32 prot)
{
	int	 mode = 0;

	if (AFFS_UMAYWRITE(prot))
		mode |= S_IWUSR;
	if (AFFS_UMAYREAD(prot))
		mode |= S_IRUSR;
	if (AFFS_UMAYEXECUTE(prot))
		mode |= S_IXUSR;
	if (AFFS_GMAYWRITE(prot))
		mode |= S_IWGRP;
	if (AFFS_GMAYREAD(prot))
		mode |= S_IRGRP;
	if (AFFS_GMAYEXECUTE(prot))
		mode |= S_IXGRP;
	if (AFFS_OMAYWRITE(prot))
		mode |= S_IWOTH;
	if (AFFS_OMAYREAD(prot))
		mode |= S_IROTH;
	if (AFFS_OMAYEXECUTE(prot))
		mode |= S_IXOTH;
	
	return mode;
}

u32
mode_to_prot(int mode)
{
	u32	 prot = 0;

	if (mode & S_IXUSR)
		prot |= FIBF_SCRIPT;
	if (mode & S_IRUSR)
		prot |= FIBF_READ;
	if (mode & S_IWUSR)
		prot |= FIBF_WRITE | FIBF_DELETE;
	if (mode & S_IRGRP)
		prot |= FIBF_GRP_READ;
	if (mode & S_IWGRP)
		prot |= FIBF_GRP_WRITE;
	if (mode & S_IROTH)
		prot |= FIBF_OTR_READ;
	if (mode & S_IWOTH)
		prot |= FIBF_OTR_WRITE;
	
	return prot;
}

void
affs_error(struct super_block *sb, const char *function, const char *fmt, ...)
{
	va_list	 args;

	va_start(args,fmt);
	vsprintf(ErrorBuffer,fmt,args);
	va_end(args);

	printk(KERN_CRIT "AFFS error (device %s): %s(): %s\n",kdevname(sb->s_dev),
		function,ErrorBuffer);
	if (!(sb->s_flags & MS_RDONLY))
		printk(KERN_WARNING "AFFS: Remounting filesystem read-only\n");
	sb->s_flags |= MS_RDONLY;
	sb->u.affs_sb.s_flags |= SF_READONLY;	/* Don't allow to remount rw */
}

void
affs_warning(struct super_block *sb, const char *function, const char *fmt, ...)
{
	va_list	 args;

	va_start(args,fmt);
	vsprintf(ErrorBuffer,fmt,args);
	va_end(args);

	printk(KERN_WARNING "AFFS warning (device %s): %s(): %s\n",kdevname(sb->s_dev),
		function,ErrorBuffer);
}

/* Check if the name is valid for a affs object. */

int
affs_check_name(const unsigned char *name, int len)
{
	int	 i;

	if (len > 30)
#ifdef AFFS_NO_TRUNCATE
		return -ENAMETOOLONG;
#else
		len = 30;
#endif

	for (i = 0; i < len; i++) {
		if (name[i] < ' ' || name[i] == ':'
		    || (name[i] > 0x7e && name[i] < 0xa0))
			return -EINVAL;
	}

	return 0;
}

/* This function copies name to bstr, with at most 30
 * characters length. The bstr will be prepended by
 * a length byte.
 * NOTE: The name will must be already checked by
 *       affs_check_name()!
 */

int
affs_copy_name(unsigned char *bstr, const unsigned char *name)
{
	int	 len;

	for (len = 0; len < 30; len++) {
		bstr[len + 1] = name[len];
		if (name[len] == '\0')
			break;
	}
	bstr[0] = len;
	return len;
}

/*
 *  linux/fs/affs/inode.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#define DEBUG 0
#include <asm/div64.h>
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
#include <linux/smp_lock.h>
#include <asm/system.h>
#include <asm/uaccess.h>

extern int *blk_size[];
extern struct timezone sys_tz;
extern struct inode_operations affs_symlink_inode_operations;

#define MIN(a,b) (((a)<(b))?(a):(b))

unsigned long
affs_parent_ino(struct inode *dir)
{
	int root_ino = (dir->i_sb->u.affs_sb.s_root_block);

	if (!S_ISDIR(dir->i_mode)) {
		affs_error(dir->i_sb,"parent_ino","Trying to get parent of non-directory");
		return root_ino;
	}
	if (dir->i_ino == root_ino)
		return root_ino;
	return dir->u.affs_i.i_parent;
}

void
affs_read_inode(struct inode *inode)
{
	struct buffer_head	*bh;
	struct file_front	*file_front;
	struct file_end		*file_end;
	s32			 block;
	unsigned long		 prot;
	s32			 ptype, stype;
	unsigned short		 id;
	loff_t		tmp;

	pr_debug("AFFS: read_inode(%lu)\n",inode->i_ino);

	block = inode->i_ino;
	if (!(bh = affs_bread(inode->i_dev,block,AFFS_I2BSIZE(inode)))) {
		affs_error(inode->i_sb,"read_inode","Cannot read block %d",block);
		return;
	}
	if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&ptype,&stype) || ptype != T_SHORT) {
		affs_error(inode->i_sb,"read_inode",
			   "Checksum or type (ptype=%d) error on inode %d",ptype,block);
		affs_brelse(bh);
		return;
	}

	file_front = (struct file_front *)bh->b_data;
	file_end   = GET_END_PTR(struct file_end, bh->b_data,AFFS_I2BSIZE(inode));
	prot       = (be32_to_cpu(file_end->protect) & ~0x10) ^ FIBF_OWNER;

	inode->u.affs_i.i_protect      = prot;
	inode->u.affs_i.i_parent       = be32_to_cpu(file_end->parent);
	inode->u.affs_i.i_original     = 0;
	inode->u.affs_i.i_zone         = 0;
	inode->u.affs_i.i_hlink        = 0;
	inode->u.affs_i.i_pa_cnt       = 0;
	inode->u.affs_i.i_pa_next      = 0;
	inode->u.affs_i.i_pa_last      = 0;
	inode->u.affs_i.i_ec           = NULL;
	inode->u.affs_i.i_lastblock    = -1;
	inode->i_nlink                 = 1;
	inode->i_mode                  = 0;

	if (inode->i_sb->u.affs_sb.s_flags & SF_SETMODE)
		inode->i_mode = inode->i_sb->u.affs_sb.s_mode;
	else
		inode->i_mode = prot_to_mode(prot);

	if (inode->i_sb->u.affs_sb.s_flags & SF_SETUID)
		inode->i_uid = inode->i_sb->u.affs_sb.s_uid;
	id = be16_to_cpu(file_end->owner_uid);
	if (id == 0 || inode->i_sb->u.affs_sb.s_flags & SF_SETUID)
		inode->i_uid = inode->i_sb->u.affs_sb.s_uid;
	else if (id == 0xFFFF && inode->i_sb->u.affs_sb.s_flags & SF_MUFS)
		inode->i_uid = 0;
	else 
		inode->i_uid = id;

	id = be16_to_cpu(file_end->owner_gid);
	if (id == 0 || inode->i_sb->u.affs_sb.s_flags & SF_SETGID)
		inode->i_gid = inode->i_sb->u.affs_sb.s_gid;
	else if (id == 0xFFFF && inode->i_sb->u.affs_sb.s_flags & SF_MUFS)
		inode->i_gid = 0;
	else
		inode->i_gid = id;

	switch (be32_to_cpu(file_end->secondary_type)) {
		case ST_ROOT:
			inode->i_uid   = inode->i_sb->u.affs_sb.s_uid;
			inode->i_gid   = inode->i_sb->u.affs_sb.s_gid;
		case ST_USERDIR:
			if (be32_to_cpu(file_end->secondary_type) == ST_USERDIR ||
			    inode->i_sb->u.affs_sb.s_flags & SF_SETMODE) {
				if (inode->i_mode & S_IRUSR)
					inode->i_mode |= S_IXUSR;
				if (inode->i_mode & S_IRGRP)
					inode->i_mode |= S_IXGRP;
				if (inode->i_mode & S_IROTH)
					inode->i_mode |= S_IXOTH;
				inode->i_mode |= S_IFDIR;
			} else
				inode->i_mode = S_IRUGO | S_IXUGO | S_IWUSR | S_IFDIR;
			inode->i_size  = 0;
			break;
		case ST_LINKDIR:
			affs_error(inode->i_sb,"read_inode","inode is LINKDIR");
			affs_brelse(bh);
			return;
		case ST_LINKFILE:
			affs_error(inode->i_sb,"read_inode","inode is LINKFILE");
			affs_brelse(bh);
			return;
		case ST_FILE:
			inode->i_mode |= S_IFREG;
			inode->i_size  = be32_to_cpu(file_end->byte_size);
			if (inode->i_sb->u.affs_sb.s_flags & SF_OFS)
				block = AFFS_I2BSIZE(inode) - 24;
			else
				block = AFFS_I2BSIZE(inode);
			tmp = inode->i_size + block -1;
			do_div (tmp, block);
			tmp--;
			inode->u.affs_i.i_lastblock = tmp;
			break;
		case ST_SOFTLINK:
			inode->i_mode |= S_IFLNK;
			inode->i_size  = 0;
			break;
	}

	inode->i_mtime = inode->i_atime = inode->i_ctime
		       = (be32_to_cpu(file_end->created.ds_Days) * (24 * 60 * 60) +
		         be32_to_cpu(file_end->created.ds_Minute) * 60 +
			 be32_to_cpu(file_end->created.ds_Tick) / 50 +
			 ((8 * 365 + 2) * 24 * 60 * 60)) +
			 sys_tz.tz_minuteswest * 60;
	affs_brelse(bh);

	if (S_ISREG(inode->i_mode)) {
		if (inode->i_sb->u.affs_sb.s_flags & SF_OFS) {
			inode->i_op = &affs_file_inode_operations;
			inode->i_fop = &affs_file_operations_ofs;
			return;
		}
		inode->i_op = &affs_file_inode_operations;
		inode->i_fop = &affs_file_operations;
		inode->i_mapping->a_ops = &affs_aops;
		inode->u.affs_i.mmu_private = inode->i_size;
	} else if (S_ISDIR(inode->i_mode)) {
		/* Maybe it should be controlled by mount parameter? */
		inode->i_mode |= S_ISVTX;
		inode->i_op = &affs_dir_inode_operations;
		inode->i_fop = &affs_dir_operations;
	}
	else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &affs_symlink_inode_operations;
		inode->i_data.a_ops = &affs_symlink_aops;
	}
}

void
affs_write_inode(struct inode *inode, int unused)
{
	struct buffer_head	*bh;
	struct file_end		*file_end;
	uid_t			 uid;
	gid_t			 gid;

	pr_debug("AFFS: write_inode(%lu)\n",inode->i_ino);

	if (!inode->i_nlink)
		return;
	lock_kernel();
	if (!(bh = bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode)))) {
		affs_error(inode->i_sb,"write_inode","Cannot read block %lu",inode->i_ino);
		unlock_kernel();
		return;
	}
	file_end = GET_END_PTR(struct file_end, bh->b_data,AFFS_I2BSIZE(inode));
	if (file_end->secondary_type == be32_to_cpu(ST_ROOT)) {
		secs_to_datestamp(inode->i_mtime,&ROOT_END(bh->b_data,inode)->disk_altered);
	} else {
		file_end->protect   = cpu_to_be32(inode->u.affs_i.i_protect ^ FIBF_OWNER);
		file_end->byte_size = cpu_to_be32(inode->i_size);
		secs_to_datestamp(inode->i_mtime,&file_end->created);
		if (!(inode->i_ino == inode->i_sb->u.affs_sb.s_root_block)) {
			uid = inode->i_uid;
			gid = inode->i_gid;
			if (inode->i_sb->u.affs_sb.s_flags & SF_MUFS) {
				if (inode->i_uid == 0 || inode->i_uid == 0xFFFF)
					uid = inode->i_uid ^ ~0;
				if (inode->i_gid == 0 || inode->i_gid == 0xFFFF)
					gid = inode->i_gid ^ ~0;
			}
			if (!(inode->i_sb->u.affs_sb.s_flags & SF_SETUID))
				file_end->owner_uid = cpu_to_be16(uid);
			if (!(inode->i_sb->u.affs_sb.s_flags & SF_SETGID))
				file_end->owner_gid = cpu_to_be16(gid);
		}
	}
	affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
	mark_buffer_dirty(bh);
	brelse(bh);
	unlock_kernel();
}

int
affs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	pr_debug("AFFS: notify_change(%lu,0x%x)\n",inode->i_ino,attr->ia_valid);

	error = inode_change_ok(inode,attr);
	if (error)
		goto out;

	if (((attr->ia_valid & ATTR_UID) && (inode->i_sb->u.affs_sb.s_flags & SF_SETUID)) ||
	    ((attr->ia_valid & ATTR_GID) && (inode->i_sb->u.affs_sb.s_flags & SF_SETGID)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (inode->i_sb->u.affs_sb.s_flags & (SF_SETMODE | SF_IMMUTABLE)))) {
		if (!(inode->i_sb->u.affs_sb.s_flags & SF_QUIET))
			error = -EPERM;
		goto out;
	}

	if (attr->ia_valid & ATTR_MODE)
		inode->u.affs_i.i_protect = mode_to_prot(attr->ia_mode);

	error = 0;
	inode_setattr(inode, attr);
out:
	return error;
}

void
affs_put_inode(struct inode *inode)
{
	pr_debug("AFFS: put_inode(ino=%lu, nlink=%u)\n",
		inode->i_ino,inode->i_nlink);

	lock_kernel();
	affs_free_prealloc(inode);
	if (atomic_read(&inode->i_count) == 1) {
		unsigned long cache_page = (unsigned long) inode->u.affs_i.i_ec;
		if (cache_page) {
			pr_debug("AFFS: freeing ext cache\n");
			inode->u.affs_i.i_ec = NULL;
			free_page(cache_page);
		}
	}
	unlock_kernel();
}

void
affs_delete_inode(struct inode *inode)
{
	pr_debug("AFFS: delete_inode(ino=%lu, nlink=%u)\n",inode->i_ino,inode->i_nlink);
	lock_kernel();
	inode->i_size = 0;
	if (S_ISREG(inode->i_mode) && !inode->u.affs_i.i_hlink)
		affs_truncate(inode);
	affs_free_block(inode->i_sb,inode->i_ino);
	unlock_kernel();
	clear_inode(inode);
}

struct inode *
affs_new_inode(const struct inode *dir)
{
	struct inode		*inode;
	struct super_block	*sb;
	s32			 block;

	if (!dir)
		return NULL;

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return NULL;

	if (!(block = affs_new_header((struct inode *)dir))) {
		iput(inode);
		return NULL;
	}

	inode->i_uid     = current->fsuid;
	inode->i_gid     = current->fsgid;
	inode->i_ino     = block;
	inode->i_mtime   = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	inode->u.affs_i.i_original  = 0;
	inode->u.affs_i.i_parent    = dir->i_ino;
	inode->u.affs_i.i_zone      = 0;
	inode->u.affs_i.i_hlink     = 0;
	inode->u.affs_i.i_pa_cnt    = 0;
	inode->u.affs_i.i_pa_next   = 0;
	inode->u.affs_i.i_pa_last   = 0;
	inode->u.affs_i.i_ec        = NULL;
	inode->u.affs_i.i_lastblock = -1;

	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	return inode;
}

/*
 * Add an entry to a directory. Create the header block
 * and insert it into the hash table.
 */

int
affs_add_entry(struct inode *dir, struct inode *link, struct inode *inode,
	       struct dentry *dentry, int type)
{
	struct buffer_head	*dir_bh;
	struct buffer_head	*inode_bh;
	struct buffer_head	*link_bh;
	int			 retval;
	const unsigned char	*name = dentry->d_name.name;
	int			 len  = dentry->d_name.len;

	pr_debug("AFFS: add_entry(dir=%lu,inode=%lu,\"%*s\",type=%d)\n",dir->i_ino,inode->i_ino,
		 len,name,type);

	if ((retval = affs_check_name(name,len)))
		return retval;
	if (len > 30)
		len = 30;

	dir_bh   = affs_bread(dir->i_dev,dir->i_ino,AFFS_I2BSIZE(dir));
	inode_bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	link_bh  = NULL;
	retval   = -EIO;
	if (!dir_bh || !inode_bh)
		goto addentry_done;
	if (link) {
		link_bh = affs_bread(link->i_dev,link->i_ino,AFFS_I2BSIZE(link));
		if (!link_bh)
			goto addentry_done;
	}
	((struct dir_front *)inode_bh->b_data)->primary_type = cpu_to_be32(T_SHORT);
	((struct dir_front *)inode_bh->b_data)->own_key      = cpu_to_be32(inode->i_ino);
	DIR_END(inode_bh->b_data,inode)->dir_name[0]         = len;
	strncpy(DIR_END(inode_bh->b_data,inode)->dir_name + 1,name,len);
	DIR_END(inode_bh->b_data,inode)->secondary_type = cpu_to_be32(type);
	DIR_END(inode_bh->b_data,inode)->parent         = cpu_to_be32(dir->i_ino);

	lock_super(inode->i_sb);
	retval = affs_insert_hash(dir->i_ino,inode_bh,dir);

	if (link_bh) {
		LINK_END(inode_bh->b_data,inode)->original   = cpu_to_be32(link->i_ino);
		LINK_END(inode_bh->b_data,inode)->link_chain =
						FILE_END(link_bh->b_data,link)->link_chain;
		FILE_END(link_bh->b_data,link)->link_chain   = cpu_to_be32(inode->i_ino);
		affs_fix_checksum(AFFS_I2BSIZE(link),link_bh->b_data,5);
		link->i_version = ++event;
		mark_inode_dirty(link);
		mark_buffer_dirty(link_bh);
	}
	affs_fix_checksum(AFFS_I2BSIZE(inode),inode_bh->b_data,5);
	affs_fix_checksum(AFFS_I2BSIZE(dir),dir_bh->b_data,5);
	dir->i_version = ++event;
	dir->i_mtime   = dir->i_atime = dir->i_ctime = CURRENT_TIME;
	unlock_super(inode->i_sb);

	mark_inode_dirty(dir);
	mark_inode_dirty(inode);
	mark_buffer_dirty(dir_bh);
	mark_buffer_dirty(inode_bh);

addentry_done:
	affs_brelse(dir_bh);
	affs_brelse(inode_bh);
	affs_brelse(link_bh);

	return retval;
}

/*
 *  linux/fs/affs/namei.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#define DEBUG 0
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/locks.h>
#include <linux/amigaffs.h>
#include <asm/uaccess.h>

#include <linux/errno.h>

extern struct inode_operations affs_symlink_inode_operations;

/* Simple toupper() for DOS\1 */

static unsigned int
affs_toupper(unsigned int ch)
{
	return ch >= 'a' && ch <= 'z' ? ch -= ('a' - 'A') : ch;
}

/* International toupper() for DOS\3 ("international") */

static unsigned int
affs_intl_toupper(unsigned int ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 0xE0
		&& ch <= 0xFE && ch != 0xF7) ?
		ch - ('a' - 'A') : ch;
}

static int	 affs_hash_dentry(struct dentry *, struct qstr *);
static int       affs_compare_dentry(struct dentry *, struct qstr *, struct qstr *);
struct dentry_operations affs_dentry_operations = {
	d_hash:		affs_hash_dentry,
	d_compare:	affs_compare_dentry,
};

/*
 * Note: the dentry argument is the parent dentry.
 */
static int
affs_hash_dentry(struct dentry *dentry, struct qstr *qstr)
{
	unsigned int (*toupper)(unsigned int) = affs_toupper;
	unsigned long	 hash;
	int		 i;

	if ((i = affs_check_name(qstr->name,qstr->len)))
		return i;

	/* Check whether to use the international 'toupper' routine */
	if (AFFS_I2FSTYPE(dentry->d_inode))
		toupper = affs_intl_toupper;
	hash = init_name_hash();
	for (i = 0; i < qstr->len && i < 30; i++)
		hash = partial_name_hash(toupper(qstr->name[i]), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

static int
affs_compare_dentry(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	unsigned int (*toupper)(unsigned int) = affs_toupper;
	int	 alen = a->len;
	int      blen = b->len;
	int	 i;

	/* 'a' is the qstr of an already existing dentry, so the name
	 * must be valid. 'b' must be validated first.
	 */
	
	if (affs_check_name(b->name,b->len))
		return 1;

	/* If the names are longer than the allowed 30 chars,
	 * the excess is ignored, so their length may differ.
	 */
	if (alen > 30)
		alen = 30;
	if (blen > 30)
		blen = 30;
	if (alen != blen)
		return 1;

	/* Check whether to use the international 'toupper' routine */
	if (AFFS_I2FSTYPE(dentry->d_inode))
		toupper = affs_intl_toupper;

	for (i = 0; i < alen; i++)
		if (toupper(a->name[i]) != toupper(b->name[i]))
			return 1;
	
	return 0;
}

/*
 * NOTE! unlike strncmp, affs_match returns 1 for success, 0 for failure.
 */

static int
affs_match(const unsigned char *name, int len, const unsigned char *compare, int dlen, int intl)
{
	unsigned int	(*toupper)(unsigned int) = intl ? affs_intl_toupper : affs_toupper;
	int		  i;

	if (!compare)
		return 0;

	if (len > 30)
		len = 30;
	if (dlen > 30)
		dlen = 30;

	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && dlen == 1 && compare[0] == '.')
		return 1;
	if (dlen != len)
		return 0;
	for (i = 0; i < len; i++)
		if (toupper(name[i]) != toupper(compare[i]))
			return 0;
	return 1;
}

int
affs_hash_name(const unsigned char *name, int len, int intl, int hashsize)
{
	unsigned int i, x;

	if (len > 30)
		len = 30;

	x = len;
	for (i = 0; i < len; i++)
		if (intl)
			x = (x * 13 + affs_intl_toupper(name[i] & 0xFF)) & 0x7ff;
		else
			x = (x * 13 + affs_toupper(name[i] & 0xFF)) & 0x7ff;

	return x % hashsize;
}

static struct buffer_head *
affs_find_entry(struct inode *dir, struct dentry *dentry, unsigned long *ino)
{
	struct buffer_head	*bh;
	int			 intl = AFFS_I2FSTYPE(dir);
	s32			 key;
	const char		*name = dentry->d_name.name;
	int			 namelen = dentry->d_name.len;

	pr_debug("AFFS: find_entry(\"%.*s\")\n",namelen,name);

	bh = affs_bread(dir->i_dev,dir->i_ino,AFFS_I2BSIZE(dir));
	if (!bh)
		return NULL;

	if (namelen == 1 && name[0] == '.') {
		*ino = dir->i_ino;
		return bh;
	}
	if (namelen == 2 && name[0] == '.' && name[1] == '.') {
		*ino = affs_parent_ino(dir);
		return bh;
	}

	key = AFFS_GET_HASHENTRY(bh->b_data,affs_hash_name(name,namelen,intl,AFFS_I2HSIZE(dir)));

	for (;;) {
		unsigned char *cname;
		int cnamelen;

		affs_brelse(bh);
		bh = NULL;
		if (key == 0)
			break;
		bh = affs_bread(dir->i_dev,key,AFFS_I2BSIZE(dir));
		if (!bh)
			break;
		cnamelen = affs_get_file_name(AFFS_I2BSIZE(dir),bh->b_data,&cname);
		if (affs_match(name,namelen,cname,cnamelen,intl))
			break;
		key = be32_to_cpu(FILE_END(bh->b_data,dir)->hash_chain);
	}
	*ino = key;
	return bh;
}

struct dentry *
affs_lookup(struct inode *dir, struct dentry *dentry)
{
	unsigned long		 ino;
	struct buffer_head	*bh;
	struct inode		*inode;

	pr_debug("AFFS: lookup(\"%.*s\")\n",(int)dentry->d_name.len,dentry->d_name.name);

	inode = NULL;
	bh = affs_find_entry(dir,dentry,&ino);
	if (bh) {
		if (FILE_END(bh->b_data,dir)->original)
			ino = be32_to_cpu(FILE_END(bh->b_data,dir)->original);
		affs_brelse(bh);
		inode = iget(dir->i_sb,ino);
		if (!inode)
			return ERR_PTR(-EACCES);
	}
	dentry->d_op = &affs_dentry_operations;
	d_add(dentry,inode);
	return NULL;
}

int
affs_unlink(struct inode *dir, struct dentry *dentry)
{
	int			 retval;
	struct buffer_head	*bh;
	unsigned long		 ino;
	struct inode		*inode;

	pr_debug("AFFS: unlink(dir=%ld,\"%.*s\")\n",dir->i_ino,
		 (int)dentry->d_name.len,dentry->d_name.name);

	retval  = -ENOENT;
	if (!(bh = affs_find_entry(dir,dentry,&ino)))
		goto unlink_done;

	inode  = dentry->d_inode;

	if ((retval = affs_remove_header(bh,inode)) < 0)
		goto unlink_done;
	
	inode->i_nlink = retval;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_version = ++event;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	retval = 0;

unlink_done:
	affs_brelse(bh);
	return retval;
}

int
affs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode	*inode;
	int		 error;
	
	pr_debug("AFFS: create(%lu,\"%.*s\",0%o)\n",dir->i_ino,(int)dentry->d_name.len,
		 dentry->d_name.name,mode);

	error = -ENOSPC;
	inode = affs_new_inode(dir);
	if (!inode)
		goto out;

	pr_debug("AFFS: ino=%lu\n",inode->i_ino);
	if (dir->i_sb->u.affs_sb.s_flags & SF_OFS) {
		inode->i_op = &affs_file_inode_operations;
		inode->i_fop = &affs_file_operations_ofs;
	} else {
		inode->i_op = &affs_file_inode_operations;
		inode->i_fop = &affs_file_operations;
		inode->i_mapping->a_ops = &affs_aops;
		inode->u.affs_i.mmu_private = inode->i_size;
	}
	error = affs_add_entry(dir,NULL,inode,dentry,ST_FILE);
	if (error)
		goto out_iput;
	inode->i_mode = mode;
	inode->u.affs_i.i_protect = mode_to_prot(inode->i_mode);
	d_instantiate(dentry,inode);
	mark_inode_dirty(inode);
	dir->i_version = ++event;
	mark_inode_dirty(dir);
out:
	return error;

out_iput:
	inode->i_nlink = 0;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

int
affs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode		*inode;
	int			 error;
	
	pr_debug("AFFS: mkdir(%lu,\"%.*s\",0%o)\n",dir->i_ino,
		 (int)dentry->d_name.len,dentry->d_name.name,mode);

	error = -ENOSPC;
	inode = affs_new_inode(dir);
	if (!inode)
		goto out;

	inode->i_op = &affs_dir_inode_operations;
	inode->i_fop = &affs_dir_operations;
	error       = affs_add_entry(dir,NULL,inode,dentry,ST_USERDIR);
	if (error)
		goto out_iput;
	inode->i_mode = S_IFDIR | S_ISVTX | mode;
	inode->u.affs_i.i_protect = mode_to_prot(inode->i_mode);
	d_instantiate(dentry,inode);
	mark_inode_dirty(inode);
	dir->i_version = ++event;
	mark_inode_dirty(dir);
out:
	return error;

out_iput:
	inode->i_nlink = 0;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

static int
empty_dir(struct buffer_head *bh, int hashsize)
{
	while (--hashsize >= 0) {
		if (((struct dir_front *)bh->b_data)->hashtable[hashsize])
			return 0;
	}
	return 1;
}

int
affs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode		*inode = dentry->d_inode;
	int			 retval;
	unsigned long		 ino;
	struct buffer_head	*bh;

	pr_debug("AFFS: rmdir(dir=%lu,\"%.*s\")\n",dir->i_ino,
		 (int)dentry->d_name.len,dentry->d_name.name);

	retval = -ENOENT;
	if (!(bh = affs_find_entry(dir,dentry,&ino)))
		goto rmdir_done;

	/*
	 * Make sure the directory is empty and the dentry isn't busy.
	 */
	retval = -ENOTEMPTY;
	if (!empty_dir(bh,AFFS_I2HSIZE(inode)))
		goto rmdir_done;
	retval = -EBUSY;
	if (!d_unhashed(dentry))
		goto rmdir_done;

	if ((retval = affs_remove_header(bh,inode)) < 0)
		goto rmdir_done;
	
	inode->i_nlink = retval;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	retval         = 0;
	dir->i_version = ++event;
	mark_inode_dirty(dir);
	mark_inode_dirty(inode);

rmdir_done:
	affs_brelse(bh);
	return retval;
}

int
affs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct buffer_head	*bh;
	struct inode		*inode;
	char			*p;
	unsigned long		 tmp;
	int			 i, maxlen, error;
	char			 c, lc;

	pr_debug("AFFS: symlink(%lu,\"%.*s\" -> \"%s\")\n",dir->i_ino,
		 (int)dentry->d_name.len,dentry->d_name.name,symname);
	
	maxlen = 4 * AFFS_I2HSIZE(dir) - 1;
	error = -ENOSPC;
	inode  = affs_new_inode(dir);
	if (!inode)
		goto out;

	inode->i_op = &affs_symlink_inode_operations;
	inode->i_data.a_ops = &affs_symlink_aops;
	inode->i_mode = S_IFLNK | 0777;
	inode->u.affs_i.i_protect = mode_to_prot(inode->i_mode);
	error = -EIO;
	bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	if (!bh)
		goto out_iput;
	i  = 0;
	p  = ((struct slink_front *)bh->b_data)->symname;
	lc = '/';
	if (*symname == '/') {
		while (*symname == '/')
			symname++;
		while (inode->i_sb->u.affs_sb.s_volume[i])	/* Cannot overflow */
			*p++ = inode->i_sb->u.affs_sb.s_volume[i++];
	}
	while (i < maxlen && (c = *symname++)) {
		if (c == '.' && lc == '/' && *symname == '.' && symname[1] == '/') {
			*p++ = '/';
			i++;
			symname += 2;
			lc = '/';
		} else if (c == '.' && lc == '/' && *symname == '/') {
			symname++;
			lc = '/';
		} else {
			*p++ = c;
			lc   = c;
			i++;
		}
		if (lc == '/')
			while (*symname == '/')
				symname++;
	}
	*p = 0;
	mark_buffer_dirty(bh);
	affs_brelse(bh);
	mark_inode_dirty(inode);

	/* N.B. This test shouldn't be necessary ... dentry must be negative */
	error = -EEXIST;
	bh = affs_find_entry(dir,dentry,&tmp);
	if (bh)
		goto out_release;
	/* N.B. Shouldn't we add the entry before dirtying the buffer? */
	error = affs_add_entry(dir,NULL,inode,dentry,ST_SOFTLINK);
	if (error)
		goto out_release;
	d_instantiate(dentry,inode);
	dir->i_version = ++event;
	mark_inode_dirty(dir);

out:
	return error;

out_release:
	affs_brelse(bh);
out_iput:
	inode->i_nlink = 0;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

int
affs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode		*oldinode = old_dentry->d_inode;
	struct inode		*inode;
	struct buffer_head	*bh;
	unsigned long		 i;
	int			 error;
	
	pr_debug("AFFS: link(%lu,%lu,\"%.*s\")\n",oldinode->i_ino,dir->i_ino,
		 (int)dentry->d_name.len,dentry->d_name.name);

	/* N.B. Do we need this test? The dentry must be negative ... */
	bh = affs_find_entry(dir,dentry,&i);
	if (bh) {
		affs_brelse(bh);
		return -EEXIST;
	}
	if (oldinode->u.affs_i.i_hlink)	{	/* Cannot happen */
		affs_warning(dir->i_sb,"link","Impossible link to link");
		return -EINVAL;
	}
	error = -ENOSPC;
	if (!(inode = affs_new_inode(dir)))
		goto out;

	inode->i_op                = oldinode->i_op;
	inode->i_fop               = oldinode->i_fop;
	inode->u.affs_i.i_protect  = mode_to_prot(oldinode->i_mode);
	inode->u.affs_i.i_original = oldinode->i_ino;
	inode->u.affs_i.i_hlink    = 1;
	inode->i_mtime             = oldinode->i_mtime;

	if (S_ISDIR(oldinode->i_mode))
		error = affs_add_entry(dir,oldinode,inode,dentry,ST_LINKDIR);
	else
		error = affs_add_entry(dir,oldinode,inode,dentry,ST_LINKFILE);
	if (error)
		inode->i_nlink = 0;
	else {
		dir->i_version = ++event;
		mark_inode_dirty(dir);
		mark_inode_dirty(oldinode);
		atomic_inc(&oldinode->i_count);
		d_instantiate(dentry,oldinode);
	}
	mark_inode_dirty(inode);
	iput(inode);

out:
	return error;
}

int
affs_rename(struct inode *old_dir, struct dentry *old_dentry,
	    struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode		*old_inode = old_dentry->d_inode;
	struct inode		*new_inode = new_dentry->d_inode;
	struct buffer_head	*old_bh;
	struct buffer_head	*new_bh;
	unsigned long		 old_ino;
	unsigned long		 new_ino;
	int			 retval;

	pr_debug("AFFS: rename(old=%lu,\"%*s\" (inode=%p) to new=%lu,\"%*s\" (inode=%p))\n",
		 old_dir->i_ino,old_dentry->d_name.len,old_dentry->d_name.name,old_inode,
		 new_dir->i_ino,new_dentry->d_name.len,new_dentry->d_name.name,new_inode);
	
	if ((retval = affs_check_name(new_dentry->d_name.name,new_dentry->d_name.len)))
		goto out;

	new_bh = NULL;
	retval = -ENOENT;
	old_bh = affs_find_entry(old_dir,old_dentry,&old_ino);
	if (!old_bh)
		goto end_rename;

	new_bh = affs_find_entry(new_dir,new_dentry,&new_ino);
	if (new_bh && !new_inode) {
		affs_error(old_inode->i_sb,"affs_rename",
			   "No inode for entry found (key=%lu)\n",new_ino);
		goto end_rename;
	}
	if (S_ISDIR(old_inode->i_mode)) {
		if (new_inode) {
			retval = -EBUSY;
			if (!d_unhashed(new_dentry))
				goto end_rename;
			retval = -ENOTEMPTY;
			if (!empty_dir(new_bh,AFFS_I2HSIZE(new_inode)))
				goto end_rename;
		}

		retval = -ENOENT;
		if (affs_parent_ino(old_inode) != old_dir->i_ino)
			goto end_rename;
	}
	/* Unlink destination if it already exists */
	if (new_inode) {
		if ((retval = affs_remove_header(new_bh,new_dir)) < 0)
			goto end_rename;
		new_inode->i_nlink = retval;
		mark_inode_dirty(new_inode);
		if (new_inode->i_ino == new_ino)
			new_inode->i_nlink = 0;
	}
	/* Remove header from its parent directory. */
	if ((retval = affs_remove_hash(old_bh,old_dir)))
		goto end_rename;
	/* And insert it into the new directory with the new name. */
	affs_copy_name(FILE_END(old_bh->b_data,old_inode)->file_name,new_dentry->d_name.name);
	if ((retval = affs_insert_hash(new_dir->i_ino,old_bh,new_dir)))
		goto end_rename;
	affs_fix_checksum(AFFS_I2BSIZE(new_dir),old_bh->b_data,5);

	new_dir->i_ctime   = new_dir->i_mtime = old_dir->i_ctime
			   = old_dir->i_mtime = CURRENT_TIME;
	new_dir->i_version = ++event;
	old_dir->i_version = ++event;
	retval             = 0;
	mark_inode_dirty(new_dir);
	mark_inode_dirty(old_dir);
	mark_buffer_dirty(old_bh);
	
end_rename:
	affs_brelse(old_bh);
	affs_brelse(new_bh);
out:
	return retval;
}

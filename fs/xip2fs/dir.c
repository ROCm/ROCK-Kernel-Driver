/*
 *  linux/fs/xip2fs/dir.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */

#include "xip2.h"
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

typedef struct ext2_dir_entry_2 ext2_dirent;

/*
 * ext2 uses block-sized chunks. Arguably, sector-sized ones would be
 * more robust, but we have what we have
 */
static inline unsigned xip2_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

static inline unsigned long dir_pages(struct inode *inode)
{
	return (inode->i_size+PAGE_CACHE_SIZE-1)>>PAGE_CACHE_SHIFT;
}

/*
 * Return the offset into page `page_nr' of the last valid
 * byte in that page, plus one.
 */
static unsigned
xip2_last_byte(struct inode *inode, unsigned long page_nr)
{
	unsigned last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_CACHE_SHIFT;
	if (last_byte > PAGE_CACHE_SIZE)
		last_byte = PAGE_CACHE_SIZE;
	return last_byte;
}

static int xip2_check_page(struct inode* dir, void *kaddr)
{
	struct super_block *sb = dir->i_sb;
	unsigned chunk_size = xip2_chunk_size(dir);
	u32 max_inumber = le32_to_cpu(XIP2_SB(sb)->s_es->s_inodes_count);
	unsigned offs, rec_len;
	unsigned limit = PAGE_CACHE_SIZE;
	ext2_dirent *p;
	char *error;

	for (offs = 0; offs <= limit - EXT2_DIR_REC_LEN(1); offs += rec_len) {
		p = (ext2_dirent *)(kaddr + offs);
		rec_len = le16_to_cpu(p->rec_len);

		if (rec_len < EXT2_DIR_REC_LEN(1))
			goto Eshort;
		if (rec_len & 3)
			goto Ealign;
		if (rec_len < EXT2_DIR_REC_LEN(p->name_len))
			goto Enamelen;
		if (((offs + rec_len - 1) ^ offs) & ~(chunk_size-1))
			goto Espan;
		if (le32_to_cpu(p->inode) > max_inumber)
			goto Einumber;
	}
	if (offs != limit)
		goto Eend;
	return 0;

	/* Too bad, we had an error */
Eshort:
	error = "rec_len is smaller than minimal";
	goto bad_entry;
Ealign:
	error = "unaligned directory entry";
	goto bad_entry;
Enamelen:
	error = "rec_len is too small for name_len";
	goto bad_entry;
Espan:
	error = "directory entry across blocks";
	goto bad_entry;
Einumber:
	error = "inode out of bounds";
bad_entry:
	xip2_error (sb, "xip2_check_page", "bad entry in directory #%lu: %s - "
		"ptr=%p, inode=%lu, rec_len=%d, name_len=%d",
		dir->i_ino, error, kaddr+offs,
		(unsigned long) le32_to_cpu(p->inode),
		rec_len, p->name_len);
	goto fail;
Eend:
	p = (ext2_dirent *)(kaddr + offs);
	xip2_error (sb, "xip2_check_page",
		"entry in directory #%lu spans the page boundary"
		"ptr=%p, inode=%lu",
		dir->i_ino, kaddr+offs,
		(unsigned long) le32_to_cpu(p->inode));
fail:
	return -EINVAL;
}

static void* xip2_dir_bread (struct inode *dir, unsigned long n)
{
	sector_t blockno;
	void *result;
	int rc;
	rc = xip2_get_block (dir, n, &blockno, 0);
	if (rc)
		return NULL;
	result = xip2_sb_bread (dir->i_sb, blockno);
	if (xip2_check_page (dir, result)) 
		return NULL;
	return result;
}

/*
 * NOTE! unlike strncmp, xip2_match returns 1 for success, 0 for failure.
 *
 * len <= EXT2_NAME_LEN and de != NULL are guaranteed by caller.
 */
static inline int xip2_match (int len, const char * const name,
					struct ext2_dir_entry_2 * de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*
 * p is at least 6 bytes before the end of page
 */
static inline ext2_dirent *xip2_next_entry(ext2_dirent *p)
{
	return (ext2_dirent *)((char*)p + le16_to_cpu(p->rec_len));
}

static inline unsigned 
xip2_validate_entry(char *base, unsigned offset, unsigned mask)
{
	ext2_dirent *de = (ext2_dirent*)(base + offset);
	ext2_dirent *p = (ext2_dirent*)(base + (offset&mask));
	while ((char*)p < (char*)de) {
		if (p->rec_len == 0)
			break;
		p = xip2_next_entry(p);
	}
	return (char *)p - base;
}

static unsigned char xip2_filetype_table[EXT2_FT_MAX] = {
	[EXT2_FT_UNKNOWN]	= DT_UNKNOWN,
	[EXT2_FT_REG_FILE]	= DT_REG,
	[EXT2_FT_DIR]		= DT_DIR,
	[EXT2_FT_CHRDEV]	= DT_CHR,
	[EXT2_FT_BLKDEV]	= DT_BLK,
	[EXT2_FT_FIFO]		= DT_FIFO,
	[EXT2_FT_SOCK]		= DT_SOCK,
	[EXT2_FT_SYMLINK]	= DT_LNK,
};

#define S_SHIFT 12
static unsigned char xip2_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= EXT2_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= EXT2_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= EXT2_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= EXT2_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= EXT2_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= EXT2_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= EXT2_FT_SYMLINK,
};

static inline void xip2_set_de_type(ext2_dirent *de, struct inode *inode)
{
	mode_t mode = inode->i_mode;
	if (XIP2_HAS_INCOMPAT_FEATURE(inode->i_sb,
				      EXT2_FEATURE_INCOMPAT_FILETYPE))
		de->file_type = xip2_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
	else
		de->file_type = 0;
}

static int
xip2_readdir (struct file * filp, void * dirent, filldir_t filldir)
{
	loff_t pos = filp->f_pos;
	struct inode *inode = filp->f_dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	unsigned offset = pos & ~PAGE_CACHE_MASK;
	unsigned long n = pos >> PAGE_CACHE_SHIFT;
	unsigned long npages = dir_pages(inode);
	unsigned chunk_mask = ~(xip2_chunk_size(inode)-1);
	unsigned char *types = NULL;
	int need_revalidate = (filp->f_version != inode->i_version);
	int ret = 0;

	if (pos > inode->i_size - EXT2_DIR_REC_LEN(1))
		goto done;

	if (XIP2_HAS_INCOMPAT_FEATURE(sb, EXT2_FEATURE_INCOMPAT_FILETYPE))
		types = xip2_filetype_table;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		ext2_dirent *de;
		kaddr = xip2_dir_bread (inode,n);

		if (kaddr == NULL)
			continue;
		if (need_revalidate) {
			offset = xip2_validate_entry(kaddr, offset, chunk_mask);
			need_revalidate = 0;
		}
		de = (ext2_dirent *)(kaddr+offset);
		limit = kaddr + xip2_last_byte(inode, n) - EXT2_DIR_REC_LEN(1);
		for ( ;(char*)de <= limit; de = xip2_next_entry(de)) {
			if (de->rec_len == 0) {
				xip2_error(sb, __FUNCTION__,
					"zero-length directory entry");
				ret = -EIO;
				goto done;
			}
			if (de->inode) {
				int over;
				unsigned char d_type = DT_UNKNOWN;

				if (types && de->file_type < EXT2_FT_MAX)
					d_type = types[de->file_type];

				offset = (char *)de - kaddr;
				over = filldir(dirent, de->name, de->name_len,
						(n<<PAGE_CACHE_SHIFT) | offset,
						le32_to_cpu(de->inode), d_type);
				if (over) {
					goto done;
				}
			}
		}
	}

done:
	filp->f_pos = (n << PAGE_CACHE_SHIFT) | offset;
	filp->f_version = inode->i_version;
	return 0;
}

/*
 *	xip2_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the page in which the entry was found, and the entry itself
 * (as a parameter - res_dir). Page is returned mapped and unlocked.
 * Entry is guaranteed to be valid.
 */
static struct ext2_dir_entry_2 * xip2_find_entry (struct inode * dir,
			struct dentry *dentry)
{
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned reclen = EXT2_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct xip2_inode_info *ei = XIP2_I(dir);
	ext2_dirent * de;

	if (npages == 0)
		goto out;

	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr;
		kaddr = xip2_dir_bread(dir, n);
		if (kaddr != NULL) {
			de = (ext2_dirent *) kaddr;
			kaddr += xip2_last_byte(dir, n) - reclen;
			while ((char *) de <= kaddr) {
				if (de->rec_len == 0) {
					xip2_error(dir->i_sb, __FUNCTION__,
						"zero-length directory entry");
					goto out;
				}
				if (xip2_match (namelen, name, de))
					goto found;
				de = xip2_next_entry(de);
			}
		}
		if (++n >= npages)
			n = 0;
	} while (n != start);
out:
	return NULL;

found:
	ei->i_dir_start_lookup = n;
	return de;
}

ino_t xip2_inode_by_name(struct inode * dir, struct dentry *dentry)
{
	ino_t res = 0;
	struct ext2_dir_entry_2 * de;
	
	de = xip2_find_entry (dir, dentry);
	if (de) {
		res = le32_to_cpu(de->inode);
	}
	return res;
}

struct file_operations xip2_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= xip2_readdir,
	.ioctl		= xip2_ioctl,
};

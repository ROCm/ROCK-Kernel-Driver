/*
 * hugetlbpage-backed filesystem.  Based on ramfs.
 *
 * William Irwin, 2002
 *
 * Copyright (C) 2002 Linus Torvalds.
 */

#include <linux/module.h>
#include <linux/thread_info.h>
#include <asm/current.h>
#include <linux/sched.h>		/* remove ASAP */
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/hugetlb.h>
#include <linux/pagevec.h>
#include <linux/quotaops.h>
#include <linux/dnotify.h>
#include <linux/security.h>

#include <asm/uaccess.h>

/* some random number */
#define HUGETLBFS_MAGIC	0x958458f6

static struct super_operations hugetlbfs_ops;
static struct address_space_operations hugetlbfs_aops;
struct file_operations hugetlbfs_file_operations;
static struct inode_operations hugetlbfs_dir_inode_operations;

static struct backing_dev_info hugetlbfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};

static int hugetlbfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode =file->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	size_t len;
	int ret;

	if (!capable(CAP_IPC_LOCK))
		return -EPERM;

	if (vma->vm_start & ~HPAGE_MASK)
		return -EINVAL;

	if (vma->vm_end & ~HPAGE_MASK)
		return -EINVAL;

	if (vma->vm_end - vma->vm_start < HPAGE_SIZE)
		return -EINVAL;

	down(&inode->i_sem);

	UPDATE_ATIME(inode);
	vma->vm_flags |= VM_HUGETLB | VM_RESERVED;
	vma->vm_ops = &hugetlb_vm_ops;
	ret = hugetlb_prefault(mapping, vma);
	len = (vma->vm_end - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);
	if (inode->i_size < len)
		inode->i_size = len;

	up(&inode->i_sem);
	return ret;
}

/*
 * Read a page. Again trivial. If it didn't already exist
 * in the page cache, it is zero-filled.
 */
static int hugetlbfs_readpage(struct file *file, struct page * page)
{
	unlock_page(page);
	return -EINVAL;
}

static int hugetlbfs_prepare_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	return -EINVAL;
}

static int hugetlbfs_commit_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	return -EINVAL;
}

void huge_pagevec_release(struct pagevec *pvec)
{
	int i;

	for (i = 0; i < pagevec_count(pvec); ++i)
		huge_page_release(pvec->pages[i]);

	pagevec_reinit(pvec);
}

void truncate_partial_hugepage(struct page *page, unsigned partial)
{
	int i;
	const unsigned piece = partial & (PAGE_SIZE - 1);
	const unsigned tailstart = PAGE_SIZE - piece;
	const unsigned whole_pages = partial / PAGE_SIZE;
	const unsigned last_page_offset = HPAGE_SIZE/PAGE_SIZE - whole_pages;

	for (i = HPAGE_SIZE/PAGE_SIZE - 1; i >= last_page_offset; ++i)
		memclear_highpage_flush(&page[i], 0, PAGE_SIZE);

	if (!piece)
		return;

	memclear_highpage_flush(&page[last_page_offset - 1], tailstart, piece);
}

void truncate_huge_page(struct address_space *mapping, struct page *page)
{
	if (page->mapping != mapping)
		return;

	clear_page_dirty(page);
	ClearPageUptodate(page);
	remove_from_page_cache(page);
	huge_page_release(page);
}

void truncate_hugepages(struct address_space *mapping, loff_t lstart)
{
	const pgoff_t start = (lstart + HPAGE_SIZE - 1) >> HPAGE_SHIFT;
	const unsigned partial = lstart & (HPAGE_SIZE - 1);
	struct pagevec pvec;
	pgoff_t next;
	int i;

	pagevec_init(&pvec, 0);
	next = start;

	while (pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); ++i) {
			struct page *page = pvec.pages[i];
			pgoff_t page_index = page->index;

			if (page_index > next)
				next = page_index;

			++next;

			if (TestSetPageLocked(page))
				continue;

			if (PageWriteback(page)) {
				unlock_page(page);
				continue;
			}

			truncate_huge_page(mapping, page);
			unlock_page(page);
		}
		huge_pagevec_release(&pvec);
		cond_resched();
	}

	if (partial) {
		struct page *page = find_lock_page(mapping, start - 1);
		if (page) {
			wait_on_page_writeback(page);
			truncate_partial_hugepage(page, partial);
			unlock_page(page);
			huge_page_release(page);
		}
	}

	next = start;

	while (1) {
		if (!pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
			if (next == start)
				break;
			next = start;
			continue;
		}

		for (i = 0; i < pagevec_count(&pvec); ++i) {
			struct page *page = pvec.pages[i];

			lock_page(page);
			wait_on_page_writeback(page);
			if (page->index > next)
				next = page->index;
			++next;
			truncate_huge_page(mapping, page);
			unlock_page(page);
		}
		huge_pagevec_release(&pvec);
	}
	BUG_ON(!lstart && mapping->nrpages);
}

static void hugetlbfs_delete_inode(struct inode *inode)
{
	list_del_init(&inode->i_hash);
	list_del_init(&inode->i_list);
	inode->i_state |= I_FREEING;
	inodes_stat.nr_inodes--;
	spin_unlock(&inode_lock);

	if (inode->i_data.nrpages)
		truncate_hugepages(&inode->i_data, 0);

	security_inode_delete(inode);

	clear_inode(inode);
	destroy_inode(inode);
}

static void hugetlbfs_forget_inode(struct inode *inode)
{
	struct super_block *super_block = inode->i_sb;

	if (list_empty(&inode->i_hash))
		goto out_truncate;

	if (!(inode->i_state & (I_DIRTY|I_LOCK))) {
		list_del(&inode->i_list);
		list_add(&inode->i_list, &inode_unused);
	}
	inodes_stat.nr_unused++;
	if (!super_block | (super_block->s_flags & MS_ACTIVE)) {
		spin_unlock(&inode_lock);
		return;
	}

	/* write_inode_now() ? */
	inodes_stat.nr_unused--;
	list_del_init(&inode->i_hash);
out_truncate:
	list_del_init(&inode->i_list);
	inode->i_state |= I_FREEING;
	inodes_stat.nr_inodes--;
	spin_unlock(&inode_lock);
	if (inode->i_data.nrpages)
		truncate_hugepages(&inode->i_data, 0);
	clear_inode(inode);
	destroy_inode(inode);
}

static void hugetlbfs_drop_inode(struct inode *inode)
{
	if (!inode->i_nlink)
		hugetlbfs_delete_inode(inode);
	else
		hugetlbfs_forget_inode(inode);
}

static void hugetlb_vmtruncate_list(struct list_head *list, unsigned long pgoff)
{
	unsigned long start, end, length, delta;
	struct vm_area_struct *vma;

	list_for_each_entry(vma, list, shared) {
		start = vma->vm_start;
		end = vma->vm_end;
		length = end - start;

		if (vma->vm_pgoff >= pgoff) {
			zap_hugepage_range(vma, start, length);
			continue;
		}

		length >>= PAGE_SHIFT;
		delta = pgoff = vma->vm_pgoff;
		if (delta >= length)
			continue;

		start += delta << PAGE_SHIFT;
		length = (length - delta) << PAGE_SHIFT;
		zap_hugepage_range(vma, start, length);
	}
}

static int hugetlb_vmtruncate(struct inode *inode, loff_t offset)
{
	unsigned long pgoff;
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	pgoff = (offset + HPAGE_SIZE - 1) >> HPAGE_SHIFT;

	if (inode->i_size < offset)
		goto do_expand;

	inode->i_size = offset;
	down(&mapping->i_shared_sem);
	if (list_empty(&mapping->i_mmap) && list_empty(&mapping->i_mmap_shared))
		goto out_unlock;
	if (!list_empty(&mapping->i_mmap))
		hugetlb_vmtruncate_list(&mapping->i_mmap, pgoff);
	if (!list_empty(&mapping->i_mmap_shared))
		hugetlb_vmtruncate_list(&mapping->i_mmap_shared, pgoff);

out_unlock:
	up(&mapping->i_shared_sem);
	truncate_hugepages(mapping, offset);
	return 0;

do_expand:
	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY && offset > limit)
		goto out_sig;
	if (offset > inode->i_sb->s_maxbytes)
		goto out;
	inode->i_size = offset;
	return 0;

out_sig:
	send_sig(SIGXFSZ, current, 0);
out:
	return -EFBIG;
}

static int hugetlbfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;
	unsigned int ia_valid = attr->ia_valid;
	unsigned long dn_mask;

	BUG_ON(!inode);

	error = inode_change_ok(inode, attr);
	if (error)
		goto out;

	error = security_inode_setattr(dentry, attr);
	if (error)
		goto out;

	if ((ia_valid & ATTR_UID && attr->ia_uid != inode->i_uid) ||
	    (ia_valid & ATTR_GID && attr->ia_gid != inode->i_gid))
		error = DQUOT_TRANSFER(inode, attr) ? -EDQUOT : 0;
	if (error)
		goto out;

	if (ia_valid & ATTR_SIZE) {
		error = hugetlb_vmtruncate(inode, attr->ia_size);
		if (error)
			goto out;
		attr->ia_valid &= ~ATTR_SIZE;
		error = inode_setattr(inode, attr);
	}
	if (error)
		goto out;
	dn_mask = setattr_mask(ia_valid);
	if (dn_mask)
		dnotify_parent(dentry, dn_mask);
out:
	return error;
}

static struct inode *
hugetlbfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_mapping->a_ops = &hugetlbfs_aops;
		inode->i_mapping->backing_dev_info = &hugetlbfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_fop = &hugetlbfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &hugetlbfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inode->i_nlink++;
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
hugetlbfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode * inode = hugetlbfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);		/* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int hugetlbfs_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	int retval = hugetlbfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		dir->i_nlink++;
	return retval;
}

static int hugetlbfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return hugetlbfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int hugetlbfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = hugetlbfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	return error;
}

/*
 * For direct-IO reads into hugetlb pages
 */
int hugetlbfs_set_page_dirty(struct page *page)
{
	return 0;
}

static struct address_space_operations hugetlbfs_aops = {
	.readpage	= hugetlbfs_readpage,
	.prepare_write	= hugetlbfs_prepare_write,
	.commit_write	= hugetlbfs_commit_write,
	.set_page_dirty	= hugetlbfs_set_page_dirty,
};

struct file_operations hugetlbfs_file_operations = {
	.mmap		= hugetlbfs_file_mmap,
	.fsync		= simple_sync_file,
};

static struct inode_operations hugetlbfs_dir_inode_operations = {
	.create		= hugetlbfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= hugetlbfs_symlink,
	.mkdir		= hugetlbfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= hugetlbfs_mknod,
	.rename		= simple_rename,
	.setattr	= hugetlbfs_setattr,
};

static struct super_operations hugetlbfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= hugetlbfs_drop_inode,
};

static int hugetlbfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = HUGETLBFS_MAGIC;
	sb->s_op = &hugetlbfs_ops;
	inode = hugetlbfs_get_inode(sb, S_IFDIR | 0755, 0);
	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

static struct super_block *hugetlbfs_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_nodev(fs_type, flags, data, hugetlbfs_fill_super);
}

static struct file_system_type hugetlbfs_fs_type = {
	.name		= "hugetlbfs",
	.get_sb		= hugetlbfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static struct vfsmount *hugetlbfs_vfsmount;

static atomic_t hugetlbfs_counter = ATOMIC_INIT(0);

struct file *hugetlb_zero_setup(size_t size)
{
	int error, n;
	struct file *file;
	struct inode *inode;
	struct dentry *dentry, *root;
	struct qstr quick_string;
	char buf[16];

	if (!capable(CAP_IPC_LOCK))
		return ERR_PTR(-EPERM);

	if (!is_hugepage_mem_enough(size))
		return ERR_PTR(-ENOMEM);
	n = atomic_read(&hugetlbfs_counter);
	atomic_inc(&hugetlbfs_counter);

	root = hugetlbfs_vfsmount->mnt_root;
	snprintf(buf, 16, "%d", n);
	quick_string.name = buf;
	quick_string.len = strlen(quick_string.name);
	quick_string.hash = 0;
	dentry = d_alloc(root, &quick_string);
	if (!dentry)
		return ERR_PTR(-ENOMEM);

	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto out_dentry;

	error = -ENOSPC;
	inode = hugetlbfs_get_inode(root->d_sb, S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto out_file;

	d_instantiate(dentry, inode);
	inode->i_size = size;
	inode->i_nlink = 0;
	file->f_vfsmnt = mntget(hugetlbfs_vfsmount);
	file->f_dentry = dentry;
	file->f_op = &hugetlbfs_file_operations;
	file->f_mode = FMODE_WRITE | FMODE_READ;
	return file;

out_file:
	put_filp(file);
out_dentry:
	dput(dentry);
	return ERR_PTR(error);
}

static int __init init_hugetlbfs_fs(void)
{
	int error;
	struct vfsmount *vfsmount;

	error = register_filesystem(&hugetlbfs_fs_type);
	if (error)
		return error;

	vfsmount = kern_mount(&hugetlbfs_fs_type);

	if (!IS_ERR(vfsmount)) {
		hugetlbfs_vfsmount = vfsmount;
		return 0;
	}

	error = PTR_ERR(vfsmount);
	return error;
}

static void __exit exit_hugetlbfs_fs(void)
{
	unregister_filesystem(&hugetlbfs_fs_type);
}

module_init(init_hugetlbfs_fs)
module_exit(exit_hugetlbfs_fs)

MODULE_LICENSE("GPL");

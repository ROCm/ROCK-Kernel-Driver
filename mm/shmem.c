/*
 * Resizable simple shmem filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *		 2000 Transmeta Corp.
 *		 2000 Christoph Rohland
 * 
 * This file is released under the GPL.
 */

/*
 * This shared memory handling is heavily based on the ramfs. It
 * extends the ramfs by the ability to use swap which would makes it a
 * completely usable filesystem.
 *
 * But read and write are not supported (yet)
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <asm/smplock.h>

#include <asm/uaccess.h>

#define SHMEM_MAGIC	0x01021994

#define ENTRIES_PER_PAGE (PAGE_SIZE/sizeof(unsigned long))
#define NR_SINGLE (ENTRIES_PER_PAGE + SHMEM_NR_DIRECT)

static struct super_operations shmem_ops;
static struct address_space_operations shmem_aops;
static struct file_operations shmem_file_operations;
static struct inode_operations shmem_inode_operations;
static struct file_operations shmem_dir_operations;
static struct inode_operations shmem_dir_inode_operations;
static struct vm_operations_struct shmem_shared_vm_ops;
static struct vm_operations_struct shmem_private_vm_ops;

LIST_HEAD (shmem_inodes);
static spinlock_t shmem_ilock = SPIN_LOCK_UNLOCKED;

static swp_entry_t * shmem_swp_entry (struct shmem_inode_info *info, unsigned long index) 
{
	if (index < SHMEM_NR_DIRECT)
		return info->i_direct+index;

	index -= SHMEM_NR_DIRECT;
	if (index >= ENTRIES_PER_PAGE*ENTRIES_PER_PAGE)
		return NULL;

	if (!info->i_indirect) {
		info->i_indirect = (swp_entry_t **) get_zeroed_page(GFP_USER);
		if (!info->i_indirect)
			return NULL;
	}
	if(!(info->i_indirect[index/ENTRIES_PER_PAGE])) {
		info->i_indirect[index/ENTRIES_PER_PAGE] = (swp_entry_t *) get_zeroed_page(GFP_USER);
		if (!info->i_indirect[index/ENTRIES_PER_PAGE])
			return NULL;
	}
	
	return info->i_indirect[index/ENTRIES_PER_PAGE]+index%ENTRIES_PER_PAGE;
}

static int shmem_free_swp(swp_entry_t *dir, unsigned int count)
{
	swp_entry_t *ptr, entry;
	struct page * page;
	int freed = 0;

	for (ptr = dir; ptr < dir + count; ptr++) {
		if (!ptr->val)
			continue;
		entry = *ptr;
		swap_free (entry);
		*ptr = (swp_entry_t){0};
		freed++;
		if (!(page = lookup_swap_cache(entry)))
			continue;
		delete_from_swap_cache(page);
		page_cache_release(page);
	}
	return freed;
}

/*
 * shmem_truncate_part - free a bunch of swap entries
 *
 * @dir:	pointer to swp_entries 
 * @size:	number of entries in dir
 * @start:	offset to start from
 * @inode:	inode for statistics
 * @freed:	counter for freed pages
 *
 * It frees the swap entries from dir+start til dir+size
 *
 * returns 0 if it truncated something, else (offset-size)
 */

static unsigned long 
shmem_truncate_part (swp_entry_t * dir, unsigned long size, 
		     unsigned long start, struct inode * inode, unsigned long *freed) {
	if (start > size)
		return start - size;
	if (dir)
		*freed += shmem_free_swp (dir+start, size-start);
	
	return 0;
}

static void shmem_truncate (struct inode * inode)
{
	int clear_base;
	unsigned long start;
	unsigned long mmfreed, freed = 0;
	swp_entry_t **base, **ptr;
	struct shmem_inode_info * info = &inode->u.shmem_i;

	spin_lock (&info->lock);
	start = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	start = shmem_truncate_part (info->i_direct, SHMEM_NR_DIRECT, start, inode, &freed);

	if (!(base = info->i_indirect))
		goto out;;

	clear_base = 1;
	for (ptr = base; ptr < base + ENTRIES_PER_PAGE; ptr++) {
		if (!start) {
			if (!*ptr)
				continue;
			freed += shmem_free_swp (*ptr, ENTRIES_PER_PAGE);
			free_page ((unsigned long) *ptr);
			*ptr = 0;
			continue;
		}
		clear_base = 0;
		start = shmem_truncate_part (*ptr, ENTRIES_PER_PAGE, start, inode, &freed);
	}

	if (!clear_base) 
		goto out;

	free_page ((unsigned long)base);
	info->i_indirect = 0;

out:

	/*
	 * We have to calculate the free blocks since we do not know
	 * how many pages the mm discarded
	 *
	 * But we know that normally
	 * inodes->i_blocks == inode->i_mapping->nrpages + info->swapped
	 *
	 * So the mm freed 
	 * inodes->i_blocks - (inode->i_mapping->nrpages + info->swapped)
	 */

	mmfreed = inode->i_blocks - (inode->i_mapping->nrpages + info->swapped);
	info->swapped -= freed;
	inode->i_blocks -= freed + mmfreed;
	spin_unlock (&info->lock);

	spin_lock (&inode->i_sb->u.shmem_sb.stat_lock);
	inode->i_sb->u.shmem_sb.free_blocks += freed + mmfreed;
	spin_unlock (&inode->i_sb->u.shmem_sb.stat_lock);
}

static void shmem_delete_inode(struct inode * inode)
{
	struct shmem_sb_info *info = &inode->i_sb->u.shmem_sb;

	spin_lock (&shmem_ilock);
	list_del (&inode->u.shmem_i.list);
	spin_unlock (&shmem_ilock);
	inode->i_size = 0;
	shmem_truncate (inode);
	spin_lock (&info->stat_lock);
	info->free_inodes++;
	spin_unlock (&info->stat_lock);
	clear_inode(inode);
}

/*
 * Move the page from the page cache to the swap cache
 */
static int shmem_writepage(struct page * page)
{
	int error;
	struct shmem_inode_info *info;
	swp_entry_t *entry, swap;

	info = &page->mapping->host->u.shmem_i;
	if (info->locked)
		return 1;
	swap = __get_swap_page(2);
	if (!swap.val)
		return 1;

	spin_lock(&info->lock);
	entry = shmem_swp_entry (info, page->index);
	if (!entry)	/* this had been allocted on page allocation */
		BUG();
	error = -EAGAIN;
	if (entry->val) {
                __swap_free(swap, 2);
		goto out;
        }

        *entry = swap;
	error = 0;
	/* Remove the from the page cache */
	lru_cache_del(page);
	remove_inode_page(page);

	/* Add it to the swap cache */
	add_to_swap_cache(page, swap);
	page_cache_release(page);
	set_page_dirty(page);
	info->swapped++;
out:
	spin_unlock(&info->lock);
	UnlockPage(page);
	return error;
}

/*
 * shmem_nopage - either get the page from swap or allocate a new one
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache
 */
struct page * shmem_nopage(struct vm_area_struct * vma, unsigned long address, int no_share)
{
	unsigned long size;
	struct page * page;
	unsigned int idx;
	swp_entry_t *entry;
	struct inode * inode = vma->vm_file->f_dentry->d_inode;
	struct address_space * mapping = inode->i_mapping;
	struct shmem_inode_info *info;

	idx = (address - vma->vm_start) >> PAGE_SHIFT;
	idx += vma->vm_pgoff;

	down (&inode->i_sem);
	size = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	page = NOPAGE_SIGBUS;
	if ((idx >= size) && (vma->vm_mm == current->mm))
		goto out;

	/* retry, we may have slept */
	page = __find_lock_page(mapping, idx, page_hash (mapping, idx));
	if (page)
		goto cached_page;

	info = &inode->u.shmem_i;
	entry = shmem_swp_entry (info, idx);
	if (!entry)
		goto oom;
	if (entry->val) {
		unsigned long flags;

		/* Look it up and read it in.. */
		page = lookup_swap_cache(*entry);
		if (!page) {
			lock_kernel();
			swapin_readahead(*entry);
			page = read_swap_cache(*entry);
			unlock_kernel();
			if (!page) 
				goto oom;
		}

		/* We have to this with page locked to prevent races */
		spin_lock (&info->lock);
		swap_free(*entry);
		lock_page(page);
		delete_from_swap_cache_nolock(page);
		*entry = (swp_entry_t) {0};
		flags = page->flags & ~((1 << PG_uptodate) | (1 << PG_error) | (1 << PG_referenced) | (1 << PG_arch_1));
		page->flags = flags | (1 << PG_dirty);
		add_to_page_cache_locked(page, mapping, idx);
		info->swapped--;
		spin_unlock (&info->lock);
	} else {
		spin_lock (&inode->i_sb->u.shmem_sb.stat_lock);
		if (inode->i_sb->u.shmem_sb.free_blocks == 0)
			goto no_space;
		inode->i_sb->u.shmem_sb.free_blocks--;
		spin_unlock (&inode->i_sb->u.shmem_sb.stat_lock);
		/* Ok, get a new page */
		page = page_cache_alloc();
		if (!page)
			goto oom;
		clear_user_highpage(page, address);
		inode->i_blocks++;
		add_to_page_cache (page, mapping, idx);
	}
	/* We have the page */
	SetPageUptodate (page);

cached_page:
	UnlockPage (page);
	up(&inode->i_sem);

	if (no_share) {
		struct page *new_page = page_cache_alloc();

		if (new_page) {
			copy_user_highpage(new_page, page, address);
			flush_page_to_ram(new_page);
		} else
			new_page = NOPAGE_OOM;
		page_cache_release(page);
		return new_page;
	}

	flush_page_to_ram (page);
	return(page);
no_space:
	spin_unlock (&inode->i_sb->u.shmem_sb.stat_lock);
oom:
	page = NOPAGE_OOM;
out:
	up(&inode->i_sem);
	return page;
}

struct inode *shmem_get_inode(struct super_block *sb, int mode, int dev)
{
	struct inode * inode;

	spin_lock (&sb->u.shmem_sb.stat_lock);
	if (!sb->u.shmem_sb.free_inodes) {
		spin_unlock (&sb->u.shmem_sb.stat_lock);
		return NULL;
	}
	sb->u.shmem_sb.free_inodes--;
	spin_unlock (&sb->u.shmem_sb.stat_lock);

	inode = new_inode(sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = to_kdev_t(dev);
		inode->i_mapping->a_ops = &shmem_aops;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		spin_lock_init (&inode->u.shmem_i.lock);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &shmem_inode_operations;
			inode->i_fop = &shmem_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &shmem_dir_inode_operations;
			inode->i_fop = &shmem_dir_operations;
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
		spin_lock (&shmem_ilock);
		list_add (&inode->u.shmem_i.list, &shmem_inodes);
		spin_unlock (&shmem_ilock);
	}
	return inode;
}

static int shmem_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = SHMEM_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	spin_lock (&sb->u.shmem_sb.stat_lock);
	if (sb->u.shmem_sb.max_blocks != ULONG_MAX || 
	    sb->u.shmem_sb.max_inodes != ULONG_MAX) {
		buf->f_blocks = sb->u.shmem_sb.max_blocks;
		buf->f_bavail = buf->f_bfree = sb->u.shmem_sb.free_blocks;
		buf->f_files = sb->u.shmem_sb.max_inodes;
		buf->f_ffree = sb->u.shmem_sb.free_inodes;
	}
	spin_unlock (&sb->u.shmem_sb.stat_lock);
	buf->f_namelen = 255;
	return 0;
}

/*
 * Lookup the data. This is trivial - if the dentry didn't already
 * exist, we know it is negative.
 */
static struct dentry * shmem_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int shmem_mknod(struct inode *dir, struct dentry *dentry, int mode, int dev)
{
	struct inode * inode = shmem_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int shmem_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	return shmem_mknod(dir, dentry, mode | S_IFDIR, 0);
}

static int shmem_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return shmem_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Link a file..
 */
static int shmem_link(struct dentry *old_dentry, struct inode * dir, struct dentry * dentry)
{
	struct inode *inode = old_dentry->d_inode;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	inode->i_nlink++;
	atomic_inc(&inode->i_count);	/* New dentry reference */
	dget(dentry);		/* Extra pinning count for the created dentry */
	d_instantiate(dentry, inode);
	return 0;
}

static inline int shmem_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

/*
 * Check that a directory is empty (this works
 * for regular files too, they'll just always be
 * considered empty..).
 *
 * Note that an empty directory can still have
 * children, they just all have to be negative..
 */
static int shmem_empty(struct dentry *dentry)
{
	struct list_head *list;

	spin_lock(&dcache_lock);
	list = dentry->d_subdirs.next;

	while (list != &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);

		if (shmem_positive(de)) {
			spin_unlock(&dcache_lock);
			return 0;
		}
		list = list->next;
	}
	spin_unlock(&dcache_lock);
	return 1;
}

/*
 * This works for both directories and regular files.
 * (non-directories will always have empty subdirs)
 */
static int shmem_unlink(struct inode * dir, struct dentry *dentry)
{
	int retval = -ENOTEMPTY;

	if (shmem_empty(dentry)) {
		struct inode *inode = dentry->d_inode;

		inode->i_nlink--;
		dput(dentry);	/* Undo the count from "create" - this does all the work */
		retval = 0;
	}
	return retval;
}

#define shmem_rmdir shmem_unlink

/*
 * The VFS layer already does all the dentry stuff for rename,
 * we just have to decrement the usage count for the target if
 * it exists so that the VFS layer correctly free's it when it
 * gets overwritten.
 */
static int shmem_rename(struct inode * old_dir, struct dentry *old_dentry, struct inode * new_dir,struct dentry *new_dentry)
{
	int error = -ENOTEMPTY;

	if (shmem_empty(new_dentry)) {
		struct inode *inode = new_dentry->d_inode;
		if (inode) {
			inode->i_nlink--;
			dput(new_dentry);
		}
		error = 0;
	}
	return error;
}

static int shmem_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	int error;

	error = shmem_mknod(dir, dentry, S_IFLNK | S_IRWXUGO, 0);
	if (!error) {
		int l = strlen(symname)+1;
		struct inode *inode = dentry->d_inode;
		error = block_symlink(inode, symname, l);
	}
	return error;
}

static int shmem_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct vm_operations_struct * ops;
	struct inode *inode = file->f_dentry->d_inode;

	ops = &shmem_private_vm_ops;
	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE))
		ops = &shmem_shared_vm_ops;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	UPDATE_ATIME(inode);
	vma->vm_ops = ops;
	return 0;
}

static int shmem_parse_options(char *options, int *mode, unsigned long * blocks, unsigned long *inodes)
{
	char *this_char, *value;

	this_char = NULL;
	if ( options )
		this_char = strtok(options,",");
	for ( ; this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"nr_blocks")) {
			if (!value || !*value || !blocks)
				return 1;
			*blocks = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		} else if (!strcmp(this_char,"nr_inodes")) {
			if (!value || !*value || !inodes)
				return 1;
			*inodes = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		} else if (!strcmp(this_char,"mode")) {
			if (!value || !*value || !mode)
				return 1;
			*mode = simple_strtoul(value,&value,8);
			if (*value)
				return 1;
		}
		else
			return 1;
	}

	return 0;
}

static struct super_block *shmem_read_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	unsigned long blocks = ULONG_MAX;	/* unlimited */
	unsigned long inodes = ULONG_MAX;	/* unlimited */
	int mode   = S_IRWXUGO | S_ISVTX;

	if (shmem_parse_options (data, &mode, &blocks, &inodes)) {
		printk(KERN_ERR "shmem fs invalid option\n");
		return NULL;
	}

	spin_lock_init (&sb->u.shmem_sb.stat_lock);
	sb->u.shmem_sb.max_blocks = blocks;
	sb->u.shmem_sb.free_blocks = blocks;
	sb->u.shmem_sb.max_inodes = inodes;
	sb->u.shmem_sb.free_inodes = inodes;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SHMEM_MAGIC;
	sb->s_op = &shmem_ops;
	inode = shmem_get_inode(sb, S_IFDIR | mode, 0);
	if (!inode)
		return NULL;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return NULL;
	}
	sb->s_root = root;
	return sb;
}

static int shmem_remount_fs (struct super_block *sb, int *flags, char *data)
{
	int error;
	unsigned long max_blocks, blocks;
	unsigned long max_inodes, inodes;
	struct shmem_sb_info *info = &sb->u.shmem_sb;

	if (shmem_parse_options (data, NULL, &max_blocks, &max_inodes))
		return -EINVAL;

	spin_lock(&info->stat_lock);
	blocks = info->max_blocks - info->free_blocks;
	inodes = info->max_inodes - info->free_inodes;
	error = -EINVAL;
	if (max_blocks < blocks)
		goto out;
	if (max_inodes < inodes)
		goto out;
	error = 0;
	info->max_blocks  = max_blocks;
	info->free_blocks = max_blocks - blocks;
	info->max_inodes  = max_inodes;
	info->free_inodes = max_inodes - inodes;
out:
	spin_unlock(&info->stat_lock);
	return error;
}

static struct address_space_operations shmem_aops = {
	writepage: shmem_writepage
};

static struct file_operations shmem_file_operations = {
	mmap:		shmem_mmap
};

static struct inode_operations shmem_inode_operations = {
	truncate:	shmem_truncate,
};

static struct file_operations shmem_dir_operations = {
	read:		generic_read_dir,
	readdir:	dcache_readdir,
};

static struct inode_operations shmem_dir_inode_operations = {
	create:		shmem_create,
	lookup:		shmem_lookup,
	link:		shmem_link,
	unlink:		shmem_unlink,
	symlink:	shmem_symlink,
	mkdir:		shmem_mkdir,
	rmdir:		shmem_rmdir,
	mknod:		shmem_mknod,
	rename:		shmem_rename,
};

static struct super_operations shmem_ops = {
	statfs:		shmem_statfs,
	remount_fs:	shmem_remount_fs,
	delete_inode:	shmem_delete_inode,
	put_inode:	force_delete,	
};

static struct vm_operations_struct shmem_private_vm_ops = {
	nopage:	shmem_nopage,
};

static struct vm_operations_struct shmem_shared_vm_ops = {
	nopage:	shmem_nopage,
};

static DECLARE_FSTYPE(shmem_fs_type, "shm", shmem_read_super, FS_LITTER);

static int __init init_shmem_fs(void)
{
	int error;
	struct vfsmount * res;

	if ((error = register_filesystem(&shmem_fs_type))) {
		printk (KERN_ERR "Could not register shmem fs\n");
		return error;
	}

	res = kern_mount(&shmem_fs_type);
	if (IS_ERR (res)) {
		printk (KERN_ERR "could not kern_mount shmem fs\n");
		unregister_filesystem(&shmem_fs_type);
		return PTR_ERR(res);
	}

	devfs_mk_dir (NULL, "shm", NULL);
	return 0;
}

static void __exit exit_shmem_fs(void)
{
	unregister_filesystem(&shmem_fs_type);
}

module_init(init_shmem_fs)
module_exit(exit_shmem_fs)

static int shmem_clear_swp (swp_entry_t entry, swp_entry_t *ptr, int size) {
	swp_entry_t *test;

	for (test = ptr; test < ptr + size; test++) {
		if (test->val == entry.val) {
			swap_free (entry);
			*test = (swp_entry_t) {0};
			return test - ptr;
		}
	}
	return -1;
}

static int shmem_unuse_inode (struct inode *inode, swp_entry_t entry, struct page *page)
{
	swp_entry_t **base, **ptr;
	unsigned long idx;
	int offset;
	struct shmem_inode_info *info = &inode->u.shmem_i;
	
	idx = 0;
	spin_lock (&info->lock);
	if ((offset = shmem_clear_swp (entry,info->i_direct, SHMEM_NR_DIRECT)) >= 0)
		goto found;

	idx = SHMEM_NR_DIRECT;
	if (!(base = info->i_indirect))
		goto out;

	for (ptr = base; ptr < base + ENTRIES_PER_PAGE; ptr++) {
		if (*ptr &&
		    (offset = shmem_clear_swp (entry, *ptr, ENTRIES_PER_PAGE)) >= 0)
			goto found;
		idx += ENTRIES_PER_PAGE;
	}
out:
	spin_unlock (&info->lock);
	return 0;
found:
	add_to_page_cache(page, inode->i_mapping, offset + idx);
	set_page_dirty(page);
	SetPageUptodate(page);
	UnlockPage(page);
	info->swapped--;
	spin_unlock(&info->lock);
	return 1;
}

/*
 * unuse_shmem() search for an eventually swapped out shmem page.
 */
void shmem_unuse(swp_entry_t entry, struct page *page)
{
	struct list_head *p;
	struct inode * inode;

	spin_lock (&shmem_ilock);
	list_for_each(p, &shmem_inodes) {
		inode = list_entry(p, struct inode, u.shmem_i.list);

		if (shmem_unuse_inode(inode, entry, page))
			break;
	}
	spin_unlock (&shmem_ilock);
}


/*
 * shmem_file_setup - get an unlinked file living in shmem fs
 *
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 *
 */
struct file *shmem_file_setup(char * name, loff_t size)
{
	int error;
	struct file *file;
	struct inode * inode;
	struct dentry *dentry, *root;
	struct qstr this;
	int vm_enough_memory(long pages);

	error = -ENOMEM;
	if (!vm_enough_memory((size) >> PAGE_SHIFT))
		goto out;

	this.name = name;
	this.len = strlen(name);
	this.hash = 0; /* will go */
	root = shmem_fs_type.kern_mnt->mnt_root;
	dentry = d_alloc(root, &this);
	if (!dentry)
		goto out;

	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto put_dentry;

	error = -ENOSPC;
	inode = shmem_get_inode(root->d_sb, S_IFREG | S_IRWXUGO, 0);
	if (!inode) 
		goto close_file;

	d_instantiate(dentry, inode);
	dentry->d_inode->i_size = size;
	file->f_vfsmnt = mntget(shmem_fs_type.kern_mnt);
	file->f_dentry = dentry;
	file->f_op = &shmem_file_operations;
	file->f_mode = FMODE_WRITE | FMODE_READ;
	inode->i_nlink = 0;	/* It is unlinked */
	return(file);

close_file:
	put_filp(file);
put_dentry:
	dput (dentry);
out:
	return ERR_PTR(error);	
}
/*
 * shmem_zero_setup - setup a shared anonymous mapping
 *
 * @vma: the vma to be mmapped is prepared by do_mmap_pgoff
 */
int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;
	
	file = shmem_file_setup("dev/zero", size);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput (vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &shmem_shared_vm_ops;
	return 0;
}

/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * This file contains functions used to control access to the Linux file
 * system.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/autoconf.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <asm/statfs.h>
#include <asm/uaccess.h>
#include <linux/ctype.h>
#include <linux/statfs.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/mm.h>
#include <linux/file.h>

/*===[ Include files specific to this module ]============================*/
#include "vfs.h"


struct inode_data {
	void *Scope;
	unsigned long Flags;
	struct list_head IList;
	struct inode *Inode;
	unsigned long cntDC;
	struct list_head DirCache;
	struct semaphore DirCacheLock;
	void * FileHandle;
	int CacheFlag;
	char Name[1];		/* Needs to be last entry */
};

#define FILE_UPDATE_TIMEOUT   2

/*===[ Function prototypes ]=============================================*/

static unsigned long novfs_internal_hash(struct qstr *name);
static int novfs_d_add(struct dentry *p, struct dentry *d, struct inode *i, int add);

static int novfs_get_sb(struct file_system_type *Fstype, int Flags,
		 const char *Dev_name, void *Data, struct vfsmount *Mnt);

static void novfs_kill_sb(struct super_block *SB);


/*
 * Declared dentry_operations
 */
int novfs_d_revalidate(struct dentry *, struct nameidata *);
int novfs_d_hash(struct dentry *, struct qstr *);
int novfs_d_compare(struct dentry *, struct qstr *, struct qstr *);
int novfs_d_delete(struct dentry *dentry);
void novfs_d_release(struct dentry *dentry);
void novfs_d_iput(struct dentry *dentry, struct inode *inode);

/*
 * Declared directory operations
 */
int novfs_dir_open(struct inode *inode, struct file *file);
int novfs_dir_release(struct inode *inode, struct file *file);
loff_t novfs_dir_lseek(struct file *file, loff_t offset, int origin);
ssize_t novfs_dir_read(struct file *file, char *buf, size_t len, loff_t * off);
void addtodentry(struct dentry *Parent, unsigned char *List, int Level);
int novfs_filldir(void *data, const char *name, int namelen, loff_t off,
		  ino_t ino, unsigned ftype);
int novfs_dir_readdir(struct file *filp, void *dirent, filldir_t filldir);
int novfs_dir_fsync(struct file *file, struct dentry *dentry, int datasync);

/*
 * Declared address space operations
 */
int novfs_a_writepage(struct page *page, struct writeback_control *wbc);
int novfs_a_writepages(struct address_space *mapping,
		       struct writeback_control *wbc);
int novfs_a_prepare_write(struct file *file, struct page *page, unsigned from,
			  unsigned to);
int novfs_a_commit_write(struct file *file, struct page *page, unsigned offset,
			 unsigned to);
int novfs_a_readpage(struct file *file, struct page *page);
int novfs_a_readpages(struct file *file, struct address_space *mapping,
		      struct list_head *page_lst, unsigned nr_pages);
ssize_t novfs_a_direct_IO(int rw, struct kiocb *kiocb, const struct iovec *iov,
			  loff_t offset, unsigned long nr_segs);

/*
 * Declared file_operations
 */
ssize_t novfs_f_read(struct file *, char *, size_t, loff_t *);
ssize_t novfs_f_write(struct file *, const char *, size_t, loff_t *);
int novfs_f_readdir(struct file *, void *, filldir_t);
int novfs_f_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
int novfs_f_mmap(struct file *file, struct vm_area_struct *vma);
int novfs_f_open(struct inode *, struct file *);
int novfs_f_flush(struct file *, fl_owner_t);
int novfs_f_release(struct inode *, struct file *);
int novfs_f_fsync(struct file *, struct dentry *, int datasync);
int novfs_f_lock(struct file *, int, struct file_lock *);

/*
 * Declared inode_operations
 */
int novfs_i_create(struct inode *, struct dentry *, int, struct nameidata *);
struct dentry *novfs_i_lookup(struct inode *, struct dentry *,
			      struct nameidata *);
int novfs_i_mkdir(struct inode *, struct dentry *, int);
int novfs_i_unlink(struct inode *dir, struct dentry *dentry);
int novfs_i_rmdir(struct inode *, struct dentry *);
int novfs_i_mknod(struct inode *, struct dentry *, int, dev_t);
int novfs_i_rename(struct inode *, struct dentry *, struct inode *,
		   struct dentry *);
int novfs_i_setattr(struct dentry *, struct iattr *);
int novfs_i_getattr(struct vfsmount *mnt, struct dentry *, struct kstat *);
int novfs_i_revalidate(struct dentry *dentry);

/*
 * Extended attributes operations
 */

int novfs_i_getxattr(struct dentry *dentry, const char *name, void *buffer,
		     size_t size);
int novfs_i_setxattr(struct dentry *dentry, const char *name, const void *value,
		     size_t value_size, int flags);
int novfs_i_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size);

void update_inode(struct inode *Inode, struct novfs_entry_info *Info);

/*
 * Declared super_operations
 */
void novfs_read_inode(struct inode *inode);
void novfs_write_inode(struct inode *inode);
int novfs_notify_change(struct dentry *dentry, struct iattr *attr);
void novfs_clear_inode(struct inode *inode);
int novfs_show_options(struct seq_file *s, struct vfsmount *m);

int novfs_statfs(struct dentry *de, struct kstatfs *buf);

/*
 * Declared control interface functions
 */
ssize_t
novfs_control_Read(struct file *file, char *buf, size_t nbytes, loff_t * ppos);

ssize_t
novfs_control_write(struct file *file, const char *buf, size_t nbytes,
		    loff_t * ppos);

int novfs_control_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg);

int __init init_novfs(void);
void __exit exit_novfs(void);

int novfs_lock_inode_cache(struct inode *i);
void novfs_unlock_inode_cache(struct inode *i);
int novfs_enumerate_inode_cache(struct inode *i, struct list_head **iteration,
				ino_t * ino, struct novfs_entry_info *info);
int novfs_get_entry(struct inode *i, struct qstr *name, ino_t * ino,
		    struct novfs_entry_info *info);
int novfs_get_entry_by_pos(struct inode *i, loff_t pos, ino_t * ino,
			   struct novfs_entry_info *info);
int novfs_get_entry_time(struct inode *i, struct qstr *name, ino_t * ino,
			 struct novfs_entry_info *info, u64 * EntryTime);
int novfs_get_remove_entry(struct inode *i, ino_t * ino, struct novfs_entry_info *info);
void novfs_invalidate_inode_cache(struct inode *i);
struct novfs_dir_cache *novfs_lookup_inode_cache(struct inode *i, struct qstr *name,
				    ino_t ino);
int novfs_lookup_validate(struct inode *i, struct qstr *name, ino_t ino);
int novfs_add_inode_entry(struct inode *i, struct qstr *name, ino_t ino,
			  struct novfs_entry_info *info);
int novfs_update_entry(struct inode *i, struct qstr *name, ino_t ino,
		       struct novfs_entry_info *info);
void novfs_remove_inode_entry(struct inode *i, struct qstr *name, ino_t ino);
void novfs_free_invalid_entries(struct inode *i);
void novfs_free_inode_cache(struct inode *i);

/*===[ Global variables ]=================================================*/
struct dentry_operations novfs_dentry_operations = {
	.d_revalidate = novfs_d_revalidate,
	.d_hash = novfs_d_hash,
	.d_compare = novfs_d_compare,
	//.d_delete      = novfs_d_delete,
	.d_release = novfs_d_release,
	.d_iput = novfs_d_iput,
};

struct file_operations novfs_dir_operations = {
	.owner = THIS_MODULE,
	.open = novfs_dir_open,
	.release = novfs_dir_release,
	.llseek = novfs_dir_lseek,
	.read = novfs_dir_read,
	.readdir = novfs_dir_readdir,
	.fsync = novfs_dir_fsync,
};

static struct file_operations novfs_file_operations = {
	.owner = THIS_MODULE,
	.read = novfs_f_read,
	.write = novfs_f_write,
	.readdir = novfs_f_readdir,
	.ioctl = novfs_f_ioctl,
	.mmap = novfs_f_mmap,
	.open = novfs_f_open,
	.flush = novfs_f_flush,
	.release = novfs_f_release,
	.fsync = novfs_f_fsync,
	.llseek = generic_file_llseek,
	.lock = novfs_f_lock,
};

static struct address_space_operations novfs_nocache_aops = {
	.readpage = novfs_a_readpage,
};

struct backing_dev_info novfs_backing_dev_info = {
	.ra_pages = (VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE,
	.state = 0,
	.capabilities = BDI_CAP_NO_WRITEBACK | BDI_CAP_MAP_COPY,
	.unplug_io_fn = default_unplug_io_fn,
};

static struct address_space_operations novfs_aops = {
	.readpage = novfs_a_readpage,
	.readpages = novfs_a_readpages,
	.writepage = novfs_a_writepage,
	.writepages = novfs_a_writepages,
	.prepare_write = novfs_a_prepare_write,
	.commit_write = novfs_a_commit_write,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.direct_IO = novfs_a_direct_IO,
};

static struct inode_operations novfs_inode_operations = {
	.create = novfs_i_create,
	.lookup = novfs_i_lookup,
	.unlink = novfs_i_unlink,
	.mkdir = novfs_i_mkdir,
	.rmdir = novfs_i_rmdir,
	.mknod = novfs_i_mknod,
	.rename = novfs_i_rename,
	.setattr = novfs_i_setattr,
	.getattr = novfs_i_getattr,
/*
	.getxattr = novfs_i_getxattr,
	.setxattr = novfs_i_setxattr,
	.listxattr = novfs_i_listxattr,
*/
};

static struct inode_operations novfs_file_inode_operations = {
	.setattr = novfs_i_setattr,
	.getattr = novfs_i_getattr,
/*
	.getxattr = novfs_i_getxattr,
	.setxattr = novfs_i_setxattr,
	.listxattr = novfs_i_listxattr,
*/
};

static struct super_operations novfs_ops = {
	.statfs = novfs_statfs,
	.clear_inode = novfs_clear_inode,
	.drop_inode = generic_delete_inode,
	.show_options = novfs_show_options,

};

/* Not currently used
static struct file_operations novfs_Control_operations = {
   .read    = novfs_Control_read,
   .write   = novfs_Control_write,
   .ioctl   = novfs_Control_ioctl,
};
*/

static atomic_t novfs_Inode_Number = ATOMIC_INIT(0);


struct dentry *novfs_root = NULL;
char *novfs_current_mnt = NULL;

DECLARE_MUTEX(InodeList_lock);

LIST_HEAD(InodeList);

DECLARE_MUTEX(TimeDir_Lock);
uint64_t lastTime;
char lastDir[PATH_MAX];

uint64_t inHAXTime;
int inHAX;

unsigned long InodeCount = 0, DCCount = 0;
unsigned long novfs_update_timeout = FILE_UPDATE_TIMEOUT;
int novfs_page_cache = 0;

struct file_private {
	int listedall;
	void *enumHandle;
};

static void PRINT_DENTRY(const char *s, struct dentry *d)
{
	DbgPrint("%s: 0x%p\n", s, d);
	DbgPrint("   d_count:      0x%x\n", d->d_count);
	DbgPrint("   d_lock:       0x%x\n", d->d_lock);
	DbgPrint("   d_inode:      0x%x\n", d->d_inode);
	DbgPrint("   d_lru:        0x%p\n"
		 "      next:      0x%p\n"
		 "      prev:      0x%p\n", &d->d_lru, d->d_lru.next,
		 d->d_lru.prev);
	DbgPrint("   d_child:      0x%p\n" "      next:      0x%p\n"
		 "      prev:      0x%p\n", &d->d_u.d_child,
		 d->d_u.d_child.next, d->d_u.d_child.prev);
	DbgPrint("   d_subdirs:    0x%p\n" "      next:      0x%p\n"
		 "      prev:      0x%p\n", &d->d_subdirs, d->d_subdirs.next,
		 d->d_subdirs.prev);
	DbgPrint("   d_alias:      0x%p\n" "      next:      0x%p\n"
		 "      prev:      0x%p\n", &d->d_alias, d->d_alias.next,
		 d->d_alias.prev);
	DbgPrint("   d_time:       0x%x\n", d->d_time);
	DbgPrint("   d_op:         0x%p\n", d->d_op);
	DbgPrint("   d_sb:         0x%p\n", d->d_sb);
	DbgPrint("   d_flags:      0x%x\n", d->d_flags);
	DbgPrint("   d_mounted:    0x%x\n", d->d_mounted);
	DbgPrint("   d_fsdata:     0x%p\n", d->d_fsdata);
/*   DbgPrint("   d_cookie:     0x%x\n", d->d_cookie); */
	DbgPrint("   d_parent:     0x%p\n", d->d_parent);
	DbgPrint("   d_name:       0x%p %.*s\n", &d->d_name, d->d_name.len,
		 d->d_name.name);
	DbgPrint("      name:      0x%p\n" "      len:       %d\n"
		 "      hash:      0x%x\n", d->d_name.name, d->d_name.len,
		 d->d_name.hash);
	DbgPrint("   d_hash:       0x%x\n" "      next:      0x%x\n"
		 "      pprev:     0x%x\n", d->d_hash, d->d_hash.next,
		 d->d_hash.pprev);
}

/*++======================================================================*/
int novfs_remove_from_root(char *RemoveName)
{
	struct qstr name;
	struct dentry *dentry;
	struct inode *dir;

	DbgPrint("novfs_Remove_from_Root: %s\n", RemoveName);
	name.len = strlen(RemoveName);
	name.name = RemoveName;
	novfs_d_hash(novfs_root, &name);

	dentry = d_lookup(novfs_root, &name);
	if (dentry) {
		if (dentry->d_inode && dentry->d_inode->i_private) {
			struct inode_data *n_inode =
				dentry->d_inode->i_private;
			n_inode->Scope = NULL;
		}
		dput(dentry);
	}

	dir = novfs_root->d_inode;

	novfs_lock_inode_cache(dir);
	novfs_remove_inode_entry(dir, &name, 0);
	novfs_unlock_inode_cache(dir);

	return (0);
}

/*++======================================================================*/
int novfs_add_to_root(char *AddName)
{
	struct qstr name;
	struct inode *dir;
	struct novfs_entry_info info;
	ino_t ino;

	DbgPrint("novfs_Add_to_Root: %s\n", AddName);
	name.len = strlen(AddName);
	name.name = AddName;
	novfs_d_hash(novfs_root, &name);

	dir = novfs_root->d_inode;

	novfs_lock_inode_cache(dir);

	ino = 0;

	if (!novfs_lookup_inode_cache(dir, &name, 0)) {
		info.mode = S_IFDIR | 0700;
		info.size = 0;
		info.atime = info.ctime = info.mtime = CURRENT_TIME;

		ino = (ino_t)atomic_inc_return(&novfs_Inode_Number);
		novfs_add_inode_entry(dir, &name, ino, &info);
	}

	novfs_unlock_inode_cache(dir);

	return (0);
}

/*++======================================================================*/
int novfs_Add_to_Root2(char *AddName)
{
	struct dentry *entry;
	struct qstr name;
	struct inode *inode;
	void *scope;

	DbgPrint("novfs_Add_to_Root: %s\n", AddName);
	name.len = strlen(AddName);
	name.name = AddName;

	novfs_d_hash(novfs_root, &name);

	entry = d_lookup(novfs_root, &name);
	DbgPrint("novfs_Add_to_Root: novfs_d_lookup 0x%p\n", entry);
	if (NULL == entry) {
		scope = novfs_scope_lookup();

		entry = d_alloc(novfs_root, &name);
		DbgPrint("novfs_Add_to_Root: d_alloc 0x%p\n", entry);
		if (entry) {
			entry->d_op = &novfs_dentry_operations;
			entry->d_time = jiffies + (novfs_update_timeout * HZ);
			/*
			 * done in novfs_d_add now... entry->d_fsdata = (void *)novfs_internal_hash( &name );
			 */
			inode =
			    novfs_get_inode(novfs_root->d_sb, S_IFDIR | 0700, 0, novfs_scope_get_uid(scope), 0, &name);
			DbgPrint("novfs_Add_to_Root: Inode=0x%p\n", inode);
			if (inode) {
				inode->i_atime =
				    inode->i_ctime =
				    inode->i_mtime = CURRENT_TIME;
				if (!novfs_d_add(novfs_root, entry, inode, 1)) {
					if (inode->i_private) {
						struct inode_data *n_inode = inode->i_private;
						n_inode->Flags = USER_INODE;
					}
					PRINT_DENTRY("After novfs_d_add",
						     entry);
				} else {
					dput(entry);
					iput(inode);
				}
			}
		}
	} else {
		dput(entry);
		PRINT_DENTRY("novfs_Add_to_Root: After dput Dentry", entry);
	}
	return (0);
}

char *novfs_dget_path(struct dentry *Dentry, char *Buf, unsigned int Buflen)
{
	char *retval = &Buf[Buflen];
	struct dentry *p = Dentry;

	*(--retval) = '\0';
	Buflen--;

	if (!IS_ROOT(p) && !IS_ROOT(p->d_parent)) {
		while (Buflen && !IS_ROOT(p) && !IS_ROOT(p->d_parent)) {
			if (Buflen > p->d_name.len) {
				retval -= p->d_name.len;
				Buflen -= p->d_name.len;
				memcpy(retval, p->d_name.name, p->d_name.len);
				*(--retval) = '\\';
				Buflen--;
				p = p->d_parent;
			} else {
				retval = NULL;
				break;
			}
		}
	} else {
		*(--retval) = '\\';
	}

	if (retval)
		DbgPrint("novfs_dget_path: %s\n", retval);
	return (retval);
}

int verify_dentry(struct dentry *dentry, int Flags)
{
	int retVal = -ENOENT;
	struct inode *dir;
	struct novfs_entry_info *info = NULL;
	struct inode_data *id;
	struct novfs_schandle session;
	char *path, *list = NULL, *cp;
	ino_t ino = 0;
	struct qstr name;
	int iLock = 0;
	struct dentry *parent = NULL;
	u64 ctime;
	struct inode *inode;

	if (IS_ROOT(dentry)) {
		DbgPrint("verify_dentry: Root entry\n");
		return (0);
	}

	if (dentry && dentry->d_parent &&
	    (dir = dentry->d_parent->d_inode) && (id = dir->i_private)) {
		parent = dget_parent(dentry);

		info = kmalloc(sizeof(struct novfs_entry_info) + PATH_LENGTH_BUFFER, GFP_KERNEL);

		if (info) {
			if (novfs_lock_inode_cache(dir)) {
				name.len = dentry->d_name.len;
				name.name = dentry->d_name.name;
				name.hash = novfs_internal_hash(&name);
				if (!novfs_get_entry_time(dir, &name, &ino, info, &ctime)) {
					inode = dentry->d_inode;
					if (inode && inode->i_private &&
					    ((inode->i_size != info->size) ||
					     (inode->i_mtime.tv_sec !=
					      info->mtime.tv_sec)
					     || (inode->i_mtime.tv_nsec !=
						 info->mtime.tv_nsec))) {
						/*
						 * Values don't match so update.
						 */
						struct inode_data *n_inode = inode->i_private;
						n_inode->Flags |= UPDATE_INODE;
					}

					ctime = get_jiffies_64() - ctime;
					if (Flags || ctime < (u64) (novfs_update_timeout * HZ)) {
						retVal = 0;
						novfs_unlock_inode_cache(dir);
						dput(parent);
						kfree(info);
						return (0);
					}
				}
				novfs_unlock_inode_cache(dir);
			}

			if (IS_ROOT(dentry->d_parent)) {
				session =	novfs_scope_get_sessionId(
				novfs_get_scope_from_name(&dentry->d_name));
			} else
				session = novfs_scope_get_sessionId(id->Scope);

			if (!SC_PRESENT(session)) {
				id->Scope = novfs_get_scope(dentry);
				session = novfs_scope_get_sessionId(id->Scope);
			}

			ino = 0;
			retVal = 0;

			if (IS_ROOT(dentry->d_parent)) {
				DbgPrint("verify_dentry: parent is Root directory\n");
				list = novfs_get_scopeusers();

				iLock = novfs_lock_inode_cache(dir);
				novfs_invalidate_inode_cache(dir);

				if (list) {
					cp = list;
					while (*cp) {
						name.name = cp;
						name.len = strlen(cp);
						name.hash = novfs_internal_hash(&name);
						cp += (name.len + 1);
						ino = 0;
						if (novfs_get_entry(dir, &name, &ino, info)) {
							info->mode = S_IFDIR | 0700;
							info->size = 0;
							info->atime = info->ctime = info->mtime = CURRENT_TIME;
							ino = (ino_t)atomic_inc_return(&novfs_Inode_Number);
							novfs_add_inode_entry(dir, &name, ino, info);
						}
					}
				}
				novfs_free_invalid_entries(dir);
			} else {

				path =
				    novfs_dget_path(dentry, info->name,
						    PATH_LENGTH_BUFFER);
				if (path) {
					if (dentry->d_name.len <=
					    NW_MAX_PATH_LENGTH) {
						name.hash =
						    novfs_internal_hash
						    (&dentry->d_name);
						name.len = dentry->d_name.len;
						name.name = dentry->d_name.name;

						retVal =
						    novfs_get_file_info(path,
									info,
									session);
						if (0 == retVal) {
							dentry->d_time =
							    jiffies +
							    (novfs_update_timeout
							     * HZ);
							iLock =
							    novfs_lock_inode_cache
							    (dir);
							if (novfs_update_entry
							    (dir, &name, 0,
							     info)) {
								if (dentry->
								    d_inode) {
									ino = dentry->d_inode->i_ino;
								} else {
									ino = (ino_t)atomic_inc_return(&novfs_Inode_Number);
								}
								novfs_add_inode_entry
								    (dir, &name,
								     ino, info);
							}
							if (dentry->d_inode) {
								update_inode
								    (dentry->
								     d_inode,
								     info);
								id->Flags &=
								    ~UPDATE_INODE;

								dentry->
								    d_inode->
								    i_flags &=
								    ~S_DEAD;
								if (dentry->
								    d_inode->
								    i_private) {
									((struct inode_data *) dentry->d_inode->i_private)->Scope = id->Scope;
								}
							}
						} else if (-EINTR != retVal) {
							retVal = 0;
							iLock = novfs_lock_inode_cache(dir);
							novfs_remove_inode_entry(dir, &name, 0);
							if (dentry->d_inode
							    && !(dentry->d_inode->i_flags & S_DEAD)) {
								dentry->d_inode->i_flags |= S_DEAD;
								dentry->d_inode-> i_size = 0;
								dentry->d_inode->i_atime.tv_sec =
									dentry->d_inode->i_atime.tv_nsec =
									dentry->d_inode->i_ctime.tv_sec =
									dentry->d_inode->i_ctime.tv_nsec =
									dentry->d_inode->i_mtime.tv_sec =
									dentry->d_inode->i_mtime.tv_nsec = 0;
								dentry->d_inode->i_blocks = 0;
								d_delete(dentry);	/* Remove from cache */
							}
						}
					} else {
						retVal = -ENAMETOOLONG;
					}
				}
			}
		} else {
			retVal = -ENOMEM;
		}
		if (iLock) {
			novfs_unlock_inode_cache(dir);
		}
		dput(parent);
	}

	if (list)
		kfree(list);
	if (info)
		kfree(info);

	DbgPrint("verify_dentry: return=0x%x\n", retVal);

	return (retVal);
}


static int novfs_d_add(struct dentry *Parent, struct dentry *d, struct inode *i, int a)
{
	void *scope;
	struct inode_data *id = NULL;

	char *path, *buf;

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		path = novfs_dget_path(d, buf, PATH_LENGTH_BUFFER);
		if (path) {
			DbgPrint("novfs_d_add: inode=0x%p ino=%d path %s\n", i,
				 i->i_ino, path);
		}
		kfree(buf);
	}

	if (Parent && Parent->d_inode && Parent->d_inode->i_private) {
		id = (struct inode_data *) Parent->d_inode->i_private;
	}

	if (id && id->Scope) {
		scope = id->Scope;
	} else {
		scope = novfs_get_scope(d);
	}

	((struct inode_data *) i->i_private)->Scope = scope;

	d->d_time = jiffies + (novfs_update_timeout * HZ);
	if (a) {
		d_add(d, i);
	} else {
		d_instantiate(d, i);
	}

	return (0);
}

int novfs_d_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	int retCode = 0;
	struct inode *dir;
	struct inode_data *id;
	struct qstr name;

	DbgPrint("novfs_d_revalidate: 0x%p %.*s\n"
		 "   d_count: %d\n"
		 "   d_inode: 0x%p\n",
		 dentry, dentry->d_name.len, dentry->d_name.name,
		 dentry->d_count, dentry->d_inode);

	if (IS_ROOT(dentry)) {
		retCode = 1;
	} else {
		if (dentry->d_inode &&
		    dentry->d_parent &&
		    (dir = dentry->d_parent->d_inode) &&
		    (id = dir->i_private)) {
			/*
			 * Check timer to see if in valid time limit
			 */
			if (jiffies > dentry->d_time) {
				/*
				 * Revalidate entry
				 */
				name.len = dentry->d_name.len;
				name.name = dentry->d_name.name;
				name.hash =
				    novfs_internal_hash(&dentry->d_name);
				dentry->d_time = 0;

				if (0 == verify_dentry(dentry, 0)) {
					if (novfs_lock_inode_cache(dir)) {
						if (novfs_lookup_inode_cache
						    (dir, &name, 0)) {
							dentry->d_time =
							    jiffies +
							    (novfs_update_timeout
							     * HZ);
							retCode = 1;
						}
						novfs_unlock_inode_cache(dir);
					}
				}
			} else {
				retCode = 1;
			}
		}
	}

	if ((0 == retCode) && dentry->d_inode) {
		/*
		 * Entry has become invalid
		 */
/*      dput(dentry);
*/
	}

	DbgPrint("novfs_d_revalidate: return 0x%x %.*s\n", retCode,
		 dentry->d_name.len, dentry->d_name.name);

	return (retCode);
}

static unsigned long novfs_internal_hash(struct qstr *name)
{
	unsigned long hash = 0;
	unsigned int len = name->len;
	unsigned char *c = (unsigned char *)name->name;

	while (len--) {
		/*
		 * Lower case values for the hash.
		 */
		hash = partial_name_hash(tolower(*c++), hash);
	}

	return (hash);
}

int novfs_d_hash(struct dentry *dentry, struct qstr *name)
{
	DbgPrint("novfs_d_hash: %.*s\n", name->len, name->name);

	name->hash = novfs_internal_hash(name);

	return (0);
}

int novfs_d_strcmp(struct qstr *s1, struct qstr *s2)
{
	int retCode = 1;
	unsigned char *str1, *str2;
	unsigned int len;

	DbgPrint("novfs_d_strcmp: s1=%.*s s2=%.*s\n", s1->len, s1->name,
		 s2->len, s2->name);

	if (s1->len && (s1->len == s2->len) && (s1->hash == s2->hash)) {
		len = s1->len;
		str1 = (unsigned char *)s1->name;
		str2 = (unsigned char *)s2->name;
		for (retCode = 0; len--; str1++, str2++) {
			if (*str1 != *str2) {
				if (tolower(*str1) != tolower(*str2)) {
					retCode = 1;
					break;
				}
			}
		}
	}

	DbgPrint("novfs_d_strcmp: retCode=0x%x\n", retCode);
	return (retCode);
}

int novfs_d_compare(struct dentry *parent, struct qstr *s1, struct qstr *s2)
{
	int retCode;

	retCode = novfs_d_strcmp(s1, s2);

	DbgPrint("novfs_d_compare: retCode=0x%x\n", retCode);
	return (retCode);
}

int novfs_d_delete(struct dentry *dentry)
{
	int retVal = 0;

	DbgPrint("novfs_d_delete: 0x%p %.*s\n"
		 "   d_count: %d\n"
		 "   d_inode: 0x%p\n",
		 dentry, dentry->d_name.len, dentry->d_name.name,
		 dentry->d_count, dentry->d_inode);

	if (dentry->d_inode && (dentry->d_inode->i_flags & S_DEAD)) {
		retVal = 1;
	}

	dentry->d_time = 0;

	return (retVal);
}

void novfs_d_release(struct dentry *dentry)
{
	DbgPrint("novfs_d_release: 0x%p %.*s\n", dentry, dentry->d_name.len,
		 dentry->d_name.name);
}

void novfs_d_iput(struct dentry *dentry, struct inode *inode)
{
	DbgPrint
	    ("novfs_d_iput: Inode=0x%p Ino=%d Dentry=0x%p i_state=%d Name=%.*s\n",
	     inode, inode->i_ino, dentry, inode->i_state, dentry->d_name.len,
	     dentry->d_name.name);

	iput(inode);

}

int novfs_dir_open(struct inode *dir, struct file *file)
{
	char *path, *buf;
	struct file_private *file_private = NULL;

	DbgPrint("novfs_dir_open: Inode 0x%p %d Name %.*s\n", dir, dir->i_ino,
		 file->f_dentry->d_name.len, file->f_dentry->d_name.name);

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		path = novfs_dget_path(file->f_dentry, buf, PATH_LENGTH_BUFFER);
		if (path) {
			DbgPrint("novfs_dir_open: path %s\n", path);
		}
		kfree(buf);
	}

	file_private = kmalloc(sizeof(struct file_private), GFP_KERNEL);
	file_private->listedall = 0;
	file_private->enumHandle = NULL;

	file->private_data = file_private;

	return (0);
}

int novfs_dir_release(struct inode *dir, struct file *file)
{
	struct file_private *file_private;
	file_private = (struct file_private *) file->private_data;

	DbgPrint("novfs_dir_release: Inode 0x%p %d Name %.*s\n", dir,
		 dir->i_ino, file->f_dentry->d_name.len,
		 file->f_dentry->d_name.name);

	if (file_private) {
		kfree(file_private);
		file->private_data = NULL;
	}

	return (0);
}

loff_t novfs_dir_lseek(struct file * file, loff_t offset, int origin)
{
	struct file_private *file_private = NULL;

	DbgPrint("novfs_dir_lseek: offset %lld %d Name %.*s\n", offset, origin,
		 file->f_dentry->d_name.len, file->f_dentry->d_name.name);
	//printk("<1> seekdir file = %.*s offset = %i\n", file->f_dentry->d_name.len, file->f_dentry->d_name.name, (int)offset);

	if (0 != offset) {
		return -ESPIPE;
	}

	file->f_pos = 0;

	file_private = (struct file_private *) file->private_data;
	file_private->listedall = 0;
	file_private->enumHandle = NULL;

	return 0;
	//return(default_llseek(file, offset, origin));
}

ssize_t novfs_dir_read(struct file * file, char *buf, size_t len, loff_t * off)
{
/*
   int rlen = 0;

   DbgPrint("novfs_dir_readdir: dentry path %.*s buf=0x%p len=%d off=%lld\n", file->f_dentry->d_name.len, file->f_dentry->d_name.name, buf, len, *off);

   if (0 == *off)
   {
      rlen = 8;
      rlen -= copy_to_user(buf, "Testing\n", 8);
      *off += rlen;
   }
   return(rlen);
*/
	DbgPrint("novfs_dir_read: %lld %d Name %.*s\n", *off, len,
		 file->f_dentry->d_name.len, file->f_dentry->d_name.name);
	return (generic_read_dir(file, buf, len, off));
}

static void novfs_Dump_Info(struct novfs_entry_info *info)
{
	char atime_buf[32], mtime_buf[32], ctime_buf[32];
	char namebuf[512];
	int len = 0;

	if (info == NULL) {
		DbgPrint("novfs_dir_readdir : Dump_Info info == NULL\n");
		return;
	}

	if (info->namelength >= 512) {
		len = 511;
	} else {
		len = info->namelength;
	}

	memcpy(namebuf, info->name, len);
	namebuf[len] = '\0';

	ctime_r(&info->atime.tv_sec, atime_buf);
	ctime_r(&info->mtime.tv_sec, mtime_buf);
	ctime_r(&info->ctime.tv_sec, ctime_buf);
	DbgPrint("novfs_dir_readdir : type = %i\n", info->type);
	DbgPrint("novfs_dir_readdir : mode = %x\n", info->mode);
	DbgPrint("novfs_dir_readdir : uid = %d\n", info->uid);
	DbgPrint("novfs_dir_readdir : gid = %d\n", info->gid);
	DbgPrint("novfs_dir_readdir : size = %i\n", info->size);
	DbgPrint("novfs_dir_readdir : atime = %s\n", atime_buf);
	DbgPrint("novfs_dir_readdir : mtime = %s\n", mtime_buf);
	DbgPrint("novfs_dir_readdir : ctime = %s\n", ctime_buf);
	DbgPrint("novfs_dir_readdir : namelength = %i\n", info->namelength);
	DbgPrint("novfs_dir_readdir : name = %s\n", namebuf);
}

void processList(struct file *file, void *dirent, filldir_t filldir, char *list,
		 int type, struct novfs_schandle SessionId)
{
	unsigned char *path, *buf = NULL, *cp;
	struct qstr name;
	struct novfs_entry_info *pinfo = NULL;

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	path = buf;
	if (buf) {
		path = novfs_dget_path(file->f_dentry, buf, PATH_LENGTH_BUFFER);
		if (path) {
			strcpy(buf, path);
		}
		path = buf + strlen(buf);
		*path++ = '\\';
	}

	if (list) {
		cp = list;
		while (*cp) {
			name.name = cp;
			DbgPrint("novfs_dir_readdir : name.name = %s\n",
				 name.name);
			name.len = strlen(cp);
			name.hash = novfs_internal_hash(&name);
			cp += (name.len + 1);

			pinfo =
			    kmalloc(sizeof(struct novfs_entry_info) +
					 PATH_LENGTH_BUFFER, GFP_KERNEL);
			pinfo->mode = S_IFDIR | 0700;
			pinfo->size = 0;
			pinfo->atime = pinfo->ctime = pinfo->mtime =
			    CURRENT_TIME;
			strcpy(pinfo->name, name.name);
			pinfo->namelength = name.len;

			novfs_Dump_Info(pinfo);

			filldir(dirent, pinfo->name, pinfo->namelength,
				file->f_pos, file->f_pos, pinfo->mode >> 12);
			file->f_pos += 1;

			kfree(pinfo);
		}
	}

	if (buf) {
		kfree(buf);
	}
}

int processEntries(struct file *file, void *dirent, filldir_t filldir,
		   void ** enumHandle, struct novfs_schandle sessionId)
{
	unsigned char *path = NULL, *buf = NULL;
	int count = 0, status = 0;
	struct novfs_entry_info *pinfo = NULL;
	struct novfs_entry_info *pInfoMem = NULL;

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (!buf) {
		return -ENOMEM;
	}

	path = novfs_dget_path(file->f_dentry, buf, PATH_LENGTH_BUFFER);
	if (!path) {
		kfree(buf);
		return -ENOMEM;
	}
	//NWSearchfiles
	count = 0;
	status =
	    novfs_get_dir_listex(path, enumHandle, &count, &pinfo,
				       sessionId);
	pInfoMem = pinfo;

	if ((count == -1) || (count == 0) || (status != 0)) {
		kfree(pInfoMem);
		kfree(buf);
		return -1;
	}
	// parse resultset
	while (pinfo && count--) {
		filldir(dirent, pinfo->name, pinfo->namelength, file->f_pos,
			file->f_pos, pinfo->mode >> 12);
		file->f_pos += 1;

		pinfo = (struct novfs_entry_info *) (pinfo->name + pinfo->namelength);
	}

	kfree(pInfoMem);
	kfree(buf);
	return 0;
}

int novfs_dir_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	unsigned char *list = NULL;
	int status = 0;		//-ENOMEM;
	struct inode *inode = file->f_dentry->d_inode;
	struct novfs_schandle sessionId;
	uid_t uid;
	int type = 0;
	struct file_private *file_private = NULL;
	int lComm;

	file_private = (struct file_private *) file->private_data;
	DbgPrint("novfs_dir_readdir: Name %.*s\n", file->f_dentry->d_name.len,
		 file->f_dentry->d_name.name);

	//printk("<1> file = %.*s\n", file->f_dentry->d_name.len, file->f_dentry->d_name.name);

// Use this hack by default
#ifndef SKIP_CROSSOVER_HACK
	// Hack for crossover - begin
	down(&TimeDir_Lock);
	if ((file->f_dentry->d_name.len == 7) &&
	    ((0 == strncmp(file->f_dentry->d_name.name, " !xover", 7)) ||
	     (0 == strncmp(file->f_dentry->d_name.name, "z!xover", 7)))) {
		//printk("<1> xoverhack: we are in xoverHack\n");

		inHAX = 1;
		inHAXTime = get_nanosecond_time();
		//up( &TimeDir_Lock );
		//return 0;
		file_private->listedall = 1;
	} else {
		if (inHAX) {
			if (get_nanosecond_time() - inHAXTime >
			    100 * 1000 * 1000) {
				//printk("<1> xoverhack: it was long, long, long ago...\n");
				inHAX = 0;
			} else {
				//printk("<1> xoverhack: word gotcha in xoverHack...\n");
				inHAXTime = get_nanosecond_time();
				//up( &TimeDir_Lock );
				//return 0;
				file_private->listedall = 1;
			}
		}
	}

	up(&TimeDir_Lock);
	// Hack for crossover - end
#endif

	if (file->f_pos == 0) {
		if (filldir(dirent, ".", 1, file->f_pos, inode->i_ino, DT_DIR) <
		    0)
			return 1;
		file->f_pos++;
		return 1;
	}

	if (file->f_pos == 1) {
		if (filldir
		    (dirent, "..", 2, file->f_pos,
		     file->f_dentry->d_parent->d_inode->i_ino, DT_DIR) < 0)
			return 1;
		file->f_pos++;
		return 1;
	}

	if (file_private->listedall != 0) {
		return 0;
	}

	inode = file->f_dentry->d_inode;
	if (inode && inode->i_private) {
		sessionId =
		    novfs_scope_get_sessionId(((struct inode_data *) inode->i_private)->
					Scope);
		if (0 == SC_PRESENT(sessionId)) {
			((struct inode_data *) inode->i_private)->Scope =
			    novfs_get_scope(file->f_dentry);
			sessionId =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
		}
		uid = novfs_scope_get_uid(((struct inode_data *) inode->i_private)->Scope);
	} else {
		SC_INITIALIZE(sessionId);
		uid = current->euid;
	}

	if (IS_ROOT(file->f_dentry) ||	// Root
	    IS_ROOT(file->f_dentry->d_parent) ||	// User
	    IS_ROOT(file->f_dentry->d_parent->d_parent))	// Server
	{
		if (IS_ROOT(file->f_dentry)) {
			DbgPrint("novfs_dir_readdir: Root directory\n");
			list = novfs_get_scopeusers();
			type = USER_LIST;
		} else if (IS_ROOT(file->f_dentry->d_parent)) {
			DbgPrint
			    ("novfs_dir_readdir: Parent is Root directory\n");
			novfs_get_servers(&list, sessionId);
			type = SERVER_LIST;
		} else {
			DbgPrint
			    ("novfs_dir_readdir: Parent-Parent is Root directory\n");
			novfs_get_vols(&file->f_dentry->d_name,
						     &list, sessionId);
			type = VOLUME_LIST;
		}

		processList(file, dirent, filldir, list, type, sessionId);
		file_private->listedall = 1;
	} else {
		status =
		    processEntries(file, dirent, filldir,
				   &file_private->enumHandle, sessionId);

		if (status != 0) {
			file_private->listedall = 1;
#ifndef SKIP_CROSSOVER_HACK
			// Hack for crossover part 2 - begin
			lComm = strlen(current->comm);
			if ((lComm > 4)
			    && (0 ==
				strcmp(current->comm + lComm - 4, ".EXE"))) {
				if (filldir
				    (dirent, " !xover", 7, file->f_pos,
				     inode->i_ino, DT_DIR) < 0)
					return 1;
				if (filldir
				    (dirent, "z!xover", 7, file->f_pos,
				     inode->i_ino, DT_DIR) < 0)
					return 1;
				file->f_pos += 2;
			}
			// Hack for crossover part2 - end
#endif
		}
	}

	file->private_data = file_private;
	return 1;
}

int novfs_dir_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	DbgPrint("novfs_dir_fsync: Name %.*s\n", file->f_dentry->d_name.len,
		 file->f_dentry->d_name.name);
	return (simple_sync_file(file, dentry, datasync));
}

ssize_t novfs_f_read(struct file * file, char *buf, size_t len, loff_t * off)
{
	size_t thisread, totalread = 0;
	loff_t offset = *off;
	struct inode *inode;
	struct novfs_schandle session;
	struct inode_data *id;

	if (file->f_dentry &&
	    (inode = file->f_dentry->d_inode) &&
	    (id = (struct inode_data *) inode->i_private)) {

		DbgPrint("novfs_f_read(0x%p 0x%p %d %lld %.*s)\n",
			 file->private_data,
			 buf, len, offset,
			 file->f_dentry->d_name.len,
			 file->f_dentry->d_name.name);

		if (novfs_page_cache && !(file->f_flags & O_DIRECT) && id->CacheFlag) {
			totalread = do_sync_read(file, buf, len, off);
		} else {
			session = novfs_scope_get_sessionId(id->Scope);
			if (0 == SC_PRESENT(session)) {
				id->Scope =
				    novfs_get_scope(file->f_dentry);
				session = novfs_scope_get_sessionId(id->Scope);
			}

			while (len > 0 && (offset < i_size_read(inode))) {
				int retval;
				thisread = len;
				retval =
				    novfs_read_file(file->private_data, buf,
						    &thisread, &offset,
						    session);
				if (retval || !thisread) {
					if (retval) {
						totalread = retval;
					}
					break;
				}
				DbgPrint("novfs_f_read thisread = 0x%x\n",
					 thisread);
				len -= thisread;
				buf += thisread;
				offset += thisread;
				totalread += thisread;
			}
			*off = offset;
		}
	}
	DbgPrint("novfs_f_read return = %d\n", totalread);

	return (totalread);
}

ssize_t novfs_f_write(struct file * file, const char *buf, size_t len,
		      loff_t * off)
{
	ssize_t thiswrite, totalwrite = 0;
	loff_t offset = *off;
	struct novfs_schandle session;
	struct inode *inode;
	int status;
	struct inode_data *id;

	if (file->f_dentry &&
	    (inode = file->f_dentry->d_inode) &&
	    (id = file->f_dentry->d_inode->i_private)) {
		DbgPrint("novfs_f_write(0x%p 0x%p 0x%p %d %lld %.*s)\n",
			 file->private_data, inode, id->FileHandle, len, offset,
			 file->f_dentry->d_name.len,
			 file->f_dentry->d_name.name);

		if (novfs_page_cache &&
		    !(file->f_flags & O_DIRECT) &&
		    id->CacheFlag && !(file->f_flags & O_WRONLY)) {
			totalwrite = do_sync_write(file, buf, len, off);
		} else {
			if (file->f_flags & O_APPEND) {
				offset = i_size_read(inode);
				DbgPrint
				    ("novfs_f_write appending to end %lld %.*s\n",
				     offset, file->f_dentry->d_name.len,
				     file->f_dentry->d_name.name);
			}

			session = novfs_scope_get_sessionId(id->Scope);
			if (0 == SC_PRESENT(session)) {
				id->Scope =
				    novfs_get_scope(file->f_dentry);
				session = novfs_scope_get_sessionId(id->Scope);
			}

			while (len > 0) {
				thiswrite = len;
				if ((status =
				     novfs_write_file(file->private_data,
						      (unsigned char *)buf,
						      &thiswrite, &offset,
						      session)) || !thiswrite) {
					totalwrite = status;
					break;
				}
				DbgPrint("novfs_f_write thiswrite = 0x%x\n",
					 thiswrite);
				len -= thiswrite;
				buf += thiswrite;
				offset += thiswrite;
				totalwrite += thiswrite;
				if (offset > i_size_read(inode)) {
					i_size_write(inode, offset);
					inode->i_blocks =
					    (offset + inode->i_sb->s_blocksize -
					     1) >> inode->i_blkbits;
				}
				inode->i_mtime = inode->i_atime = CURRENT_TIME;
				id->Flags |= UPDATE_INODE;

			}
			*off = offset;
		}
	}
	DbgPrint("novfs_f_write return = 0x%x\n", totalwrite);

	return (totalwrite);
}

int novfs_f_readdir(struct file *file, void *data, filldir_t fill)
{
	return -EISDIR;
}

int novfs_f_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		  unsigned long arg)
{
	DbgPrint("novfs_f_ioctl: file=0x%p cmd=0x%x arg=0x%p\n", file, cmd,
		 arg);

	return -ENOSYS;
}

int novfs_f_mmap(struct file *file, struct vm_area_struct *vma)
{
	int retCode = -EINVAL;

	DbgPrint("novfs_f_mmap: file=0x%p %.*s\n", file,
		 file->f_dentry->d_name.len, file->f_dentry->d_name.name);

	retCode = generic_file_mmap(file, vma);

	DbgPrint("novfs_f_mmap: retCode=0x%x\n", retCode);
	return (retCode);
}

int novfs_f_open(struct inode *inode, struct file *file)
{
	struct novfs_entry_info *info = NULL;
	int retCode = -ENOENT;
	struct novfs_schandle session;
	char *path;
	struct dentry *parent;
	ino_t ino;
	struct inode_data *id;
	int errInfo;

	DbgPrint
	    ("novfs_f_open: inode=0x%p file=0x%p dentry=0x%p dentry->d_inode=0x%p %.*s\n",
	     inode, file, file->f_dentry, file->f_dentry->d_inode,
	     file->f_dentry->d_name.len, file->f_dentry->d_name.name);
	if (file->f_dentry) {
		DbgPrint
		    ("novfs_f_open: %.*s f_flags=0%o f_mode=0%o i_mode=0%o\n",
		     file->f_dentry->d_name.len, file->f_dentry->d_name.name,
		     file->f_flags, file->f_mode, inode->i_mode);
	}

	if (inode && inode->i_private) {
		id = (struct inode_data *) file->f_dentry->d_inode->i_private;
		session = novfs_scope_get_sessionId(id->Scope);
		if (0 == SC_PRESENT(session)) {
			id->Scope = novfs_get_scope(file->f_dentry);
			session = novfs_scope_get_sessionId(id->Scope);
		}

		info = kmalloc(sizeof(struct novfs_entry_info) +
					       PATH_LENGTH_BUFFER, GFP_KERNEL);
		if (info) {
			path =
			    novfs_dget_path(file->f_dentry, info->name,
					    PATH_LENGTH_BUFFER);
			if (path) {
				if (file->f_flags & O_TRUNC) {
					errInfo =
					    novfs_get_file_info(path, info,
								session);

					if (errInfo || info->size == 0) {
						// clear O_TRUNC flag, bug #275366
						file->f_flags =
						    file->f_flags & (~O_TRUNC);
					}
				}

				DbgPrint("novfs_f_open: %s\n", path);
				retCode = novfs_open_file(path,
							  file->
							  f_flags & ~O_EXCL,
							  info,
							  &file->private_data,
							  session);

				DbgPrint("novfs_f_open: 0x%x 0x%p\n", retCode,
					 file->private_data);
				if (!retCode) {
					/*
					 *update_inode(inode, &info);
					 */
					//id->FileHandle = file->private_data;
					id->CacheFlag =
					    novfs_get_file_cache_flag(path,
								      session);

					if (!novfs_get_file_info
					    (path, info, session)) {
						update_inode(inode, info);
					}

					parent = dget_parent(file->f_dentry);

					if (parent && parent->d_inode) {
						struct inode *dir =
						    parent->d_inode;
						novfs_lock_inode_cache(dir);
						ino = 0;
						if (novfs_get_entry
						    (dir,
						     &file->f_dentry->d_name,
						     &ino, info)) {
							((struct inode_data *) inode->
							 i_private)->Flags |=
				       UPDATE_INODE;
						}

						novfs_unlock_inode_cache(dir);
					}
					dput(parent);
				}
			}
			kfree(info);
		}
	}
	DbgPrint("novfs_f_open: retCode=0x%x\n", retCode);
	return (retCode);
}

int novfs_flush_mapping(void *Handle, struct address_space *mapping,
			struct novfs_schandle Session)
{
	struct pagevec pagevec;
	unsigned nrpages;
	pgoff_t index = 0;
	int done, rc = 0;

	pagevec_init(&pagevec, 0);

	do {
		done = 1;
		nrpages = pagevec_lookup_tag(&pagevec,
					     mapping,
					     &index,
					     PAGECACHE_TAG_DIRTY, PAGEVEC_SIZE);

		if (nrpages) {
			struct page *page;
			int i;

			DbgPrint("novfs_flush_mapping: %u\n", nrpages);

			done = 0;
			for (i = 0; !rc && (i < nrpages); i++) {
				page = pagevec.pages[i];

				DbgPrint("novfs_flush_mapping: page 0x%p %lu\n",
					 page, page->index);

				lock_page(page);
				page_cache_get(page);
				if (page->mapping == mapping) {
					if (clear_page_dirty_for_io(page)) {
						rc = novfs_write_page(Handle,
								      page,
								      Session);
						if (!rc) {
							//ClearPageDirty(page);
							radix_tree_tag_clear
							    (&mapping->
							     page_tree,
							     page_index(page),
							     PAGECACHE_TAG_DIRTY);
						}
					}
				}

				page_cache_release(page);
				unlock_page(page);
			}
			pagevec_release(&pagevec);
		}
	} while (!rc && !done);

	DbgPrint("novfs_flush_mapping: return %d\n", rc);

	return (rc);
}

int novfs_f_flush(struct file *file, fl_owner_t ownid)
{

	int rc = 0;
#ifdef FLUSH
	struct inode *inode;
	struct novfs_schandle session;
	struct inode_data *id;

	DbgPrint("novfs_f_flush: Called from 0x%p\n",
		 __builtin_return_address(0));
	if (file->f_dentry && (inode = file->f_dentry->d_inode)
	    && (id = file->f_dentry->d_inode->i_private)) {

		if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
			inode = file->f_dentry->d_inode;
			DbgPrint
			    ("novfs_f_flush: %.*s f_flags=0%o f_mode=0%o i_mode=0%o\n",
			     file->f_dentry->d_name.len,
			     file->f_dentry->d_name.name, file->f_flags,
			     file->f_mode, inode->i_mode);

			session = novfs_scope_get_sessionId(id->Scope);
			if (0 == SC_PRESENT(session)) {
				id->Scope =
				    novfs_get_scope(file->f_dentry);
				session = novfs_scope_get_sessionId(id->Scope);
			}

			if (inode &&
			    inode->i_mapping && inode->i_mapping->nrpages) {

				DbgPrint("novfs_f_flush: %.*s pages=%lu\n",
					 file->f_dentry->d_name.len,
					 file->f_dentry->d_name.name,
					 inode->i_mapping->nrpages);

				if (file->f_dentry &&
				    file->f_dentry->d_inode &&
				    file->f_dentry->d_inode->i_mapping &&
				    file->f_dentry->d_inode->i_mapping->a_ops &&
				    file->f_dentry->d_inode->i_mapping->a_ops->
				    writepage) {
					rc = filemap_fdatawrite(file->f_dentry->
								d_inode->
								i_mapping);
				} else {
					rc = novfs_flush_mapping(file->
								 private_data,
								 file->
								 f_dentry->
								 d_inode->
								 i_mapping,
								 session);
				}
			}
		}
	}
#endif
	return (rc);
}

int novfs_f_release(struct inode *inode, struct file *file)
{
	int retCode = -EACCES;
	struct novfs_schandle session;
	struct inode_data *id;

	DbgPrint("novfs_f_release: path=%.*s handle=%p\n",
		 file->f_dentry->d_name.len,
		 file->f_dentry->d_name.name, file->private_data);

	if (inode && (id = inode->i_private)) {
		session = novfs_scope_get_sessionId(id->Scope);
		if (0 == SC_PRESENT(session)) {
			id->Scope = novfs_get_scope(file->f_dentry);
			session = novfs_scope_get_sessionId(id->Scope);
		}

		if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
			DbgPrint
			    ("novfs_f_release: %.*s f_flags=0%o f_mode=0%o i_mode=0%o\n",
			     file->f_dentry->d_name.len,
			     file->f_dentry->d_name.name, file->f_flags,
			     file->f_mode, inode->i_mode);

			if (inode->i_mapping && inode->i_mapping->nrpages) {

				DbgPrint("novfs_f_release: %.*s pages=%lu\n",
					 file->f_dentry->d_name.len,
					 file->f_dentry->d_name.name,
					 inode->i_mapping->nrpages);

				if (inode->i_mapping->a_ops &&
				    inode->i_mapping->a_ops->writepage) {
					filemap_fdatawrite(file->f_dentry->
							   d_inode->i_mapping);
				} else {
					novfs_flush_mapping(file->private_data,
							    file->f_dentry->
							    d_inode->i_mapping,
							    session);
				}
			}
		}

		if (file->f_dentry && file->f_dentry->d_inode) {
			invalidate_remote_inode(file->f_dentry->d_inode);
		}

		retCode = novfs_close_file(file->private_data, session);
		//id->FileHandle = 0;
	}
	return (retCode);
}

int novfs_f_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	return 0;
}

int novfs_f_llseek(struct file *file, loff_t offset, int origin)
{
	DbgPrint("novfs_f_llseek: File=0x%p Name=%.*s offset=%lld origin=%d\n",
		 file, file->f_dentry->d_name.len, file->f_dentry->d_name.name,
		 offset, origin);
	return (generic_file_llseek(file, offset, origin));
}

/*++======================================================================*/
int novfs_f_lock(struct file *file, int cmd, struct file_lock *lock)
/*
 *  Arguments:
 *      "file" - pointer to file structure - contains file handle in "file->private_data"
 *
 *      "cmd" could be F_SETLK, F_SETLKW, F_GETLK
 *      F_SETLK/F_SETLKW are for setting/unsetting file lock
 *      F_GETLK is for getting infomation about region - is it locked, or not
 *
 *      "lock" structure - contains "start" and "end" of locking region
 *
 *  Returns:
 *      0 on success
 *      -ENOSYS on F_GETLK cmd. It's not implemented.
 *      -EINVAL if (lock->fl_start > lock->fl_end)
 *      -EAGAIN on all other errors
 *  Abstract:
 *
 *  Notes:
 *      "lock->fl_start" and "lock->fl_end" are of type "long long",
 *      but xtier functions in novfsd "NCFsdLockFile" and "NCFsdUnlockFile"
 *      receive arguments in u64 type.
 *
 *
 *========================================================================*/
{
	int err_code;

	struct inode *inode;
	struct novfs_schandle session;
	struct inode_data *id;
	loff_t len;

	DbgPrint("novfs_f_lock(0x%p): begin in novfs_f_lock 0x%p\n",
		 __builtin_return_address(0), file->private_data);
	DbgPrint
	    ("novfs_f_lock: cmd = %d, F_GETLK = %d, F_SETLK = %d, F_SETLKW = %d\n",
	     cmd, F_GETLK, F_SETLK, F_SETLKW);
	DbgPrint
	    ("novfs_f_lock: lock->fl_start = 0x%llX, lock->fl_end = 0x%llX\n",
	     lock->fl_start, lock->fl_end);

	err_code = -1;
	if (lock->fl_start <= lock->fl_end) {
		/* Get len from "start" and "end" */
		len = lock->fl_end - lock->fl_start + 1;
		if ((0 == lock->fl_start) && (OFFSET_MAX == lock->fl_end)) {
			len = 0;
		}

		if (file->f_dentry &&
		    (inode = file->f_dentry->d_inode) &&
		    (id = (struct inode_data *) inode->i_private)) {
			DbgPrint("novfs_f_lock: (0x%p 0x%p %.*s)\n",
				 file->private_data, inode,
				 file->f_dentry->d_name.len,
				 file->f_dentry->d_name.name);

			session = novfs_scope_get_sessionId(id->Scope);
			if (0 == SC_PRESENT(session)) {
				id->Scope =
				    novfs_get_scope(file->f_dentry);
				session = novfs_scope_get_sessionId(id->Scope);
			}

			/* fl_type = F_RDLCK, F_WRLCK, F_UNLCK */
			switch (cmd) {
			case F_SETLK:
#ifdef F_GETLK64
			case F_SETLK64:
#endif

				err_code =
				    novfs_set_file_lock(session,
							file->private_data,
							lock->fl_type,
							lock->fl_start, len);
				break;

			case F_SETLKW:
#ifdef F_GETLK64
			case F_SETLKW64:
#endif
				err_code =
				    novfs_set_file_lock(session,
							file->private_data,
							lock->fl_type,
							lock->fl_start, len);
				break;

			case F_GETLK:
#ifdef F_GETLK64
			case F_GETLK64:
#endif
				err_code = -ENOSYS;
				/*
				 * Not implemented. We doesn't have appropriate xtier function.
				 * */
				break;

			default:
				printk
				    ("<1> novfs in novfs_f_lock, not implemented cmd = %d\n",
				     cmd);
				DbgPrint
				    ("novfs_f_lock: novfs in novfs_f_lock, not implemented cmd = %d\n",
				     cmd);
				break;
			}
		}

		DbgPrint("novfs_f_lock: lock->fl_type = %u, err_code 0x%X\n",
			 lock->fl_type, err_code);

		if ((err_code != 0) && (err_code != -1)
		    && (err_code != -ENOSYS)) {
			err_code = -EAGAIN;
		}
	} else {
		err_code = -EINVAL;
	}

	return (err_code);
}

/*++======================================================================*/
static void novfs_copy_cache_pages(struct address_space *mapping,
				   struct list_head *pages, int bytes_read,
				   char *data, struct pagevec *plru_pvec)
{
	struct page *page;
	char *target;

	while (bytes_read > 0) {
		if (list_empty(pages))
			break;

		page = list_entry(pages->prev, struct page, lru);
		list_del(&page->lru);

		if (add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
			page_cache_release(page);
			data += PAGE_CACHE_SIZE;
			bytes_read -= PAGE_CACHE_SIZE;
			continue;
		}

		target = kmap_atomic(page, KM_USER0);

		if (PAGE_CACHE_SIZE > bytes_read) {
			memcpy(target, data, bytes_read);
			/* zero the tail end of this partial page */
			memset(target + bytes_read, 0,
			       PAGE_CACHE_SIZE - bytes_read);
			bytes_read = 0;
		} else {
			memcpy(target, data, PAGE_CACHE_SIZE);
			bytes_read -= PAGE_CACHE_SIZE;
		}
		kunmap_atomic(target, KM_USER0);

		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);
		if (!pagevec_add(plru_pvec, page))
			__pagevec_lru_add(plru_pvec);
		data += PAGE_CACHE_SIZE;
	}
	return;
}

int novfs_a_writepage(struct page *page, struct writeback_control *wbc)
{
	int retCode = -EFAULT;
	struct inode *inode = page->mapping->host;
	struct inode_data *id = inode->i_private;
	loff_t pos = ((loff_t) page->index << PAGE_CACHE_SHIFT);
	struct novfs_schandle session;
	struct novfs_data_list dlst[2];
	size_t len = PAGE_CACHE_SIZE;

	session = novfs_scope_get_sessionId(((struct inode_data *) inode->i_private)->Scope);

	page_cache_get(page);

	pos = ((loff_t) page->index << PAGE_CACHE_SHIFT);

	/*
	 * Leave first dlst entry for reply header.
	 */
	dlst[1].page = page;
	dlst[1].offset = NULL;
	dlst[1].len = len;
	dlst[1].rwflag = DLREAD;

	/*
	 * Check size so we don't write pass end of file.
	 */
	if ((pos + (loff_t) len) > i_size_read(inode)) {
		len = (size_t) (i_size_read(inode) - pos);
	}

	retCode = novfs_write_pages(id->FileHandle, dlst, 2, len, pos, session);
	if (!retCode) {
		SetPageUptodate(page);
	}

	unlock_page(page);
	page_cache_release(page);

	return (retCode);
}

int novfs_a_writepages(struct address_space *mapping,
		       struct writeback_control *wbc)
{
	int retCode = 0;
	struct inode *inode = mapping->host;
	struct novfs_schandle session;
	void *fh = NULL;
	struct inode_data *id = NULL;

	int max_page_lookup = novfs_max_iosize / PAGE_CACHE_SIZE;

	struct novfs_data_list *dlist, *dlptr;
	struct page **pages;

	int dlist_idx, i = 0;
	pgoff_t index, next_index = 0;
	loff_t pos = 0;
	size_t tsize;

	SC_INITIALIZE(session);
	DbgPrint
	    ("novfs_a_writepages: inode=0x%p mapping=0x%p wbc=0x%p nr_to_write=%d\n",
	     inode, mapping, wbc, wbc->nr_to_write);

	if (inode) {
		DbgPrint(" Inode=0x%p Ino=%d Id=0x%p\n", inode, inode->i_ino,
			 inode->i_private);

		if (NULL != (id = inode->i_private)) {
			session =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
			fh = ((struct inode_data *) inode->i_private)->FileHandle;
		}
	}

	dlist = kmalloc(sizeof(struct novfs_data_list) * max_page_lookup, GFP_KERNEL);
	pages =
	    kmalloc(sizeof(struct page *) * max_page_lookup, GFP_KERNEL);

	if (id)
		DbgPrint
		    ("novfs_a_writepages: inode=0x%p fh=0x%p dlist=0x%p pages=0x%p %s\n",
		     inode, fh, dlist, pages, id->Name);
	else
		DbgPrint
		    ("novfs_a_writepages: inode=0x%p fh=0x%p dlist=0x%p pages=0x%p\n",
		     inode, fh, dlist, pages);

	if (dlist && pages) {
		struct backing_dev_info *bdi = mapping->backing_dev_info;
		int done = 0;
		int nr_pages = 0;
		int scanned = 0;

		if (wbc->nonblocking && bdi_write_congested(bdi)) {
			wbc->encountered_congestion = 1;
			return 0;
		}

		if (wbc->sync_mode == WB_SYNC_NONE) {
			index = mapping->writeback_index;	/* Start from prev offset */
		} else {
			index = 0;	/* whole-file sweep */
			scanned = 1;
		}

		next_index = index;

		while (!done && (wbc->nr_to_write > 0)) {
			dlist_idx = 0;
			dlptr = &dlist[1];

			DbgPrint("novfs_a_writepages1: nr_pages=%d\n",
				 nr_pages);
			if (!nr_pages) {
				memset(pages, 0,
				       sizeof(struct page *) * max_page_lookup);

				read_lock_irq(&mapping->tree_lock);

				/*
				 * Need to ask for one less then max_page_lookup or we
				 * will overflow the request buffer.  This also frees
				 * the first entry for the reply buffer.
				 */
				nr_pages =
				    radix_tree_gang_lookup_tag(&mapping->
							       page_tree,
							       (void **)pages,
							       index,
							       max_page_lookup -
							       1,
							       PAGECACHE_TAG_DIRTY);

				DbgPrint("novfs_a_writepages2: nr_pages=%d\n",
					 nr_pages);
				/*
				 * Check to see if there are dirty pages and there is a valid
				 * file handle.
				 */
				if (nr_pages && !fh) {
					set_bit(AS_EIO, &mapping->flags);
					done = 1;
					DbgPrint
					    ("novfs_a_writepage: set_bit AS_EIO\n");
					break;
				}

				for (i = 0; i < nr_pages; i++) {
					page_cache_get(pages[i]);
				}

				read_unlock_irq(&mapping->tree_lock);

				if (nr_pages) {
					index = pages[nr_pages - 1]->index + 1;
					pos =
					    (loff_t) pages[0]->
					    index << PAGE_CACHE_SHIFT;
				}

				if (!nr_pages) {
					if (scanned) {
						index = 0;
						scanned = 0;
						continue;
					}
					done = 1;
				} else {
					next_index = pages[0]->index;
					i = 0;
				}
			} else {
				if (pages[i]) {
					pos =
					    (loff_t) pages[i]->
					    index << PAGE_CACHE_SHIFT;
				}
			}

			for (; i < nr_pages; i++) {
				struct page *page = pages[i];

				/*
				 * At this point we hold neither mapping->tree_lock nor
				 * lock on the page itself: the page may be truncated or
				 * invalidated (changing page->mapping to NULL), or even
				 * swizzled back from swapper_space to tmpfs file
				 * mapping
				 */

				DbgPrint
				    ("novfs_a_writepages: pos=0x%llx index=%d page->index=%d next_index=%d\n",
				     pos, index, page->index, next_index);

				if (page->index != next_index) {
					next_index = page->index;
					break;
				}
				next_index = page->index + 1;

				lock_page(page);

				if (wbc->sync_mode != WB_SYNC_NONE)
					wait_on_page_writeback(page);

				if (page->mapping != mapping
				    || PageWriteback(page)
				    || !clear_page_dirty_for_io(page)) {
					unlock_page(page);
					continue;
				}

				dlptr[dlist_idx].page = page;
				dlptr[dlist_idx].offset = NULL;
				dlptr[dlist_idx].len = PAGE_CACHE_SIZE;
				dlptr[dlist_idx].rwflag = DLREAD;
				dlist_idx++;
				DbgPrint
				    ("novfs_a_writepages: Add page=0x%p index=0x%lx\n",
				     page, page->index);
			}

			DbgPrint("novfs_a_writepages: dlist_idx=%d\n",
				 dlist_idx);
			if (dlist_idx) {
				tsize = dlist_idx * PAGE_CACHE_SIZE;
				/*
				 * Check size so we don't write pass end of file.
				 */
				if ((pos + tsize) > i_size_read(inode)) {
					tsize =
					    (size_t) (i_size_read(inode) - pos);
				}

				retCode =
				    novfs_write_pages(fh, dlist, dlist_idx + 1,
						      tsize, pos, session);
				switch (retCode) {
				case 0:
					wbc->nr_to_write -= dlist_idx;
					break;

				case -ENOSPC:
					set_bit(AS_ENOSPC, &mapping->flags);
					done = 1;
					break;

				default:
					set_bit(AS_EIO, &mapping->flags);
					done = 1;
					break;
				}

				do {
					unlock_page((struct page *)
						    dlptr[dlist_idx - 1].page);
					page_cache_release((struct page *)
							   dlptr[dlist_idx -
								 1].page);
					DbgPrint
					    ("novfs_a_writepages: release page=0x%p index=0x%lx\n",
					     dlptr[dlist_idx - 1].page,
					     ((struct page *)
					      dlptr[dlist_idx -
						    1].page)->index);
					if (!retCode) {
						wbc->nr_to_write--;
					}
				} while (--dlist_idx);
			}

			if (i >= nr_pages) {
				nr_pages = 0;
			}
		}

		mapping->writeback_index = index;

	} else {
		DbgPrint("novfs_a_writepage: set_bit AS_EIO\n");
		set_bit(AS_EIO, &mapping->flags);
	}
	if (dlist)
		kfree(dlist);
	if (pages)
		kfree(pages);

	DbgPrint("novfs_a_writepage: retCode=%d\n", retCode);
	return (0);

}

int novfs_a_readpage(struct file *file, struct page *page)
{
	int retCode = 0;
	void *pbuf;
	struct inode *inode = NULL;
	struct dentry *dentry = NULL;
	loff_t offset;
	size_t len;
	struct novfs_schandle session;

	SC_INITIALIZE(session);
	DbgPrint("novfs_a_readpage: File=0x%p Name=%.*s Page=0x%p", file,
		 file->f_dentry->d_name.len, file->f_dentry->d_name.name, page);

	dentry = file->f_dentry;

	if (dentry) {
		DbgPrint(" Dentry=0x%p Name=%.*s", dentry, dentry->d_name.len,
			 dentry->d_name.name);
		if (dentry->d_inode) {
			inode = dentry->d_inode;
		}
	}

	if (inode) {
		DbgPrint(" Inode=0x%p Ino=%d", inode, inode->i_ino);

		if (inode->i_private) {
			session =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
			if (0 == SC_PRESENT(session)) {
				((struct inode_data *) inode->i_private)->Scope =
				    novfs_get_scope(file->f_dentry);
				session =
				    novfs_scope_get_sessionId(((struct inode_data *) inode->
							 i_private)->Scope);
			}
		}
	}

	DbgPrint("\n");

	if (!PageUptodate(page)) {
		struct novfs_data_list dlst[2];

		offset = page->index << PAGE_CACHE_SHIFT;
		len = PAGE_CACHE_SIZE;

		/*
		 * Save the first entry for the reply header.
		 */
		dlst[1].page = page;
		dlst[1].offset = NULL;
		dlst[1].len = PAGE_CACHE_SIZE;
		dlst[1].rwflag = DLWRITE;

		DbgPrint("novfs_a_readpage: calling= novfs_Read_Pages %lld\n",
			 offset);
		retCode =
		    novfs_read_pages(file->private_data, dlst, 2, &len, &offset,
				     session);
		if (len && (len < PAGE_CACHE_SIZE)) {
			pbuf = kmap_atomic(page, KM_USER0);
			memset(&((char *)pbuf)[len], 0, PAGE_CACHE_SIZE - len);
			kunmap_atomic(pbuf, KM_USER0);
		}

		flush_dcache_page(page);
		SetPageUptodate(page);
	}
	unlock_page(page);

	DbgPrint("novfs_a_readpage: retCode=%d\n", retCode);
	return (retCode);

}

int novfs_a_readpages(struct file *file, struct address_space *mapping,
		      struct list_head *page_lst, unsigned nr_pages)
{
	int retCode = 0;
	struct inode *inode = NULL;
	struct dentry *dentry = NULL;
	struct novfs_schandle session;
	loff_t offset;
	size_t len;

	unsigned page_idx;
	struct pagevec lru_pvec;
	pgoff_t next_index;

	char *rbuf, done = 0;
	SC_INITIALIZE(session);

	DbgPrint("novfs_a_readpages: File=0x%p Name=%.*s Pages=%d\n", file,
		 file->f_dentry->d_name.len, file->f_dentry->d_name.name,
		 nr_pages);

	dentry = file->f_dentry;

	if (dentry) {
		DbgPrint(" Dentry=0x%p Name=%.*s\n", dentry, dentry->d_name.len,
			 dentry->d_name.name);
		if (dentry->d_inode) {
			inode = dentry->d_inode;
		}
	}

	if (inode) {
		DbgPrint(" Inode=0x%p Ino=%d\n", inode, inode->i_ino);

		if (inode->i_private) {
			session =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
			if (0 == SC_PRESENT(session)) {
				((struct inode_data *) inode->i_private)->Scope =
				    novfs_get_scope(file->f_dentry);
				session =
				    novfs_scope_get_sessionId(((struct inode_data *) inode->
							 i_private)->Scope);
			}
		}
	}

	rbuf = kmalloc(novfs_max_iosize, GFP_KERNEL);
	if (rbuf) {
		pagevec_init(&lru_pvec, 0);
		for (page_idx = 0; page_idx < nr_pages && !done;) {
			struct page *page, *tpage;

			if (list_empty(page_lst))
				break;

			page = list_entry(page_lst->prev, struct page, lru);

			next_index = page->index;
			offset = (loff_t) page->index << PAGE_CACHE_SHIFT;
			len = 0;

			/*
			 * Count number of contiguous pages.
			 */
			list_for_each_entry_reverse(tpage, page_lst, lru) {
				if ((next_index != tpage->index) ||
				    (len >= novfs_max_iosize - PAGE_SIZE)) {
					break;
				}
				len += PAGE_SIZE;
				next_index++;
			}

			if (len && !done) {
				struct novfs_data_list dllst[2];

				dllst[1].page = NULL;
				dllst[1].offset = rbuf;
				dllst[1].len = len;
				dllst[1].rwflag = DLWRITE;

				DbgPrint
				    ("novfs_a_readpages: calling novfs_Read_Pages %lld\n",
				     offset);
				if (!novfs_read_pages
				    (file->private_data, dllst, 2, &len,
				     &offset, session)) {
					novfs_copy_cache_pages(mapping,
							       page_lst, len,
							       rbuf, &lru_pvec);
					page_idx += len >> PAGE_CACHE_SHIFT;
					if ((int)(len & PAGE_CACHE_MASK) != len) {
						page_idx++;
					}
					if (len == 0) {
						done = 1;
					}
				} else {
					done = 1;
				}
			}
		}

		/*
		 * Free any remaining pages.
		 */
		while (!list_empty(page_lst)) {
			struct page *page =
			    list_entry(page_lst->prev, struct page, lru);

			list_del(&page->lru);
			page_cache_release(page);
		}

		pagevec_lru_add(&lru_pvec);
		kfree(rbuf);
	} else {
		retCode = -ENOMEM;
	}

	DbgPrint("novfs_a_readpages: retCode=%d\n", retCode);
	return (retCode);

}

int novfs_a_prepare_write(struct file *file, struct page *page, unsigned from,
			  unsigned to)
{
	int retVal = 0;
	loff_t offset = (loff_t) page->index << PAGE_CACHE_SHIFT;
	size_t len = PAGE_CACHE_SIZE;
	struct novfs_schandle session;
	struct novfs_data_list dllst[2];
	struct inode *inode = file->f_dentry->d_inode;
	SC_INITIALIZE(session);

	DbgPrint
	    ("novfs_a_prepare_write: File=0x%p Page=0x%p offset=0x%llx From=%u To=%u filesize=%lld\n",
	     file, page, offset, from, to,
	     i_size_read(file->f_dentry->d_inode));
	if (!PageUptodate(page)) {
		/*
		 * Check to see if whole page
		 */
		if ((to == PAGE_CACHE_SIZE) && (from == 0)) {
			SetPageUptodate(page);
		}

		/*
		 * Check to see if we can read page.
		 */
		else if ((file->f_flags & O_ACCMODE) != O_WRONLY) {
			/*
			 * Get session.
			 */
			if (file->f_dentry && file->f_dentry->d_inode) {
				if (file->f_dentry->d_inode->i_private) {
					session =
					    novfs_scope_get_sessionId(((struct inode_data *)
								 inode->
								 i_private)->
								Scope);
					if (0 == SC_PRESENT(session)) {
						((struct inode_data *) inode->
						 i_private)->Scope =
			       novfs_get_scope(file->f_dentry);
						session =
						    novfs_scope_get_sessionId(((struct inode_data *) inode->i_private)->Scope);
					}
				}
			}

			page_cache_get(page);

			len = i_size_read(inode) - offset;
			if (len > PAGE_CACHE_SIZE) {
				len = PAGE_CACHE_SIZE;
			}

			if (len) {
				/*
				 * Read page from server.
				 */

				dllst[1].page = page;
				dllst[1].offset = 0;
				dllst[1].len = len;
				dllst[1].rwflag = DLWRITE;

				DbgPrint
				    ("novfs_a_prepare_write: calling novfs_Read_Pages %lld\n",
				     offset);
				novfs_read_pages(file->private_data, dllst, 2,
						 &len, &offset, session);

				/*
				 * Zero unnsed page.
				 */
			}

			if (len < PAGE_CACHE_SIZE) {
				char *adr = kmap_atomic(page, KM_USER0);
				memset(adr + len, 0, PAGE_CACHE_SIZE - len);
				kunmap_atomic(adr, KM_USER0);
			}
		} else {
			/*
			 * Zero section of memory that not going to be used.
			 */
			char *adr = kmap_atomic(page, KM_USER0);
			memset(adr, 0, from);
			memset(adr + to, 0, PAGE_CACHE_SIZE - to);
			kunmap_atomic(adr, KM_USER0);

			DbgPrint("novfs_a_prepare_write: memset 0x%p\n", adr);
		}
		flush_dcache_page(page);
		SetPageUptodate(page);
	}
//   DbgPrint("novfs_a_prepare_write: return %d\n", retVal);
	return (retVal);
}

int novfs_a_commit_write(struct file *file, struct page *page, unsigned offset,
			 unsigned to)
{
	int retCode = 0;
	struct inode *inode = page->mapping->host;
	loff_t pos = ((loff_t) page->index << PAGE_CACHE_SHIFT) + to;
	struct novfs_schandle session;
	struct inode_data *id;
	struct novfs_data_list dlst[1];
	size_t len = to - offset;

	SC_INITIALIZE(session);

	DbgPrint
	    ("novfs_a_commit_write: File=0x%p Page=0x%p offset=0x%x To=%u filesize=%lld\n",
	     file, page, offset, to, i_size_read(file->f_dentry->d_inode));
	if (file->f_dentry->d_inode
	    && (id = file->f_dentry->d_inode->i_private)) {
		session = novfs_scope_get_sessionId(id->Scope);
		if (0 == SC_PRESENT(session)) {
			id->Scope = novfs_get_scope(file->f_dentry);
			session = novfs_scope_get_sessionId(id->Scope);
		}

		/*
		 * Setup file handle
		 */
		id->FileHandle = file->private_data;

		if (pos > inode->i_size) {
			i_size_write(inode, pos);
		}

		if (!PageUptodate(page)) {
			pos =
			    ((loff_t) page->index << PAGE_CACHE_SHIFT) + offset;

			if (to < offset) {
				return (retCode);
			}
			dlst[0].page = page;
			dlst[0].offset = (void *)(unsigned long) offset;
			dlst[0].len = len;
			dlst[0].rwflag = DLREAD;

			retCode =
			    novfs_write_pages(id->FileHandle, dlst, 1, len, pos,
					      session);

		} else {
			set_page_dirty(page);
		}
	}

	return (retCode);
}

/*++======================================================================*/
ssize_t novfs_a_direct_IO(int rw, struct kiocb * kiocb,
			  const struct iovec * iov,
			  loff_t offset, unsigned long nr_segs)
/*
 *
 *  Notes:        This is a dummy function so that we can allow a file
 *                to get the direct IO flag set.  novfs_f_read and
 *                novfs_f_write will do the work.  Maybe not the best
 *                way to do but it was the easiest to implement.
 *
 *========================================================================*/
{
	return (-EIO);
}

/*++======================================================================*/
int novfs_i_create(struct inode *dir, struct dentry *dentry, int mode,
		   struct nameidata *nd)
{
	char *path, *buf;
	struct novfs_entry_info info;
	void *handle;
	struct novfs_schandle session;
	int retCode = -EACCES;

	DbgPrint("novfs_i_create: mode=0%o flags=0%o %.*s\n", mode,
		 nd->NDOPENFLAGS, dentry->d_name.len, dentry->d_name.name);

	if (IS_ROOT(dentry) ||	/* Root */
	    IS_ROOT(dentry->d_parent) ||	/* User */
	    IS_ROOT(dentry->d_parent->d_parent) ||	/* Server */
	    IS_ROOT(dentry->d_parent->d_parent->d_parent)) {	/* Volume */
		return (-EACCES);
	}

	if (mode | S_IFREG) {
		if (dir->i_private) {
			session =
			    novfs_scope_get_sessionId(((struct inode_data *) dir->i_private)->
						Scope);
			if (0 == SC_PRESENT(session)) {
				((struct inode_data *) dir->i_private)->Scope =
				    novfs_get_scope(dentry);
				session =
				    novfs_scope_get_sessionId(((struct inode_data *) dir->
							 i_private)->Scope);
			}

			buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
			if (buf) {
				path =
				    novfs_dget_path(dentry, buf,
						    PATH_LENGTH_BUFFER);
				if (path) {
					retCode =
					    novfs_open_file(path,
							    nd->
							    NDOPENFLAGS |
							    O_RDWR, &info,
							    &handle, session);
					if (!retCode && handle) {
						novfs_close_file(handle,
								 session);
						if (!novfs_i_mknod
						    (dir, dentry,
						     mode | S_IFREG, 0)) {
							if (dentry->d_inode) {
								((struct inode_data *)
								 dentry->
								 d_inode->
								 i_private)->
						      Flags |= UPDATE_INODE;
							}
						}
					}
				}
				kfree(buf);
			}
		}
	}
	return (retCode);
}

void update_inode(struct inode *Inode, struct novfs_entry_info *Info)
{
	static char dbuf[128];

	DbgPrint("update_inode: Inode=0x%p I_ino=%d\n", Inode, Inode->i_ino);

	DbgPrint("update_inode: atime=%s\n",
		 ctime_r(&Info->atime.tv_sec, dbuf));
	DbgPrint("update_inode: ctime=%s\n",
		 ctime_r(&Info->ctime.tv_sec, dbuf));
	DbgPrint("update_inode: mtime=%s %d\n",
		 ctime_r(&Info->mtime.tv_sec, dbuf), Info->mtime.tv_nsec);
	DbgPrint("update_inode: size=%lld\n", Info->size);
	DbgPrint("update_inode: mode=0%o\n", Info->mode);

	if (Inode &&
	    ((Inode->i_size != Info->size) ||
	     (Inode->i_mtime.tv_sec != Info->mtime.tv_sec) ||
	     (Inode->i_mtime.tv_nsec != Info->mtime.tv_nsec))) {
		DbgPrint
		    ("update_inode: calling invalidate_remote_inode sz  %d %d\n",
		     Inode->i_size, Info->size);
		DbgPrint
		    ("update_inode: calling invalidate_remote_inode sec %d %d\n",
		     Inode->i_mtime.tv_sec, Info->mtime.tv_sec);
		DbgPrint
		    ("update_inode: calling invalidate_remote_inode ns  %d %d\n",
		     Inode->i_mtime.tv_nsec, Info->mtime.tv_nsec);

		if (Inode && Inode->i_mapping) {
			invalidate_remote_inode(Inode);
		}
	}

	Inode->i_mode = Info->mode;
	Inode->i_size = Info->size;
	Inode->i_atime = Info->atime;
	Inode->i_ctime = Info->ctime;
	Inode->i_mtime = Info->mtime;

	if (Inode->i_size && Inode->i_sb->s_blocksize) {
		Inode->i_blocks =
		    (unsigned long) (Info->size >> (loff_t) Inode->i_blkbits);
		Inode->i_bytes = Info->size & (Inode->i_sb->s_blocksize - 1);

		DbgPrint("update_inode: i_sb->s_blocksize=%d\n",
			 Inode->i_sb->s_blocksize);
		DbgPrint("update_inode: i_blkbits=%d\n", Inode->i_blkbits);
		DbgPrint("update_inode: i_blocks=%d\n", Inode->i_blocks);
		DbgPrint("update_inode: i_bytes=%d\n", Inode->i_bytes);
	}
}

struct dentry *novfs_i_lookup(struct inode *dir, struct dentry *dentry,
			      struct nameidata *nd)
{
	struct dentry *retVal = ERR_PTR(-ENOENT);
	struct dentry *parent;
	struct novfs_entry_info *info = NULL;
	struct inode_data *id;
	struct inode *inode = NULL;
	uid_t uid = current->euid;
	ino_t ino = 0;
	struct qstr name;
	char *buf;

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		char *path;
		path = novfs_dget_path(dentry, buf, PATH_LENGTH_BUFFER);
		if (path) {
			DbgPrint
			    ("novfs_i_lookup: dir 0x%p %d hash %d inode 0x%0p %s\n",
			     dir, dir->i_ino, dentry->d_name.hash,
			     dentry->d_inode, path);
		}
		kfree(buf);
	} else {
		DbgPrint
		    ("novfs_i_lookup: dir 0x%p %d name %.*s hash %d inode 0x%0p\n",
		     dir, dir->i_ino, dentry->d_name.len, dentry->d_name.name,
		     dentry->d_name.hash, dentry->d_inode);
	}

	if ((dentry->d_name.len == 7)
	    && (0 == strncmp(dentry->d_name.name, " !xover", 7))) {
		dentry->d_op = &novfs_dentry_operations;
		igrab(dir);
		d_add(dentry, dir);
		return NULL;
	}
	if ((dentry->d_name.len == 7)
	    && (0 == strncmp(dentry->d_name.name, "z!xover", 7))) {
		dentry->d_op = &novfs_dentry_operations;
		igrab(dir);
		d_add(dentry, dir);
		return NULL;
	}

	if (dir && (id = dir->i_private)) {
		retVal = 0;
		if (IS_ROOT(dentry)) {
			DbgPrint("novfs_i_lookup: Root entry=0x%p\n",
				 novfs_root);
			inode = novfs_root->d_inode;
			return (0);
		} else {
			info =
			    kmalloc(sizeof(struct novfs_entry_info) +
					 PATH_LENGTH_BUFFER, GFP_KERNEL);
			if (info) {
				if (NULL ==
				    (retVal =
				     ERR_PTR(verify_dentry(dentry, 1)))) {
					name.name = dentry->d_name.name;
					name.len = dentry->d_name.len;
					name.hash = novfs_internal_hash(&name);

					if (novfs_lock_inode_cache(dir)) {
						if (!novfs_get_entry
						    (dir, &name, &ino, info)) {
							inode =
							    ilookup(dentry->
								    d_sb, ino);
							if (inode) {
								update_inode
								    (inode,
								     info);
							}
						}
						novfs_unlock_inode_cache(dir);
					}

					if (!inode && ino) {
						uid = novfs_scope_get_uid(id->Scope);
						if (novfs_lock_inode_cache(dir)) {
							inode = novfs_get_inode (dentry->d_sb, info->mode, 0, uid, ino, &name);
							if (inode) {
								if (!novfs_get_entry(dir, &dentry->d_name, &ino, info)) {
									update_inode
									    (inode,
									     info);
								}
							}
							novfs_unlock_inode_cache
							    (dir);
						}
					}
				}
			}
		}
	}

	if (!retVal) {
		dentry->d_op = &novfs_dentry_operations;
		if (inode) {
			parent = dget_parent(dentry);
			novfs_d_add(dentry->d_parent, dentry, inode, 1);
			dput(parent);
		} else {
			d_add(dentry, inode);
		}
	}

	if (info)
		kfree(info);

	DbgPrint
	    ("novfs_i_lookup: inode=0x%p dentry->d_inode=0x%p return=0x%p\n",
	     dir, dentry->d_inode, retVal);

	return (retVal);
}

int novfs_i_unlink(struct inode *dir, struct dentry *dentry)
{
	int retCode = -ENOENT;
	struct inode *inode;
	struct novfs_schandle session;
	char *path, *buf;
	uint64_t t64;

	DbgPrint("novfs_i_unlink: dir=0x%p dir->i_ino=%d %.*s\n", dir,
		 dir->i_ino, dentry->d_name.len, dentry->d_name.name);
	DbgPrint("novfs_i_unlink: IS_ROOT(dentry)=%d\n", IS_ROOT(dentry));
	DbgPrint("novfs_i_unlink: IS_ROOT(dentry->d_parent)=%d\n",
		 IS_ROOT(dentry->d_parent));
	DbgPrint("novfs_i_unlink: IS_ROOT(dentry->d_parent->d_parent)=%d\n",
		 IS_ROOT(dentry->d_parent->d_parent));
	DbgPrint
	    ("novfs_i_unlink: IS_ROOT(dentry->d_parent->d_parent->d_parent)=%d\n",
	     IS_ROOT(dentry->d_parent->d_parent->d_parent));

	if (IS_ROOT(dentry) ||	/* Root */
	    IS_ROOT(dentry->d_parent) ||	/* User */
	    (!IS_ROOT(dentry->d_parent->d_parent) &&	/* Server */
	     IS_ROOT(dentry->d_parent->d_parent->d_parent))) {	/* Volume */
		return (-EACCES);
	}

	inode = dentry->d_inode;
	if (inode) {
		DbgPrint
		    ("novfs_i_unlink: dir=0x%p dir->i_ino=%d inode=0x%p ino=%d\n",
		     dir, dir->i_ino, inode, inode->i_ino);
		if (inode->i_private) {
			session =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
			if (0 == SC_PRESENT(session)) {
				((struct inode_data *) inode->i_private)->Scope =
				    novfs_get_scope(dentry);
				session =
				    novfs_scope_get_sessionId(((struct inode_data *) inode->
							 i_private)->Scope);
			}

			buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
			if (buf) {
				path =
				    novfs_dget_path(dentry, buf,
						    PATH_LENGTH_BUFFER);
				if (path) {
					DbgPrint
					    ("novfs_i_unlink: path %s mode 0%o\n",
					     path, inode->i_mode);
					if (IS_ROOT(dentry->d_parent->d_parent)) {
						retCode = novfs_daemon_logout(&dentry->d_name, &session);
					} else {
						retCode =
						    novfs_delete(path,
								 S_ISDIR(inode->
									 i_mode),
								 session);
					}
					if (!retCode || IS_DEADDIR(inode)) {
						novfs_remove_inode_entry(dir,
									 &dentry->
									 d_name,
									 0);
						dentry->d_time = 0;
						t64 = 0;
						novfs_scope_set_userspace(&t64, &t64,
								    &t64, &t64);
						retCode = 0;
					}
				}
				kfree(buf);
			}
		}
	}

	DbgPrint("novfs_i_unlink: retCode 0x%x\n", retCode);
	return (retCode);
}

int novfs_i_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	char *path, *buf;
	struct novfs_schandle session;
	int retCode = 0;
	struct inode *inode;
	struct novfs_entry_info info;
	uid_t uid;

	DbgPrint("novfs_i_mkdir: dir=0x%p ino=%d dentry=0x%p %.*s mode=0%lo\n",
		 dir, dir->i_ino, dentry, dentry->d_name.len,
		 dentry->d_name.name, mode);

	if (IS_ROOT(dentry) ||	/* Root */
	    IS_ROOT(dentry->d_parent) ||	/* User */
	    IS_ROOT(dentry->d_parent->d_parent) ||	/* Server */
	    IS_ROOT(dentry->d_parent->d_parent->d_parent)) {	/* Volume */
		return (-EACCES);
	}

	mode |= S_IFDIR;
	mode &= (S_IFMT | S_IRWXU);
	if (dir->i_private) {
		session =
		    novfs_scope_get_sessionId(((struct inode_data *) dir->i_private)->Scope);
		if (0 == SC_PRESENT(session)) {
			((struct inode_data *) dir->i_private)->Scope =
			    novfs_get_scope(dentry);
			session =
			    novfs_scope_get_sessionId(((struct inode_data *) dir->i_private)->
						Scope);
		}

		uid = novfs_scope_get_uid(((struct inode_data *) dir->i_private)->Scope);
		buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
		if (buf) {
			path = novfs_dget_path(dentry, buf, PATH_LENGTH_BUFFER);
			if (path) {
				DbgPrint("novfs_i_mkdir: path %s\n", path);
				retCode =
				    novfs_create(path, S_ISDIR(mode), session);
				if (!retCode) {
					retCode =
					    novfs_get_file_info(path, &info,
								session);
					if (!retCode) {
						retCode =
						    novfs_i_mknod(dir, dentry,
								  mode, 0);
						inode = dentry->d_inode;
						if (inode) {
							update_inode(inode,
								     &info);
							((struct inode_data *) inode->
							 i_private)->Flags &=
				       ~UPDATE_INODE;

							dentry->d_time =
							    jiffies +
							    (novfs_update_timeout
							     * HZ);

							novfs_lock_inode_cache
							    (dir);
							if (novfs_update_entry
							    (dir,
							     &dentry->d_name, 0,
							     &info)) {
								novfs_add_inode_entry
								    (dir,
								     &dentry->
								     d_name,
								     inode->
								     i_ino,
								     &info);
							}
							novfs_unlock_inode_cache
							    (dir);
						}

					}
				}
			}
			kfree(buf);
		}
	}

	return (retCode);
}

int novfs_i_rmdir(struct inode *inode, struct dentry *dentry)
{
	return (novfs_i_unlink(inode, dentry));
}

int novfs_i_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode = NULL;
	int retCode = -EACCES;
	uid_t uid;
	struct dentry *parent;

	if (IS_ROOT(dentry) ||	/* Root */
	    IS_ROOT(dentry->d_parent) ||	/* User */
	    IS_ROOT(dentry->d_parent->d_parent) ||	/* Server */
	    IS_ROOT(dentry->d_parent->d_parent->d_parent)) {	/* Volume */
		return (-EACCES);
	}

	if (((struct inode_data *) dir->i_private)) {
		uid = novfs_scope_get_uid(((struct inode_data *) dir->i_private)->Scope);
		if (mode & (S_IFREG | S_IFDIR)) {
			inode =
			    novfs_get_inode(dir->i_sb, mode, dev, uid, 0, &dentry->d_name);
		}
	}
	if (inode) {
		struct novfs_entry_info info;

		dentry->d_op = &novfs_dentry_operations;
		parent = dget_parent(dentry);
		novfs_d_add(parent, dentry, inode, 0);
		memset(&info, 0, sizeof(info));
		info.mode = inode->i_mode;
		novfs_lock_inode_cache(dir);
		novfs_add_inode_entry(dir, &dentry->d_name, inode->i_ino,
				      &info);
		novfs_unlock_inode_cache(dir);

		dput(parent);

		retCode = 0;
	}
	DbgPrint("novfs_i_mknod: return 0x%x\n", retCode);
	return retCode;
}

int novfs_i_rename(struct inode *odir, struct dentry *od, struct inode *ndir,
		   struct dentry *nd)
{
	int retCode = -ENOTEMPTY;
	char *newpath, *newbuf, *newcon;
	char *oldpath, *oldbuf, *oldcon;
	struct qstr newname, oldname;
	struct novfs_entry_info *info = NULL;
	int oldlen, newlen;
	struct novfs_schandle session;
	ino_t ino;

	if (IS_ROOT(od) ||	/* Root */
	    IS_ROOT(od->d_parent) ||	/* User */
	    IS_ROOT(od->d_parent->d_parent) ||	/* Server */
	    IS_ROOT(od->d_parent->d_parent->d_parent)) {	/* Volume */
		return (-EACCES);
	}

	DbgPrint("novfs_i_rename: odir=0x%p ino=%d ndir=0x%p ino=%d\n", odir,
		 odir->i_ino, ndir, ndir->i_ino);

	oldbuf = kmalloc(PATH_LENGTH_BUFFER * 2, GFP_KERNEL);
	newbuf = oldbuf + PATH_LENGTH_BUFFER;
	if (oldbuf && newbuf) {
		oldpath = novfs_dget_path(od, oldbuf, PATH_LENGTH_BUFFER);
		newpath = novfs_dget_path(nd, newbuf, PATH_LENGTH_BUFFER);
		if (oldpath && newpath) {
			oldlen = PATH_LENGTH_BUFFER - (int)(oldpath - oldbuf);
			newlen = PATH_LENGTH_BUFFER - (int)(newpath - newbuf);

			DbgPrint
			    ("novfs_i_rename: od=0x%p od->inode=0x%p od->inode->i_ino=%d %s\n",
			     od, od->d_inode, od->d_inode->i_ino, oldpath);
			if (nd->d_inode) {
				DbgPrint
				    ("novfs_i_rename: nd=0x%p nd->inode=0x%p nd->inode->i_ino=%d %s\n",
				     nd, nd->d_inode, nd->d_inode->i_ino,
				     newpath);
			} else {
				DbgPrint
				    ("novfs_i_rename: nd=0x%p nd->inode=0x%p %s\n",
				     nd, nd->d_inode, newpath);
			}

			/*
			 * Check to see if two different servers or different volumes
			 */
			newcon = strchr(newpath + 1, '\\');
			oldcon = strchr(oldpath + 1, '\\');
			DbgPrint("novfs_i_rename: newcon=0x%p newpath=0x%p\n",
				 newcon, newpath);
			DbgPrint("novfs_i_rename: oldcon=0x%p oldpath=0x%p\n",
				 oldcon, oldpath);
			retCode = -EXDEV;
			if (newcon && oldcon
			    && ((int)(newcon - newpath) ==
				(int)(oldcon - oldpath))) {
				newcon = strchr(newcon + 1, '\\');
				oldcon = strchr(oldcon + 1, '\\');
				DbgPrint("novfs_i_rename2: newcon=0x%p newpath=0x%p\n", newcon, newpath);
				DbgPrint("novfs_i_rename2: oldcon=0x%p oldpath=0x%p\n", oldcon, oldpath);
				if (newcon && oldcon &&
				    ((int)(newcon - newpath) == (int)(oldcon - oldpath))) {
					newname.name = newpath;
					newname.len = (int)(newcon - newpath);
					newname.hash = 0;

					oldname.name = oldpath;
					oldname.len = (int)(oldcon - oldpath);
					oldname.hash = 0;
					if (!novfs_d_strcmp(&newname, &oldname)) {

						if (od->d_inode
						    && od->d_inode->i_private) {

							if (nd->d_inode
							    && nd->d_inode->
							    i_private) {
								session =
								    novfs_scope_get_sessionId
								    (((struct inode_data *) ndir->i_private)->Scope);
								if (0 ==
								    SC_PRESENT
								    (session)) {
									((struct inode_data *) ndir->i_private)->Scope = novfs_get_scope(nd);
									session
									    =
									    novfs_scope_get_sessionId
									    (((struct inode_data *) ndir->i_private)->Scope);
								}

								retCode =
								    novfs_delete
								    (newpath,
								     S_ISDIR
								     (nd->
								      d_inode->
								      i_mode),
								     session);
							}

							session = novfs_scope_get_sessionId(((struct inode_data *) ndir->i_private)->Scope);
							if (0 == SC_PRESENT(session)) {
								((struct inode_data *)ndir->i_private)->Scope = novfs_get_scope(nd);
								session = novfs_scope_get_sessionId(((struct inode_data *) ndir->i_private)->Scope);
							}
							retCode = novfs_rename_file(S_ISDIR(od->d_inode->i_mode), oldpath, oldlen - 1, newpath, newlen - 1, session);

							if (!retCode) {
								info = (struct novfs_entry_info *) oldbuf;
								od->d_time = 0;
								novfs_remove_inode_entry(odir, &od->d_name, 0);
								novfs_remove_inode_entry(ndir, &nd->d_name, 0);
								novfs_get_file_info(newpath, info, session);
								nd->d_time = jiffies + (novfs_update_timeout * HZ);

								if (od->d_inode && od->d_inode->i_ino) {
									ino = od->d_inode-> i_ino;
								} else {
									ino = (ino_t)atomic_inc_return(&novfs_Inode_Number);
								}
								novfs_add_inode_entry(ndir, &nd->d_name, ino, info);
							}
						}
					}
				}
			}
		}
	}

	if (oldbuf)
		kfree(oldbuf);

	DbgPrint("novfs_i_rename: return %d\n", retCode);
	return (retCode);
}


int novfs_i_setattr(struct dentry *dentry, struct iattr *attr)
{
	char *path, *buf;
	struct inode *inode = dentry->d_inode;
	char atime_buf[32];
	char mtime_buf[32];
	char ctime_buf[32];
	unsigned int ia_valid = attr->ia_valid;
	struct novfs_schandle session;
	int retVal = 0;
	struct iattr mattr;

	if (IS_ROOT(dentry) ||	/* Root */
	    IS_ROOT(dentry->d_parent) ||	/* User */
	    IS_ROOT(dentry->d_parent->d_parent) ||	/* Server */
	    IS_ROOT(dentry->d_parent->d_parent->d_parent)) {	/* Volume */
		return (-EACCES);
	}

	if (inode && inode->i_private) {
		session =
		    novfs_scope_get_sessionId(((struct inode_data *) inode->i_private)->
					Scope);
		if (0 == SC_PRESENT(session)) {
			((struct inode_data *) inode->i_private)->Scope =
			    novfs_get_scope(dentry);
			session =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
		}

		buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
		if (buf) {
			path = novfs_dget_path(dentry, buf, PATH_LENGTH_BUFFER);
			if (path) {
				strcpy(atime_buf, "Unspecified");
				strcpy(mtime_buf, "Unspecified");
				strcpy(ctime_buf, "Unspecified");
				if (attr->ia_valid & ATTR_ATIME) {
					ctime_r(&attr->ia_atime.tv_sec,
						atime_buf);
				}
				if (attr->ia_valid & ATTR_MTIME) {
					ctime_r(&attr->ia_mtime.tv_sec,
						mtime_buf);
				}
				if (attr->ia_valid & ATTR_CTIME) {
					ctime_r(&attr->ia_ctime.tv_sec,
						ctime_buf);
				}
				/* Removed for Bug 132374. jlt */
				DbgPrint("novfs_i_setattr: %s\n"
					 "   ia_valid:      0x%x\n"
					 "   ia_mode:       0%o\n"
					 "   ia_uid:        %d\n"
					 "   ia_gid:        %d\n"
					 "   ia_size:       %lld\n"
					 "   ia_atime:      %s\n"
					 "   ia_mtime:      %s\n"
					 "   ia_ctime:      %s\n",
					 path,
					 attr->ia_valid,
					 attr->ia_mode,
					 attr->ia_uid,
					 attr->ia_gid,
					 attr->ia_size,
					 atime_buf, mtime_buf, ctime_buf);

				if ((attr->ia_valid & ATTR_FILE)
				    && (attr->ia_valid & ATTR_SIZE)) {
					memcpy(&mattr, attr, sizeof(mattr));
					mattr.ia_valid &=
					    ~(ATTR_FILE | ATTR_SIZE);
					attr = &mattr;
					ia_valid = attr->ia_valid;
#if 0   // thanks to vfs changes in our tree...
					retVal =
					    novfs_trunc_ex(attr->
								   ia_file->
								   private_data,
								   attr->
								   ia_size,
								   session);
					if (!retVal) {
						inode->i_size = attr->ia_size;
						((struct inode_data *) inode->
						 i_private)->Flags |=
			       UPDATE_INODE;
					}
#endif
				}

				if (ia_valid
				    && !(retVal =
					 novfs_set_attr(path, attr, session))) {
					((struct inode_data *) inode->i_private)->
					    Flags |= UPDATE_INODE;

					if (ia_valid & ATTR_ATIME)
						inode->i_atime = attr->ia_atime;
					if (ia_valid & ATTR_MTIME)
						inode->i_mtime = attr->ia_mtime;
					if (ia_valid & ATTR_CTIME)
						inode->i_ctime = attr->ia_ctime;
					if (ia_valid & ATTR_MODE) {
						inode->i_mode =
						    attr->
						    ia_mode & (S_IFMT |
							       S_IRWXU);
					}
				}
			}
		}
		kfree(buf);
	}
	DbgPrint("novfs_i_setattr: return 0x%x\n", retVal);

	return (retVal);
}

int novfs_i_getattr(struct vfsmount *mnt, struct dentry *dentry,
		    struct kstat *kstat)
{
	int retCode = 0;
	char atime_buf[32];
	char mtime_buf[32];
	char ctime_buf[32];
	struct inode *inode = dentry->d_inode;

	struct novfs_entry_info info;
	char *path, *buf;
	struct novfs_schandle session;
	struct inode_data *id;

	if (!IS_ROOT(dentry) && !IS_ROOT(dentry->d_parent)) {
		SC_INITIALIZE(session);
		id = dentry->d_inode->i_private;

		if (id && (id->Flags & UPDATE_INODE)) {
			session = novfs_scope_get_sessionId(id->Scope);

			if (0 == SC_PRESENT(session)) {
				id->Scope = novfs_get_scope(dentry);
				session = novfs_scope_get_sessionId(id->Scope);
			}

			buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
			if (buf) {
				path =
				    novfs_dget_path(dentry, buf,
						    PATH_LENGTH_BUFFER);
				if (path) {
					retCode =
					    novfs_get_file_info(path, &info,
								session);
					if (!retCode) {
						update_inode(inode, &info);
						id->Flags &= ~UPDATE_INODE;
					}
				}
				kfree(buf);
			}
		}
	}

	kstat->ino = inode->i_ino;
	kstat->dev = inode->i_sb->s_dev;
	kstat->mode = inode->i_mode;
	kstat->nlink = inode->i_nlink;
	kstat->uid = inode->i_uid;
	kstat->gid = inode->i_gid;
	kstat->rdev = inode->i_rdev;
	kstat->size = i_size_read(inode);
	kstat->atime = inode->i_atime;
	kstat->mtime = inode->i_mtime;
	kstat->ctime = inode->i_ctime;
	kstat->blksize = inode->i_sb->s_blocksize;
	kstat->blocks = inode->i_blocks;
	if (inode->i_bytes) {
		kstat->blocks++;
	}
	ctime_r(&kstat->atime.tv_sec, atime_buf);
	ctime_r(&kstat->mtime.tv_sec, mtime_buf);
	ctime_r(&kstat->ctime.tv_sec, ctime_buf);

	DbgPrint("novfs_i_getattr: 0x%x 0x%p <%.*s>\n"
		 "   ino: %d\n"
		 "   dev: 0x%x\n"
		 "   mode: 0%o\n"
		 "   nlink: 0x%x\n"
		 "   uid: 0x%x\n"
		 "   gid: 0x%x\n"
		 "   rdev: 0x%x\n"
		 "   size: 0x%llx\n"
		 "   atime: %s\n"
		 "   mtime: %s\n"
		 "   ctime: %s\n"
		 "   blksize: 0x%x\n"
		 "   blocks: 0x%x\n",
		 retCode, dentry, dentry->d_name.len, dentry->d_name.name,
		 kstat->ino,
		 kstat->dev,
		 kstat->mode,
		 kstat->nlink,
		 kstat->uid,
		 kstat->gid,
		 kstat->rdev,
		 kstat->size,
		 atime_buf,
		 mtime_buf, ctime_buf, kstat->blksize, kstat->blocks);
	return (retCode);
}

int novfs_i_getxattr(struct dentry *dentry, const char *name, void *buffer,
		     size_t buffer_size)
{
	struct inode *inode = dentry->d_inode;
	struct novfs_schandle sessionId;
	char *path, *buf, *bufRead;
	ssize_t dataLen;

	int retxcode = 0;

	SC_INITIALIZE(sessionId);

	DbgPrint("novfs_i_getxattr: Ian\n");	/*%.*s\n", dentry->d_name.len, dentry->d_name.name); */
	DbgPrint
	    ("novfs_i_getxattr: dentry->d_name.len %u, dentry->d_name.name %s\n",
	     dentry->d_name.len, dentry->d_name.name);
	DbgPrint("novfs_i_getxattr: name %s\n", name);
	DbgPrint("novfs_i_getxattr: size %u\n", buffer_size);

	if (inode && inode->i_private) {
		sessionId =
		    novfs_scope_get_sessionId(((struct inode_data *) inode->i_private)->
					Scope);
		DbgPrint("novfs_i_getxattr: SessionId = %u\n", sessionId);
		//if (0 == sessionId)
		if (0 == SC_PRESENT(sessionId)) {
			((struct inode_data *) inode->i_private)->Scope =
			    novfs_get_scope(dentry);
			sessionId =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
			DbgPrint("novfs_i_getxattr: SessionId = %u\n",
				 sessionId);
		}
	}

	dataLen = 0;
	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		path = novfs_dget_path(dentry, buf, PATH_LENGTH_BUFFER);
		if (path) {
			bufRead = kmalloc(XA_BUFFER, GFP_KERNEL);
			if (bufRead) {
				retxcode =
				    novfs_getx_file_info(path, name, bufRead,
							 XA_BUFFER, &dataLen,
							 sessionId);
				DbgPrint
				    ("novfs_i_getxattr: after novfs_GetX_File_Info retxcode = %d\n",
				     retxcode);
				if (!retxcode) {
					novfs_dump(64, bufRead);
					if (buffer_size != 0) {
						if (buffer_size >= dataLen) {
							memcpy(buffer, bufRead,
							       dataLen);
						} else {
							DbgPrint
							    ("novfs_i_getxattr: (!!!) not enough buffer_size. buffer_size = %d, dataLen = %d\n",
							     buffer_size,
							     dataLen);
							retxcode = -ERANGE;
						}
					}

					if (bufRead) {
						kfree(bufRead);
					}
				}
			}
		}
		kfree(buf);
	}

	if (retxcode) {
		dataLen = retxcode;
	} else {
		if ((buffer_size > 0) && (buffer_size < dataLen)) {
			dataLen = -ERANGE;
		}
	}

	return (dataLen);
}

int novfs_i_setxattr(struct dentry *dentry, const char *name, const void *value,
		     size_t value_size, int flags)
{

	struct inode *inode = dentry->d_inode;
	struct novfs_schandle sessionId;
	char *path, *buf;
	unsigned long bytesWritten = 0;
	int retError = 0;
	int retxcode = 0;

	SC_INITIALIZE(sessionId);

	DbgPrint("novfs_i_setxattr: Ian\n");	/*%.*s\n", dentry->d_name.len, dentry->d_name.name); */
	DbgPrint
	    ("novfs_i_setxattr: dentry->d_name.len %u, dentry->d_name.name %s\n",
	     dentry->d_name.len, dentry->d_name.name);
	DbgPrint("novfs_i_setxattr: name %s\n", name);
	DbgPrint("novfs_i_setxattr: value_size %u\n", value_size);
	DbgPrint("novfs_i_setxattr: flags %d\n", flags);

	if (inode && inode->i_private) {
		sessionId =
		    novfs_scope_get_sessionId(((struct inode_data *) inode->i_private)->
					Scope);
		DbgPrint("novfs_i_setxattr: SessionId = %u\n", sessionId);
		//if (0 == sessionId)
		if (0 == SC_PRESENT(sessionId)) {
			((struct inode_data *) inode->i_private)->Scope =
			    novfs_get_scope(dentry);
			sessionId =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
			DbgPrint("novfs_i_setxattr: SessionId = %u\n",
				 sessionId);
		}
	}

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		path = novfs_dget_path(dentry, buf, PATH_LENGTH_BUFFER);
		if (path) {
			retxcode =
			    novfs_setx_file_info(path, name, value, value_size,
						 &bytesWritten, flags,
						 sessionId);
			if (!retxcode) {
				DbgPrint
				    ("novfs_i_setxattr: bytesWritten = %u\n",
				     bytesWritten);
			}
		}
		kfree(buf);
	}

	if (retxcode) {
		retError = retxcode;
	}

	if (bytesWritten < value_size) {
		retError = retxcode;
	}
	return (retError);
}

int novfs_i_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = dentry->d_inode;
	struct novfs_schandle sessionId;
	char *path, *buf, *bufList;
	ssize_t dataLen;
	int retxcode = 0;

	SC_INITIALIZE(sessionId);

	DbgPrint("novfs_i_listxattr: Ian\n");	//%.*s\n", dentry->d_name.len, dentry->d_name.name);
	DbgPrint
	    ("novfs_i_listxattr: dentry->d_name.len %u, dentry->d_name.name %s\n",
	     dentry->d_name.len, dentry->d_name.name);
	DbgPrint("novfs_i_listxattr: size %u\n", buffer_size);

	if (inode && inode->i_private) {
		sessionId =
		    novfs_scope_get_sessionId(((struct inode_data *) inode->i_private)->
					Scope);
		DbgPrint("novfs_i_listxattr: SessionId = %u\n", sessionId);
		//if (0 == sessionId)
		if (0 == SC_PRESENT(sessionId)) {
			((struct inode_data *) inode->i_private)->Scope =
			    novfs_get_scope(dentry);
			sessionId =
			    novfs_scope_get_sessionId(((struct inode_data *) inode->
						 i_private)->Scope);
			DbgPrint("novfs_i_listxattr: SessionId = %u\n",
				 sessionId);
		}
	}

	dataLen = 0;
	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		path = novfs_dget_path(dentry, buf, PATH_LENGTH_BUFFER);
		if (path) {
			bufList = kmalloc(XA_BUFFER, GFP_KERNEL);
			if (bufList) {
				retxcode =
				    novfs_listx_file_info(path, bufList,
							  XA_BUFFER, &dataLen,
							  sessionId);

				novfs_dump(64, bufList);
				if (buffer_size != 0) {
					if (buffer_size >= dataLen) {
						memcpy(buffer, bufList,
						       dataLen);
					} else {
						DbgPrint
						    ("novfs_i_listxattr: (!!!) not enough buffer_size. buffer_size = %d, dataLen = %d\n",
						     buffer_size, dataLen);
						retxcode = -1;
					}
				}

				if (bufList) {
					kfree(bufList);
				}
			}

		}
		kfree(buf);
	}

	if (retxcode) {
		dataLen = -1;
	} else {

		if ((buffer_size > 0) && (buffer_size < dataLen)) {
			dataLen = -ERANGE;
		}
	}
	return (dataLen);
}

int novfs_i_revalidate(struct dentry *dentry)
{

	DbgPrint("novfs_i_revalidate: name %.*s\n", dentry->d_name.len,
		 dentry->d_name.name);

	return (0);
}

void novfs_read_inode(struct inode *inode)
{
	DbgPrint("novfs_read_inode: 0x%p %d\n", inode, inode->i_ino);
}

void novfs_write_inode(struct inode *inode)
{
	DbgPrint("novfs_write_inode: Inode=0x%p Ino=%d\n", inode, inode->i_ino);
}

int novfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;

	DbgPrint
	    ("novfs_notify_change: Dentry=0x%p Name=%.*s Inode=0x%p Ino=%d ia_valid=0x%x\n",
	     dentry, dentry->d_name.len, dentry->d_name.name, inode,
	     inode->i_ino, attr->ia_valid);
	return (0);
}

void novfs_clear_inode(struct inode *inode)
{
	InodeCount--;

	if (inode->i_private) {
		struct inode_data *id = inode->i_private;

		DbgPrint
		    ("novfs_clear_inode: inode=0x%p ino=%d Scope=0x%p Name=%s\n",
		     inode, inode->i_ino, id->Scope, id->Name);

		novfs_free_inode_cache(inode);

		down(&InodeList_lock);
		list_del(&id->IList);
		up(&InodeList_lock);

		kfree(inode->i_private);
		inode->i_private = NULL;

		remove_inode_hash(inode);

	} else {
		DbgPrint("novfs_clear_inode: inode=0x%p ino=%d\n", inode,
			 inode->i_ino);
	}
}

/* Called when /proc/mounts is read */
int novfs_show_options(struct seq_file *s, struct vfsmount *m)
{
	char *buf, *path, *tmp;

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		struct path my_path;
		my_path.mnt = m;
		my_path.dentry = m->mnt_root;
		path = d_path(&my_path, buf, PATH_LENGTH_BUFFER);
		if (path) {
			if (!novfs_current_mnt
			    || (novfs_current_mnt
				&& strcmp(novfs_current_mnt, path))) {
				DbgPrint("novfs_show_options: %.*s %.*s %s\n",
					 m->mnt_root->d_name.len,
					 m->mnt_root->d_name.name,
					 m->mnt_mountpoint->d_name.len,
					 m->mnt_mountpoint->d_name.name, path);
				tmp = kmalloc(PATH_LENGTH_BUFFER -
							 (int)(path - buf),
							 GFP_KERNEL);
				if (tmp) {
					strcpy(tmp, path);
					path = novfs_current_mnt;
					novfs_current_mnt = tmp;
					novfs_daemon_set_mnt_point(novfs_current_mnt);

					if (path) {
						kfree(path);
					}
				}
			}
		}
		kfree(buf);
	}
	return (0);
}

/*   Called when statfs(2) system called. */
int novfs_statfs(struct dentry *de, struct kstatfs *buf)
{
	uint64_t td, fd, te, fe;
	struct super_block *sb = de->d_sb;

	DbgPrint("novfs_statfs:\n");

	td = fd = te = fe = 0;

	novfs_scope_get_userspace(&td, &fd, &te, &fe);

	DbgPrint("td=%llu\n", td);
	DbgPrint("fd=%llu\n", fd);
	DbgPrint("te=%llu\n", te);
	DbgPrint("fe=%llu\n", fd);

	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_namelen = NW_MAX_PATH_LENGTH;
	buf->f_blocks =
	    (sector_t) (td +
			(uint64_t) (sb->s_blocksize -
				    1)) >> (uint64_t) sb->s_blocksize_bits;
	buf->f_bfree = (sector_t) fd >> (uint64_t) sb->s_blocksize_bits;
	buf->f_bavail = (sector_t) buf->f_bfree;
	buf->f_files = (sector_t) te;
	buf->f_ffree = (sector_t) fe;
	buf->f_frsize = sb->s_blocksize;
	if (te > 0xffffffff)
		buf->f_files = 0xffffffff;

	if (fe > 0xffffffff)
		buf->f_ffree = 0xffffffff;

	DbgPrint("f_type:    0x%x\n", buf->f_type);
	DbgPrint("f_bsize:   %u\n", buf->f_bsize);
	DbgPrint("f_namelen: %d\n", buf->f_namelen);
	DbgPrint("f_blocks:  %llu\n", buf->f_blocks);
	DbgPrint("f_bfree:   %llu\n", buf->f_bfree);
	DbgPrint("f_bavail:  %llu\n", buf->f_bavail);
	DbgPrint("f_files:   %llu\n", buf->f_files);
	DbgPrint("f_ffree:   %llu\n", buf->f_ffree);
	DbgPrint("f_frsize:  %u\n", buf->f_frsize);

	return 0;
}

struct inode *novfs_get_inode(struct super_block *sb, int mode, int dev,
			      uid_t Uid, ino_t ino, struct qstr *name)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		InodeCount++;
		inode->i_mode = mode;
		inode->i_uid = Uid;
		inode->i_gid = 0;
		inode->i_sb->s_blocksize = sb->s_blocksize;
		inode->i_blkbits = sb->s_blocksize_bits;
		inode->i_blocks = 0;
		inode->i_rdev = 0;
		inode->i_ino = (ino) ? ino : (ino_t)atomic_inc_return(&novfs_Inode_Number);
		if (novfs_page_cache) {
			inode->i_mapping->a_ops = &novfs_aops;
		} else {
			inode->i_mapping->a_ops = &novfs_nocache_aops;
		}
		inode->i_mapping->backing_dev_info = &novfs_backing_dev_info;
		inode->i_atime.tv_sec = 0;
		inode->i_atime.tv_nsec = 0;
		inode->i_mtime = inode->i_ctime = inode->i_atime;

		DbgPrint("novfs_get_inode: Inode=0x%p I_ino=%d len=%d\n", inode,
			 inode->i_ino, name->len);

		if (NULL !=
		    (inode->i_private =
		     kmalloc(sizeof(struct inode_data) + name->len,
				  GFP_KERNEL))) {
			struct inode_data *id;
			id = inode->i_private;

			DbgPrint("novfs_get_inode: i_private 0x%p\n", id);

			id->Scope = NULL;
			id->Flags = 0;
			id->Inode = inode;

			id->cntDC = 1;

			INIT_LIST_HEAD(&id->DirCache);
			init_MUTEX(&id->DirCacheLock);

			id->FileHandle = 0;
			id->CacheFlag = 0;

			down(&InodeList_lock);

			list_add_tail(&id->IList, &InodeList);
			up(&InodeList_lock);

			id->Name[0] = '\0';

			memcpy(id->Name, name->name, name->len);
			id->Name[name->len] = '\0';

			DbgPrint("novfs_get_inode: name %s\n", id->Name);
		}

		insert_inode_hash(inode);

		switch (mode & S_IFMT) {

		case S_IFREG:
			inode->i_op = &novfs_file_inode_operations;
			inode->i_fop = &novfs_file_operations;
			break;

		case S_IFDIR:
			inode->i_op = &novfs_inode_operations;
			inode->i_fop = &novfs_dir_operations;

			inode->i_sb->s_blocksize = 0;
			inode->i_blkbits = 0;
			break;

		default:
			init_special_inode(inode, mode, dev);
			break;
		}

		DbgPrint("novfs_get_inode: size=%lld\n", inode->i_size);
		DbgPrint("novfs_get_inode: mode=0%o\n", inode->i_mode);
		DbgPrint("novfs_get_inode: i_sb->s_blocksize=%d\n",
			 inode->i_sb->s_blocksize);
		DbgPrint("novfs_get_inode: i_blkbits=%d\n", inode->i_blkbits);
		DbgPrint("novfs_get_inode: i_blocks=%d\n", inode->i_blocks);
		DbgPrint("novfs_get_inode: i_bytes=%d\n", inode->i_bytes);
	}

	DbgPrint("novfs_get_inode: 0x%p %d\n", inode, inode->i_ino);
	return (inode);
}

int novfs_fill_super(struct super_block *SB, void *Data, int Silent)
{
	struct inode *inode;
	struct dentry *server, *tree;
	struct qstr name;
	struct novfs_entry_info info;

	SB->s_blocksize = PAGE_CACHE_SIZE;
	SB->s_blocksize_bits = PAGE_CACHE_SHIFT;
	SB->s_maxbytes = 0xFFFFFFFFFFFFFFFFULL;	/* Max file size */
	SB->s_op = &novfs_ops;
	SB->s_flags |= (MS_NODIRATIME | MS_NODEV | MS_POSIXACL);
	SB->s_magic = NOVFS_MAGIC;

	name.len = 1;
	name.name = "/";

	inode = novfs_get_inode(SB, S_IFDIR | 0777, 0, 0, 0, &name);
	if (!inode) {
		return (-ENOMEM);
	}

	novfs_root = d_alloc_root(inode);

	if (!novfs_root) {
		iput(inode);
		return (-ENOMEM);
	}
	novfs_root->d_time = jiffies + (novfs_update_timeout * HZ);

	inode->i_atime = inode->i_ctime = inode->i_mtime = CURRENT_TIME;

	SB->s_root = novfs_root;

	DbgPrint("novfs_fill_super: root 0x%p\n", novfs_root);

	if (novfs_root) {
		novfs_root->d_op = &novfs_dentry_operations;

		name.name = SERVER_DIRECTORY_NAME;
		name.len = strlen(SERVER_DIRECTORY_NAME);
		name.hash = novfs_internal_hash(&name);

		inode = novfs_get_inode(SB, S_IFDIR | 0777, 0, 0, 0, &name);
		if (inode) {
			info.mode = inode->i_mode;
			info.namelength = 0;
			inode->i_size = info.size = 0;
			inode->i_uid = info.uid = 0;
			inode->i_gid = info.gid = 0;
			inode->i_atime = info.atime =
			    inode->i_ctime = info.ctime =
			    inode->i_mtime = info.mtime = CURRENT_TIME;

			server = d_alloc(novfs_root, &name);
			if (server) {
				server->d_op = &novfs_dentry_operations;
				server->d_time = 0xffffffff;
				d_add(server, inode);
				DbgPrint("novfs_fill_super: d_add %s 0x%p\n",
					 SERVER_DIRECTORY_NAME, server);
				novfs_add_inode_entry(novfs_root->d_inode,
						      &name, inode->i_ino,
						      &info);
			}
		}

		name.name = TREE_DIRECTORY_NAME;
		name.len = strlen(TREE_DIRECTORY_NAME);
		name.hash = novfs_internal_hash(&name);

		inode = novfs_get_inode(SB, S_IFDIR | 0777, 0, 0, 0, &name);
		if (inode) {
			info.mode = inode->i_mode;
			info.namelength = 0;
			inode->i_size = info.size = 0;
			inode->i_uid = info.uid = 0;
			inode->i_gid = info.gid = 0;
			inode->i_atime = info.atime =
			    inode->i_ctime = info.ctime =
			    inode->i_mtime = info.mtime = CURRENT_TIME;
			tree = d_alloc(novfs_root, &name);
			if (tree) {
				tree->d_op = &novfs_dentry_operations;
				tree->d_time = 0xffffffff;

				d_add(tree, inode);
				DbgPrint("novfs_fill_super: d_add %s 0x%p\n",
					 TREE_DIRECTORY_NAME, tree);
				novfs_add_inode_entry(novfs_root->d_inode,
						      &name, inode->i_ino,
						      &info);
			}
		}
	}

	return (0);
}

static int novfs_get_sb(struct file_system_type *Fstype, int Flags,
		 const char *Dev_name, void *Data, struct vfsmount *Mnt)
{
	DbgPrint("novfs_get_sb: Fstype=0x%x Dev_name=%s\n", Fstype, Dev_name);
	return get_sb_nodev(Fstype, Flags, Data, novfs_fill_super, Mnt);
}

static void novfs_kill_sb(struct super_block *super)
{
	kill_litter_super(super);
}

ssize_t novfs_Control_read(struct file *file, char *buf, size_t nbytes,
			   loff_t * ppos)
{
	ssize_t retval = 0;

	DbgPrint("novfs_Control_read: kernel_locked 0x%x\n", kernel_locked());

	return retval;
}

ssize_t novfs_Control_write(struct file * file, const char *buf, size_t nbytes,
			    loff_t * ppos)
{
	ssize_t retval = 0;

	DbgPrint("novfs_Control_write: kernel_locked 0x%x\n", kernel_locked());
	if (buf && nbytes) {
	}

	return (retval);
}

int novfs_Control_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	DbgPrint("novfs_Control_ioctl: kernel_locked 0x%x\n", kernel_locked());

	return (retval);
}

static struct file_system_type novfs_fs_type = {
	.name = "novfs",
	.get_sb = novfs_get_sb,
	.kill_sb = novfs_kill_sb,
	.owner = THIS_MODULE,
};

int __init init_novfs(void)
{
	int retCode;

	lastDir[0] = 0;
	lastTime = get_nanosecond_time();

	inHAX = 0;
	inHAXTime = get_nanosecond_time();

	retCode = novfs_proc_init();

	novfs_profile_init();

	if (!retCode) {
		DbgPrint("init_novfs: %s %s %s\n", __DATE__, __TIME__,
			 NOVFS_VERSION_STRING);
		novfs_daemon_queue_init();
		novfs_scope_init();
		retCode = register_filesystem(&novfs_fs_type);
		if (retCode) {
			novfs_proc_exit();
			novfs_daemon_queue_exit();
			novfs_scope_exit();
		}
	}
	return (retCode);
}

void __exit exit_novfs(void)
{
	printk(KERN_INFO "exit_novfs\n");

	novfs_scope_exit();
	printk(KERN_INFO "exit_novfs after Scope_Uninit\n");

	novfs_daemon_queue_exit();
	printk(KERN_INFO "exit_novfs after Uninit_Daemon_Queue\n");

	novfs_profile_exit();
	printk(KERN_INFO "exit_novfs after profile_exit\n");

	novfs_proc_exit();
	printk(KERN_INFO "exit_novfs Uninit_Procfs_Interface\n");

	unregister_filesystem(&novfs_fs_type);
	printk(KERN_INFO "exit_novfs: Exit\n");

	if (novfs_current_mnt) {
		kfree(novfs_current_mnt);
		novfs_current_mnt = NULL;
	}
}

int novfs_lock_inode_cache(struct inode *i)
{
	struct inode_data *id;
	int retVal = 0;

	DbgPrint("novfs_lock_inode_cache: 0x%p\n", i);
	if (i && (id = i->i_private) && id->DirCache.next) {
		down(&id->DirCacheLock);
		retVal = 1;
	}
	DbgPrint("novfs_lock_inode_cache: return %d\n", retVal);
	return (retVal);
}

void novfs_unlock_inode_cache(struct inode *i)
{
	struct inode_data *id;

	if (i && (id = i->i_private) && id->DirCache.next) {
		up(&id->DirCacheLock);
	}
}

int novfs_enumerate_inode_cache(struct inode *i, struct list_head **iteration,
				ino_t * ino, struct novfs_entry_info *info)
/*
 *  Arguments:   struct inode *i - pointer to directory inode
 *
 *  Returns:     0 - item found
 *              -1 - done
 *
 *  Abstract:    Unlocks inode cache.
 *
 *  Notes:       DirCacheLock should be held before calling this routine.
 *========================================================================*/
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	struct list_head *l = NULL;
	int retVal = -1;

	if (i && (id = i->i_private) && id->DirCache.next) {
		if ((NULL == iteration) || (NULL == *iteration)) {
			l = id->DirCache.next;
		} else {
			l = *iteration;
		}

		if (l == &id->DirCache) {
			l = NULL;
		} else {
			dc = list_entry(l, struct novfs_dir_cache, list);

			*ino = dc->ino;
			info->type = 0;
			info->mode = dc->mode;
			info->size = dc->size;
			info->atime = dc->atime;
			info->mtime = dc->mtime;
			info->ctime = dc->ctime;
			info->namelength = dc->nameLen;
			memcpy(info->name, dc->name, dc->nameLen);
			info->name[dc->nameLen] = '\0';
			retVal = 0;

			l = l->next;
		}
	}
	*iteration = l;
	return (retVal);
}

/* DirCacheLock should be held before calling this routine. */
int novfs_get_entry(struct inode *i, struct qstr *name, ino_t * ino,
		    struct novfs_entry_info *info)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	int retVal = -1;
	char *n = "<NULL>";
	int nl = 6;

	if (i && (id = i->i_private) && id->DirCache.next) {
		if (name && name->len) {
			n = (char *)name->name;
			nl = name->len;
		}

		dc = novfs_lookup_inode_cache(i, name, *ino);
		if (dc) {
			dc->flags |= ENTRY_VALID;
			retVal = 0;
			*ino = dc->ino;
			info->type = 0;
			info->mode = dc->mode;
			info->size = dc->size;
			info->atime = dc->atime;
			info->mtime = dc->mtime;
			info->ctime = dc->ctime;
			info->namelength = dc->nameLen;
			memcpy(info->name, dc->name, dc->nameLen);
			info->name[dc->nameLen] = '\0';
			retVal = 0;
		}

		DbgPrint("novfs_get_entry:\n"
			 "   inode: 0x%p\n"
			 "   name:  %.*s\n" "   ino:   %d\n", i, nl, n, *ino);
	}
	DbgPrint("novfs_get_entry: return %d\n", retVal);
	return (retVal);
}

 /*DirCacheLock should be held before calling this routine. */
int novfs_get_entry_by_pos(struct inode *i, loff_t pos, ino_t * ino,
			   struct novfs_entry_info *info)
{
	int retVal = -1;
	loff_t count = 0;
	loff_t i_pos = pos - 2;
	struct list_head *inter = NULL;
	while (!novfs_enumerate_inode_cache(i, &inter, ino, info)) {
		DbgPrint
		    ("novfs_dir_readdir : novfs_get_entry_by_pos : info->name = %s\n",
		     info->name);
		if (count == i_pos) {
			retVal = 0;
			break;
		} else
			count++;
	}

	return retVal;
}

/* DirCacheLock should be held before calling this routine. */
int novfs_get_entry_time(struct inode *i, struct qstr *name, ino_t * ino,
			 struct novfs_entry_info *info, u64 * EntryTime)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	int retVal = -1;
	char *n = "<NULL>";
	int nl = 6;

	if (i && (id = i->i_private) && id->DirCache.next) {
		if (name && name->len) {
			n = (char *)name->name;
			nl = name->len;
		}
		DbgPrint("novfs_get_entry_time:\n"
			 "   inode: 0x%p\n"
			 "   name:  %.*s\n" "   ino:   %d\n", i, nl, n, *ino);

		dc = novfs_lookup_inode_cache(i, name, *ino);
		if (dc) {
			retVal = 0;
			*ino = dc->ino;
			info->type = 0;
			info->mode = dc->mode;
			info->size = dc->size;
			info->atime = dc->atime;
			info->mtime = dc->mtime;
			info->ctime = dc->ctime;
			info->namelength = dc->nameLen;
			memcpy(info->name, dc->name, dc->nameLen);
			info->name[dc->nameLen] = '\0';
			if (EntryTime) {
				*EntryTime = dc->jiffies;
			}
			retVal = 0;
		}
	}
	DbgPrint("novfs_get_entry_time: return %d\n", retVal);
	return (retVal);
}

/*
 *  Abstract:    This routine will return the first entry on the list
 *               and then remove it.
 *
 *  Notes:       DirCacheLock should be held before calling this routine.
 *
 */
int novfs_get_remove_entry(struct inode *i, ino_t * ino, struct novfs_entry_info *info)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	struct list_head *l = NULL;
	int retVal = -1;

	if (i && (id = i->i_private) && id->DirCache.next) {
		l = id->DirCache.next;

		if (l != &id->DirCache) {
			dc = list_entry(l, struct novfs_dir_cache, list);

			*ino = dc->ino;
			info->type = 0;
			info->mode = dc->mode;
			info->size = dc->size;
			info->atime = dc->atime;
			info->mtime = dc->mtime;
			info->ctime = dc->ctime;
			info->namelength = dc->nameLen;
			memcpy(info->name, dc->name, dc->nameLen);
			info->name[dc->nameLen] = '\0';
			retVal = 0;

			list_del(&dc->list);
			kfree(dc);
			DCCount--;

			id->cntDC--;
		}
	}
	return (retVal);
}

/*
 *  Abstract:    Marks all entries in the directory cache as invalid.
 *
 *  Notes:       DirCacheLock should be held before calling this routine.
 *
 *========================================================================*/
void novfs_invalidate_inode_cache(struct inode *i)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	struct list_head *l;

	if (i && (id = i->i_private) && id->DirCache.next) {
		list_for_each(l, &id->DirCache) {
			dc = list_entry(l, struct novfs_dir_cache, list);
			dc->flags &= ~ENTRY_VALID;
		}
	}
}

/*++======================================================================*/
struct novfs_dir_cache *novfs_lookup_inode_cache(struct inode *i, struct qstr *name,
				    ino_t ino)
/*
 *  Returns:     struct novfs_dir_cache entry if match
 *               NULL - if there is no match.
 *
 *  Abstract:    Checks a inode directory to see if there are any enties
 *               matching name or ino.  If name is specified then ino is
 *               not used.  ino is use if name is not specified.
 *
 *  Notes:       DirCacheLock should be held before calling this routine.
 *
 *========================================================================*/
{
	struct inode_data *id;
	struct novfs_dir_cache *dc, *retVal = NULL;
	struct list_head *l;
	char *n = "<NULL>";
	int nl = 6;
	int hash = 0;

	if (i && (id = i->i_private) && id->DirCache.next) {
		if (name && name->name) {
			nl = name->len;
			n = (char *)name->name;
			hash = name->hash;
		}
		DbgPrint("novfs_lookup_inode_cache:\n"
			 "   inode: 0x%p\n"
			 "   name:  %.*s\n"
			 "   hash:  0x%x\n"
			 "   len:   %d\n"
			 "   ino:   %d\n", i, nl, n, hash, nl, ino);

		list_for_each(l, &id->DirCache) {
			dc = list_entry(l, struct novfs_dir_cache, list);
			if (name) {

/*         DbgPrint("novfs_lookup_inode_cache: 0x%p\n" \
                  "   ino:   %d\n" \
                  "   hash:  0x%x\n" \
                  "   len:   %d\n" \
                  "   name:  %.*s\n",
            dc, dc->ino, dc->hash, dc->nameLen, dc->nameLen, dc->name);
*/
				if ((name->hash == dc->hash) &&
				    (name->len == dc->nameLen) &&
				    (0 ==
				     memcmp(name->name, dc->name, name->len))) {
					retVal = dc;
					break;
				}
			} else {
				if (ino == dc->ino) {
					retVal = dc;
					break;
				}
			}
		}
	}

	DbgPrint("novfs_lookup_inode_cache: return 0x%p\n", retVal);
	return (retVal);
}

/*
 * Checks a inode directory to see if there are any enties matching name
 * or ino.  If entry is found the valid bit is set.
 *
 * DirCacheLock should be held before calling this routine.
 */
int novfs_lookup_validate(struct inode *i, struct qstr *name, ino_t ino)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	int retVal = -1;
	char *n = "<NULL>";
	int nl = 6;

	if (i && (id = i->i_private) && id->DirCache.next) {
		if (name && name->len) {
			n = (char *)name->name;
			nl = name->len;
		}
		DbgPrint("novfs_update_entry:\n"
			 "   inode: 0x%p\n"
			 "   name:  %.*s\n" "   ino:   %d\n", i, nl, n, ino);

		dc = novfs_lookup_inode_cache(i, name, ino);
		if (dc) {
			dc->flags |= ENTRY_VALID;
			retVal = 0;
		}
	}
	return (retVal);
}

/*
 * Added entry to directory cache.
 *
 * DirCacheLock should be held before calling this routine.
 */
int novfs_add_inode_entry(struct inode *i,
			  struct qstr *name, ino_t ino, struct novfs_entry_info *info)
{
	struct inode_data *id;
	struct novfs_dir_cache *new;
	int retVal = -ENOMEM;
	struct novfs_dir_cache *todel;
	struct list_head *todeltmp;

	//SClark
	DbgPrint("novfs_add_inode_entry:\n" "   i: %u\n", i);
	if ((id = i->i_private)) {
		DbgPrint("   i->i_private: %p\n", id);
		if (id->DirCache.next)
			DbgPrint("   id->DirCache.next: %p\n",
				 id->DirCache.next);
	}
	//SClark

	if (i && (id = i->i_private) && id->DirCache.next) {
		new = kmalloc(sizeof(struct novfs_dir_cache) + name->len, GFP_KERNEL);
		if (new) {
			id->cntDC++;

			DCCount++;
			DbgPrint("novfs_add_inode_entry:\n"
				 "   inode: 0x%p\n"
				 "   id:    0x%p\n"
				 "   DC:    0x%p\n"
				 "   new:   0x%p\n"
				 "   name:  %.*s\n"
				 "   ino:   %d\n"
				 "   size:  %lld\n"
				 "   mode:  0x%x\n",
				 i, id, &id->DirCache, new, name->len,
				 name->name, ino, info->size, info->mode);

			retVal = 0;
			new->flags = ENTRY_VALID;
			new->jiffies = get_jiffies_64();
			new->size = info->size;
			new->mode = info->mode;
			new->atime = info->atime;
			new->mtime = info->mtime;
			new->ctime = info->ctime;
			new->ino = ino;
			new->hash = name->hash;
			new->nameLen = name->len;
			memcpy(new->name, name->name, name->len);
			new->name[new->nameLen] = '\0';
			list_add(&new->list, &id->DirCache);

			if (id->cntDC > 20) {
				todeltmp = id->DirCache.prev;
				todel = list_entry(todeltmp, struct novfs_dir_cache, list);

				list_del(&todel->list);

				kfree(todel);

				DCCount--;
				id->cntDC--;
			}

		}
	}
	return (retVal);
}

/*
 *  DirCacheLock should be held before calling this routine.
 */
int novfs_update_entry(struct inode *i, struct qstr *name, ino_t ino,
		       struct novfs_entry_info *info)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	int retVal = -1;
	char *n = "<NULL>";
	int nl = 6;
	char atime_buf[32];
	char mtime_buf[32];
	char ctime_buf[32];

	if (i && (id = i->i_private) && id->DirCache.next) {

		if (name && name->len) {
			n = (char *)name->name;
			nl = name->len;
		}
		ctime_r(&info->atime.tv_sec, atime_buf);
		ctime_r(&info->mtime.tv_sec, mtime_buf);
		ctime_r(&info->ctime.tv_sec, ctime_buf);
		DbgPrint("novfs_update_entry:\n"
			 "   inode: 0x%p\n"
			 "   name:  %.*s\n"
			 "   ino:   %d\n"
			 "   size:  %lld\n"
			 "   atime: %s\n"
			 "   mtime: %s\n"
			 "   ctime: %s\n",
			 i, nl, n, ino, info->size, atime_buf, mtime_buf,
			 ctime_buf);

		dc = novfs_lookup_inode_cache(i, name, ino);
		if (dc) {
			retVal = 0;
			dc->flags = ENTRY_VALID;
			dc->jiffies = get_jiffies_64();
			dc->size = info->size;
			dc->mode = info->mode;
			dc->atime = info->atime;
			dc->mtime = info->mtime;
			dc->ctime = info->ctime;

			ctime_r(&dc->atime.tv_sec, atime_buf);
			ctime_r(&dc->mtime.tv_sec, mtime_buf);
			ctime_r(&dc->ctime.tv_sec, ctime_buf);
			DbgPrint("novfs_update_entry entry: 0x%p\n"
				 "   flags:   0x%x\n"
				 "   jiffies: %lld\n"
				 "   ino:     %d\n"
				 "   size:    %lld\n"
				 "   mode:    0%o\n"
				 "   atime:   %s\n"
				 "   mtime:   %s %d\n"
				 "   ctime:   %s\n"
				 "   hash:    0x%x\n"
				 "   nameLen: %d\n"
				 "   name:    %s\n",
				 dc, dc->flags, dc->jiffies, dc->ino, dc->size,
				 dc->mode, atime_buf, mtime_buf,
				 dc->mtime.tv_nsec, ctime_buf, dc->hash,
				 dc->nameLen, dc->name);
		}
	}
	DbgPrint("novfs_update_entry: return %d\n", retVal);
	return (retVal);
}

/*
 *  Removes entry from directory cache.  You can specify a name
 *  or an inode number.
 *
 *  DirCacheLock should be held before calling this routine.
 */
void novfs_remove_inode_entry(struct inode *i, struct qstr *name, ino_t ino)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	char *n = "<NULL>";
	int nl = 6;

	if (i && (id = i->i_private) && id->DirCache.next) {
		dc = novfs_lookup_inode_cache(i, name, ino);
		if (dc) {
			if (name && name->name) {
				nl = name->len;
				n = (char *)name->name;
			}
			DbgPrint("novfs_remove_inode_entry:\n"
				 "   inode: 0x%p\n"
				 "   id:    0x%p\n"
				 "   DC:    0x%p\n"
				 "   name:  %.*s\n"
				 "   ino:   %d\n"
				 "   entry: 0x%p\n"
				 "      name: %.*s\n"
				 "      ino:  %d\n"
				 "      next: 0x%p\n"
				 "      prev: 0x%p\n",
				 i, id, &id->DirCache, nl, n, ino, dc,
				 dc->nameLen, dc->name, dc->ino, dc->list.next,
				 dc->list.prev);
			list_del(&dc->list);
			kfree(dc);
			DCCount--;

			id->cntDC--;
		}
	}
}

/*
 * Frees all invalid entries in the directory cache.
 *
 * DirCacheLock should be held before calling this routine.
 */
void novfs_free_invalid_entries(struct inode *i)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	struct list_head *l;

	if (i && (id = i->i_private) && id->DirCache.next) {
		list_for_each(l, &id->DirCache) {
			dc = list_entry(l, struct novfs_dir_cache, list);
			if (0 == (dc->flags & ENTRY_VALID)) {
				DbgPrint("novfs_free_invalid_entries:\n"
					 "   inode: 0x%p\n"
					 "   id:    0x%p\n"
					 "   entry:    0x%p\n"
					 "   name:  %.*s\n"
					 "   ino:   %d\n",
					 i, id, dc, dc->nameLen, dc->name,
					 dc->ino);
				l = l->prev;
				list_del(&dc->list);
				kfree(dc);
				DCCount--;

				id->cntDC--;
			}
		}
	}
}

/*
 *  Frees all entries in the inode cache.
 *
 *  DirCacheLock should be held before calling this routine.
 */
void novfs_free_inode_cache(struct inode *i)
{
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	struct list_head *l;

	if (i && (id = i->i_private) && id->DirCache.next) {
		list_for_each(l, &id->DirCache) {
			dc = list_entry(l, struct novfs_dir_cache, list);
			l = l->prev;
			list_del(&dc->list);
			kfree(dc);
			DCCount--;

			id->cntDC--;
		}
	}
}

void novfs_dump_inode(void *pf)
{
	struct inode *inode;
	void (*pfunc) (char *Fmt, ...) = pf;
	struct inode_data *id;
	struct novfs_dir_cache *dc;
	struct list_head *il, *l;
	char atime_buf[32];
	char mtime_buf[32];
	char ctime_buf[32];
	unsigned long icnt = 0, dccnt = 0;

	down(&InodeList_lock);
	list_for_each(il, &InodeList) {
		id = list_entry(il, struct inode_data, IList);
		inode = id->Inode;
		if (inode) {
			icnt++;

			pfunc("Inode=0x%p I_ino=%d\n", inode, inode->i_ino);

			pfunc("   atime=%s\n",
			      ctime_r(&inode->i_atime.tv_sec, atime_buf));
			pfunc("   ctime=%s\n",
			      ctime_r(&inode->i_mtime.tv_sec, atime_buf));
			pfunc("   mtime=%s\n",
			      ctime_r(&inode->i_ctime.tv_sec, atime_buf));
			pfunc("   size=%lld\n", inode->i_size);
			pfunc("   mode=0%o\n", inode->i_mode);
			pfunc("   count=0%o\n", atomic_read(&inode->i_count));
		}

		pfunc("   nofs_inode_data: 0x%p Name=%s Scope=0x%p\n", id, id->Name,
		      id->Scope);

		if (id->DirCache.next) {
			list_for_each(l, &id->DirCache) {
				dccnt++;
				dc = list_entry(l, struct novfs_dir_cache,
						list);
				ctime_r(&dc->atime.tv_sec, atime_buf);
				ctime_r(&dc->mtime.tv_sec, mtime_buf);
				ctime_r(&dc->ctime.tv_sec, ctime_buf);

				pfunc("   Cache Entry: 0x%p\n"
				      "      flags:   0x%x\n"
				      "      jiffies: %llu\n"
				      "      ino:     %u\n"
				      "      size:    %llu\n"
				      "      mode:    0%o\n"
				      "      atime:   %s\n"
				      "      mtime:   %s\n"
				      "      ctime:   %s\n"
				      "      hash:    0x%x\n"
				      "      len:     %d\n"
				      "      name:    %s\n",
				      dc, dc->flags, dc->jiffies,
				      dc->ino, dc->size, dc->mode,
				      atime_buf, mtime_buf, ctime_buf,
				      dc->hash, dc->nameLen, dc->name);
			}
		}
	}
	up(&InodeList_lock);

	pfunc("Inodes: %d(%d) DirCache: %d(%d)\n", InodeCount, icnt, DCCount,
	      dccnt);

}

module_init(init_novfs);
module_exit(exit_novfs);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Novell Inc.");
MODULE_DESCRIPTION("Novell NetWare Client for Linux");
MODULE_VERSION(NOVFS_VERSION_STRING);

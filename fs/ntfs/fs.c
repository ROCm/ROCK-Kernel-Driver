/*
 * fs.c - NTFS driver for Linux 2.4.x
 *
 * Legato Systems, Inc. (http://www.legato.com) have sponsored Anton
 * Altaparmakov to develop NTFS on Linux since June 2001.
 *
 * Copyright (C) 1995-1997, 1999 Martin von Löwis
 * Copyright (C) 1996 Richard Russon
 * Copyright (C) 1996-1997 Régis Duchesne
 * Copyright (C) 2000-2001, Anton Altaparmakov (AIA)
 */

#include <linux/config.h>

#include "ntfstypes.h"
#include "struct.h"
#include "util.h"
#include "inode.h"
#include "super.h"
#include "dir.h"
#include "support.h"
#include "macros.h"
#include "sysctl.h"
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/locks.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <asm/page.h>

#ifndef NLS_MAX_CHARSET_SIZE
#include <linux/nls.h>
#endif

/* Forward declarations. */
static struct inode_operations ntfs_dir_inode_operations;
static struct file_operations ntfs_dir_operations;

#define ITEM_SIZE 2040

/* Io functions to user space. */
static void ntfs_putuser(ntfs_io* dest, void *src, ntfs_size_t len)
{
	copy_to_user(dest->param, src, len);
	dest->param += len;
}

#ifdef CONFIG_NTFS_RW
struct ntfs_getuser_update_vm_s {
	const char *user;
	struct inode *ino;
	loff_t off;
};

static void ntfs_getuser_update_vm(void *dest, ntfs_io *src, ntfs_size_t len)
{
	struct ntfs_getuser_update_vm_s *p = src->param;
	
	copy_from_user(dest, p->user, len);
	p->user += len;
	p->off += len;
}
#endif

/* loff_t is 64 bit signed, so is cool. */
static ssize_t ntfs_read(struct file *filp, char *buf, size_t count,loff_t *off)
{
	int error;
	ntfs_io io;
	ntfs_inode *ino = NTFS_LINO2NINO(filp->f_dentry->d_inode);

	/* Inode is not properly initialized. */
	if (!ino)
		return -EINVAL;
	ntfs_debug(DEBUG_OTHER, "ntfs_read %x, %Lx, %x ->",
		   (unsigned)ino->i_number, (unsigned long long)*off,
		   (unsigned)count);
	/* Inode has no unnamed data attribute. */
	if(!ntfs_find_attr(ino, ino->vol->at_data, NULL)) {
		ntfs_debug(DEBUG_OTHER, "ntfs_read: $DATA not found!\n");
		return -EINVAL;
	}
	/* Read the data. */
	io.fn_put = ntfs_putuser;
	io.fn_get = 0;
	io.param = buf;
	io.size = count;
	error = ntfs_read_attr(ino, ino->vol->at_data, NULL, *off, &io);
	if (error && !io.size) {
		ntfs_debug(DEBUG_OTHER, "ntfs_read: read_attr failed with "
				"error %i, io size %u.\n", error, io.size);
		return error;
	}
	*off += io.size;
	ntfs_debug(DEBUG_OTHER, "ntfs_read: finished. read %u bytes.\n",
								io.size);
	return io.size;
}

#ifdef CONFIG_NTFS_RW
static ssize_t ntfs_write(struct file *filp, const char* buf, size_t count,
			  loff_t *pos)
{
	int ret;
	ntfs_io io;
	struct inode *inode = filp->f_dentry->d_inode;
	ntfs_inode *ino = NTFS_LINO2NINO(inode);
	struct ntfs_getuser_update_vm_s param;

	if (!ino)
		return -EINVAL;
	ntfs_debug(DEBUG_LINUX, "ntfs_write %x, %x, %x ->\n",
		   (unsigned)ino->i_number, (unsigned)*pos, (unsigned)count);
	/* Allows to lock fs ro at any time. */
	if (inode->i_sb->s_flags & MS_RDONLY)
		return -ENOSPC;
	if (!ntfs_find_attr(ino, ino->vol->at_data, NULL))
		return -EINVAL;
	/* Evaluating O_APPEND is the file system's job... */
	if (filp->f_flags & O_APPEND)
		*pos = inode->i_size;
	param.user = buf;
	param.ino = inode;
	param.off = *pos;
	io.fn_put = 0;
	io.fn_get = ntfs_getuser_update_vm;
	io.param = &param;
	io.size = count;
	ret = ntfs_write_attr(ino, ino->vol->at_data, NULL, *pos, &io);
	ntfs_debug(DEBUG_LINUX, "write -> %x\n", ret);
	if (ret < 0)
		return -EINVAL;
	*pos += io.size;
	if (*pos > inode->i_size)
		inode->i_size = *pos;
	mark_inode_dirty(filp->f_dentry->d_inode);
	return io.size;
}
#endif

struct ntfs_filldir{
	struct inode *dir;
	filldir_t filldir;
	unsigned int type;
	ntfs_u32 ph,pl;
	void *dirent;
	char *name;
	int namelen;
};
	
static int ntfs_printcb(ntfs_u8 *entry, void *param)
{
	struct ntfs_filldir *nf = param;
	int flags = NTFS_GETU8(entry + 0x51);
	int show_hidden = 0;
	int length = NTFS_GETU8(entry + 0x50);
	int inum = NTFS_GETU32(entry);
	int error;
#ifdef NTFS_NGT_NT_DOES_LOWER
	int i, to_lower = 0;
#endif
	switch (nf->type) {
	case ngt_dos:
		/* Don't display long names. */
		if ((flags & 2) == 0)
			return 0;
		break;
	case ngt_nt:
		/* Don't display short-only names. */
		switch (flags & 3) {
		case 2: 
			return 0;
#ifdef NTFS_NGT_NT_DOES_LOWER
		case 3: 
			to_lower = 1;
#endif
		}
		break;
	case ngt_posix:
		break;
	case ngt_full:
		show_hidden = 1;
		break;
	}
	if (!show_hidden && ((NTFS_GETU8(entry + 0x48) & 2) == 2)) {
		ntfs_debug(DEBUG_OTHER, "Skipping hidden file\n");
		return 0;
	}
	nf->name = 0;
	if (ntfs_encodeuni(NTFS_INO2VOL(nf->dir), (ntfs_u16*)(entry + 0x52),
			   length, &nf->name, &nf->namelen)){
		ntfs_debug(DEBUG_OTHER, "Skipping unrepresentable file\n");
		if (nf->name)
			ntfs_free(nf->name);
		return 0;
	}
	/* Do not return ".", as this is faked. */
	if (length == 1 && *nf->name == '.')
		return 0;
#ifdef NTFS_NGT_NT_DOES_LOWER
	if (to_lower)
		for(i = 0; i < nf->namelen; i++)
			/* This supports ASCII only. Since only DOS-only names
			 * get converted, and since those are restricted to
			 * ASCII, this should be correct. */
			if (nf->name[i] >= 'A' && nf->name[i] <= 'Z')
				nf->name[i] += 'a' - 'A';
#endif
	nf->name[nf->namelen] = 0;
	ntfs_debug(DEBUG_OTHER, "readdir got %s, len %d\n", nf->name,
		       						nf->namelen);
	/* filldir expects an off_t rather than an loff_t. Hope we don't have
	 * more than 65535 index records. */
	error = nf->filldir(nf->dirent, nf->name, nf->namelen,
			    (nf->ph << 16) | nf->pl, inum, DT_UNKNOWN);
	ntfs_free(nf->name);
	return error;
}

/* readdir returns '..', then '.', then the directory entries in sequence.
 * As the root directory contains a entry for itself, '.' is not emulated for
 * the root directory. */
static int ntfs_readdir(struct file* filp, void *dirent, filldir_t filldir)
{
	struct ntfs_filldir cb;
	int error;
	struct inode *dir = filp->f_dentry->d_inode;

	ntfs_debug(DEBUG_OTHER, "ntfs_readdir ino %x mode %x\n",
		   (unsigned)dir->i_ino, (unsigned int)dir->i_mode);

	ntfs_debug(DEBUG_OTHER, "readdir: Looking for file %x dircount %d\n",
		   (unsigned)filp->f_pos, atomic_read(&dir->i_count));
	cb.pl = filp->f_pos & 0xFFFF;
	cb.ph = filp->f_pos >> 16;
	/* End of directory. */
	if (cb.ph == 0xFFFF) {
		/* FIXME: Maybe we can return those with the previous call. */
		switch (cb.pl) {
		case 0: 
			filldir(dirent, ".", 1, filp->f_pos, dir->i_ino,DT_DIR);
			filp->f_pos = 0xFFFF0001;
			return 0;
			/* FIXME: Parent directory. */
		case 1:
			filldir(dirent, "..", 2, filp->f_pos, 0, DT_DIR);
			filp->f_pos = 0xFFFF0002;
			return 0;
		}
		ntfs_debug(DEBUG_OTHER, "readdir: EOD\n");
		return 0;
	}
	cb.dir = dir;
	cb.filldir = filldir;
	cb.dirent = dirent;
	cb.type = NTFS_INO2VOL(dir)->ngt;
	do {
		ntfs_debug(DEBUG_OTHER,"looking for next file\n");
		error = ntfs_getdir_unsorted(NTFS_LINO2NINO(dir), &cb.ph,
					     &cb.pl, ntfs_printcb, &cb);
	} while (!error && cb.ph != 0xFFFFFFFF);
	filp->f_pos = (cb.ph << 16) | cb.pl;
	ntfs_debug(DEBUG_OTHER, "new position %x\n", (unsigned)filp->f_pos);
        /* -EINVAL is on user buffer full. This is not considered as an error
	 * by sys_getdents. */
	if (error == -EINVAL) 
		error = 0;
	/* Otherwise (device error, inconsistent data) return the error code. */
	return error;
}

/* Copied from vfat driver. */
static int simple_getbool(char *s, int *setval)
{
	if (s) {
		if (!strcmp(s, "1") || !strcmp(s, "yes") || !strcmp(s, "true"))
			*setval = 1;
		else if (!strcmp(s, "0") || !strcmp(s, "no") ||
							!strcmp(s, "false"))
			*setval = 0;
		else
			return 0;
	} else
		*setval = 1;
	return 1;
}

/* Parse the (re)mount options. */
static int parse_options(ntfs_volume *vol, char *opt, int remount)
{
	char *value;		/* Defaults if not specified and !remount. */
	ntfs_uid_t uid = -1;	/* 0, root user only */
	ntfs_gid_t gid = -1;	/* 0, root user only */
	int umask = -1;		/* 0077, owner access only */
	unsigned int ngt = -1;	/* ngt_nt */
	void *nls_map = NULL;	/* Try to load the default NLS. */
	int use_utf8 = -1;	/* If no NLS specified and loading the default
				   NLS failed use utf8. */
	if (!opt)
		goto done;
	for (opt = strtok(opt, ","); opt; opt = strtok(NULL, ",")) {
		if ((value = strchr(opt, '=')) != NULL)
			*value ++= '\0';
		if (strcmp(opt, "uid") == 0) {
			if (!value || !*value)
				goto needs_arg;
			uid = simple_strtoul(value, &value, 0);
			if (*value) {
				printk(KERN_ERR "NTFS: uid invalid argument\n");
				return 0;
			}
		} else if (strcmp(opt, "gid") == 0) {
			if (!value || !*value)
				goto needs_arg;
			gid = simple_strtoul(value, &value, 0);
			if (*value) {
				printk(KERN_ERR "NTFS: gid invalid argument\n");
				return 0;
			}
		} else if (strcmp(opt, "umask") == 0) {
			if (!value || !*value)
				goto needs_arg;
			umask = simple_strtoul(value, &value, 0);
			if (*value) {
				printk(KERN_ERR "NTFS: umask invalid "
						"argument\n");
				return 0;
			}
		} else if (strcmp(opt, "posix") == 0) {
			int val;
			if (!value || !*value)
				goto needs_arg;
			if (!simple_getbool(value, &val))
				goto needs_bool;
			ngt = val ? ngt_posix : ngt_nt;
		} else if (strcmp(opt, "show_sys_files") == 0) {
			int val = 0;
			if (!value || !*value)
				val = 1;
			else if (!simple_getbool(value, &val))
				goto needs_bool;
			ngt = val ? ngt_full : ngt_nt;
		} else if (strcmp(opt, "iocharset") == 0) {
			if (!value || !*value)
				goto needs_arg;
			nls_map = load_nls(value);
			if (!nls_map) {
				printk(KERN_ERR "NTFS: charset not found");
				return 0;
			}
		} else if (strcmp(opt, "utf8") == 0) {
			int val = 0;
			if (!value || !*value)
				val = 1;
			else if (!simple_getbool(value, &val))
				goto needs_bool;
			use_utf8 = val;
		} else {
			printk(KERN_ERR "NTFS: unkown option '%s'\n", opt);
			return 0;
		}
	}
done:
	if (use_utf8 != -1 && use_utf8) {
		if (nls_map) {
			unload_nls(nls_map);
			printk(KERN_ERR "NTFS: utf8 cannot be combined with "
					"iocharset.\n");
			return 0;
		}
		if (remount && vol->nls_map)
			unload_nls(vol->nls_map);
		vol->nls_map = NULL;
	} else {
		if (nls_map) {
			if (remount && vol->nls_map)
				unload_nls(vol->nls_map);
			vol->nls_map = nls_map;
		} else if (!remount || (remount && !use_utf8 && !vol->nls_map))
			vol->nls_map = load_nls_default();
	}
	if (uid != -1)
		vol->uid = uid;
	else if (!remount)
		vol->uid = 0;
	if (gid != -1)
		vol->gid = gid;
	else if (!remount)
		vol->gid = 0;
	if (umask != -1)
		vol->umask = (ntmode_t)umask;
	else if (!remount)
		vol->umask = 0077;
	if (ngt != -1)
		vol->ngt = ngt;
	else if (!remount)
		vol->ngt = ngt_nt;
	return 1;
needs_arg:
	printk(KERN_ERR "NTFS: %s needs an argument", opt);
	return 0;
needs_bool:
	printk(KERN_ERR "NTFS: %s needs boolean argument", opt);
	return 0;
}
			
static struct dentry *ntfs_lookup(struct inode *dir, struct dentry *d)
{
	struct inode *res = 0;
	char *item = 0;
	ntfs_iterate_s walk;
	int error;
	
	ntfs_debug(DEBUG_NAME1, "Looking up %s in %x\n", d->d_name.name,
		   (unsigned)dir->i_ino);
	/* Convert to wide string. */
	error = ntfs_decodeuni(NTFS_INO2VOL(dir), (char*)d->d_name.name,
			       d->d_name.len, &walk.name, &walk.namelen);
	if (error)
		return ERR_PTR(error);
	item = ntfs_malloc(ITEM_SIZE);
	if (!item)
		return ERR_PTR(-ENOMEM);
	/* ntfs_getdir will place the directory entry into item, and the first
	 * long long is the MFT record number. */
	walk.type = BY_NAME;
	walk.dir = NTFS_LINO2NINO(dir);
	walk.result = item;
	if (ntfs_getdir_byname(&walk))
		res = iget(dir->i_sb, NTFS_GETU32(item));
	d_add(d, res);
	ntfs_free(item);
	ntfs_free(walk.name);
	/* Always return success, the dcache will handle negative entries. */
	return NULL;
}

static struct file_operations ntfs_file_operations_nommap = {
	read:		ntfs_read,
#ifdef CONFIG_NTFS_RW
	write:		ntfs_write,
#endif
};

static struct inode_operations ntfs_inode_operations_nobmap;

#ifdef CONFIG_NTFS_RW
static int ntfs_create(struct inode* dir, struct dentry *d, int mode)
{
	struct inode *r = 0;
	ntfs_inode *ino = 0;
	ntfs_volume *vol;
	int error = 0;
	ntfs_attribute *si;

	r = new_inode(dir->i_sb);
	if (!r) {
		error = -ENOMEM;
		goto fail;
	}
	ntfs_debug(DEBUG_OTHER, "ntfs_create %s\n", d->d_name.name);
	vol = NTFS_INO2VOL(dir);
	ino = NTFS_LINO2NINO(r);
	error = ntfs_alloc_file(NTFS_LINO2NINO(dir), ino, (char*)d->d_name.name,
				d->d_name.len);
	if (error) {
		ntfs_error("ntfs_alloc_file FAILED: error = %i", error);
		goto fail;
	}
	/* Not doing this one was causing a huge amount of corruption! Now the
	 * bugger bytes the dust! (-8 (AIA) */
	r->i_ino = ino->i_number;
	error = ntfs_update_inode(ino);
	if (error)
		goto fail;
	error = ntfs_update_inode(NTFS_LINO2NINO(dir));
	if (error)
		goto fail;
	r->i_uid = vol->uid;
	r->i_gid = vol->gid;
	/* FIXME: dirty? dev? */
	/* Get the file modification times from the standard information. */
	si = ntfs_find_attr(ino, vol->at_standard_information, NULL);
	if (si) {
		char *attr = si->d.data;
		r->i_atime = ntfs_ntutc2unixutc(NTFS_GETU64(attr + 0x18));
		r->i_ctime = ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		r->i_mtime = ntfs_ntutc2unixutc(NTFS_GETU64(attr + 8));
	}
	/* It's not a directory */
	r->i_op = &ntfs_inode_operations_nobmap;
	r->i_fop = &ntfs_file_operations_nommap,
	r->i_mode = S_IFREG | S_IRUGO;
#ifdef CONFIG_NTFS_RW
	r->i_mode |= S_IWUGO;
#endif
	r->i_mode &= ~vol->umask;
	insert_inode_hash(r);
	d_instantiate(d, r);
	return 0;
 fail:
	if (r)
		iput(r);
	return error;
}

static int _linux_ntfs_mkdir(struct inode *dir, struct dentry* d, int mode)
{
	int error;
	struct inode *r = 0;
	ntfs_volume *vol;
	ntfs_inode *ino;
	ntfs_attribute *si;

	ntfs_debug (DEBUG_DIR1, "mkdir %s in %x\n", d->d_name.name, dir->i_ino);
	error = -ENAMETOOLONG;
	if (d->d_name.len > /* FIXME: */ 255)
		goto out;
	error = -EIO;
	r = new_inode(dir->i_sb);
	if (!r)
		goto out;
	vol = NTFS_INO2VOL(dir);
	ino = NTFS_LINO2NINO(r);
	error = ntfs_mkdir(NTFS_LINO2NINO(dir), d->d_name.name, d->d_name.len,
			   ino);
	if (error)
		goto out;
	/* Not doing this one was causing a huge amount of corruption! Now the
	 * bugger bytes the dust! (-8 (AIA) */
	r->i_ino = ino->i_number;
	r->i_uid = vol->uid;
	r->i_gid = vol->gid;
	si = ntfs_find_attr(ino, vol->at_standard_information, NULL);
	if (si) {
		char *attr = si->d.data;
		r->i_atime = ntfs_ntutc2unixutc(NTFS_GETU64(attr + 0x18));
		r->i_ctime = ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		r->i_mtime = ntfs_ntutc2unixutc(NTFS_GETU64(attr + 8));
	}
	/* It's a directory. */
	r->i_op = &ntfs_dir_inode_operations;
	r->i_fop = &ntfs_dir_operations;
	r->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
#ifdef CONFIG_NTFS_RW
	r->i_mode |= S_IWUGO;
#endif
	r->i_mode &= ~vol->umask;	
	
	insert_inode_hash(r);
	d_instantiate(d, r);
	error = 0;
 out:
 	ntfs_debug (DEBUG_DIR1, "mkdir returns %d\n", error);
	return error;
}
#endif

#if 0
static int ntfs_bmap(struct inode *ino, int block)
{
	int ret = ntfs_vcn_to_lcn(NTFS_LINO2NINO(ino), block);
	ntfs_debug(DEBUG_OTHER, "bmap of %lx, block %x is %x\n", ino->i_ino,
		   block, ret);
	return (ret == -1) ? 0 : ret;
}
#endif

/* It's fscking broken. */
/* FIXME: [bm]map code is disabled until ntfs_get_block() gets sorted! */
/*
static int ntfs_get_block(struct inode *inode, long block, struct buffer_head *bh, int create)
{
	BUG();
	return -1;
}

static struct file_operations ntfs_file_operations = {
	read:		ntfs_read,
	mmap:		generic_file_mmap,
#ifdef CONFIG_NTFS_RW
	write:		ntfs_write,
#endif
};

static struct inode_operations ntfs_inode_operations;
*/

static struct file_operations ntfs_dir_operations = {
	read:		generic_read_dir,
	readdir:	ntfs_readdir,
};

static struct inode_operations ntfs_dir_inode_operations = {
	lookup:		ntfs_lookup,
#ifdef CONFIG_NTFS_RW
	create:		ntfs_create,
	mkdir:		_linux_ntfs_mkdir,
#endif
};

/*
static int ntfs_writepage(struct page *page)
{
	return block_write_full_page(page,ntfs_get_block);
}

static int ntfs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,ntfs_get_block);
}

static int ntfs_prepare_write(struct file *file, struct page *page,
			      unsigned from, unsigned to)
{
	return cont_prepare_write(page, from, to, ntfs_get_block,
				  &page->mapping->host->u.ntfs_i.mmu_private);
}

static int _ntfs_bmap(struct address_space *mapping, long block)
{
	return generic_block_bmap(mapping, block, ntfs_get_block);
}

struct address_space_operations ntfs_aops = {
	readpage: ntfs_readpage,
	writepage: ntfs_writepage,
	sync_page: block_sync_page,
	prepare_write: ntfs_prepare_write,
	commit_write: generic_commit_write,
	bmap: _ntfs_bmap
};
*/

/* ntfs_read_inode() is called by the Virtual File System (the kernel layer 
 * that deals with filesystems) when iget is called requesting an inode not
 * already present in the inode table. Typically filesystems have separate
 * inode_operations for directories, files and symlinks. */
static void ntfs_read_inode(struct inode* inode)
{
	ntfs_volume *vol;
	int can_mmap = 0;
	ntfs_inode *ino;
	ntfs_attribute *data;
	ntfs_attribute *si;

	vol = NTFS_INO2VOL(inode);
	inode->i_mode = 0;
	ntfs_debug(DEBUG_OTHER, "ntfs_read_inode 0x%x\n", (unsigned)inode->i_ino);
	/*
	 * This kills all accesses to system files (except $Extend directory).
	 * The driver can bypass this by calling ntfs_init_inode() directly.
	 * Only if ngt is ngt_full do we allow access to the system files.
	 */
	switch (inode->i_ino) {
		/* Those are loaded special files. */
	case FILE_$Mft:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to open $MFT!\n");
			return;
		}
		if (!vol->mft_ino || ((vol->ino_flags & 1) == 0))
			goto sys_file_error;
		ntfs_memcpy(&inode->u.ntfs_i, vol->mft_ino, sizeof(ntfs_inode));
		ino = vol->mft_ino;
		vol->mft_ino = &inode->u.ntfs_i;
		vol->ino_flags &= ~1;
		ntfs_free(ino);
		ino = vol->mft_ino;
		ntfs_debug(DEBUG_OTHER, "Opening $MFT!\n");
		break;
	case FILE_$MftMirr:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to open $MFTMirr!\n");
			return;
		}
		if (!vol->mftmirr || ((vol->ino_flags & 2) == 0))
			goto sys_file_error;
		ntfs_memcpy(&inode->u.ntfs_i, vol->mftmirr, sizeof(ntfs_inode));
		ino = vol->mftmirr;
		vol->mftmirr = &inode->u.ntfs_i;
		vol->ino_flags &= ~2;
		ntfs_free(ino);
		ino = vol->mftmirr;
		ntfs_debug(DEBUG_OTHER, "Opening $MFTMirr!\n");
		break;
	case FILE_$BitMap:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to open $Bitmap!\n");
			return;
		}
		if (!vol->bitmap || ((vol->ino_flags & 4) == 0))
			goto sys_file_error;
		ntfs_memcpy(&inode->u.ntfs_i, vol->bitmap, sizeof(ntfs_inode));
		ino = vol->bitmap;
		vol->bitmap = &inode->u.ntfs_i;
		vol->ino_flags &= ~4;
		ntfs_free(ino);
		ino = vol->bitmap;
		ntfs_debug(DEBUG_OTHER, "Opening $Bitmap!\n");
		break;
	case FILE_$LogFile ... FILE_$AttrDef:
	/* We need to allow reading the root directory. */
	case FILE_$Boot ... FILE_$UpCase:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to open system file %i!\n",
								inode->i_ino);
	 		return;
		} /* Do the default for ngt_full. */
		ntfs_debug(DEBUG_OTHER, "Opening system file %i!\n", inode->i_ino);
	default:
		ino = &inode->u.ntfs_i;
		if (!ino || ntfs_init_inode(ino, NTFS_INO2VOL(inode),
								inode->i_ino))
		{
			ntfs_debug(DEBUG_OTHER, "NTFS: Error loading inode "
					"0x%x\n", (unsigned int)inode->i_ino);
			return;
		}
	}
	/* Set uid/gid from mount options */
	inode->i_uid = vol->uid;
	inode->i_gid = vol->gid;
	inode->i_nlink = 1;
	/* Use the size of the data attribute as file size */
	data = ntfs_find_attr(ino, vol->at_data, NULL);
	if (!data) {
		inode->i_size = 0;
		can_mmap = 0;
	} else {
		inode->i_size = data->size;
		/* FIXME: once ntfs_get_block is implemented, uncomment the
		 * next line and remove the "can_mmap = 0;". (AIA) */
		/* can_mmap = !data->resident && !data->compressed; */
		can_mmap = 0;
	}
	/* Get the file modification times from the standard information. */
	si = ntfs_find_attr(ino, vol->at_standard_information, NULL);
	if (si) {
		char *attr = si->d.data;
		inode->i_atime = ntfs_ntutc2unixutc(NTFS_GETU64(attr + 0x18));
		inode->i_ctime = ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		inode->i_mtime = ntfs_ntutc2unixutc(NTFS_GETU64(attr + 8));
	}
	/* If it has an index root, it's a directory. */
	if (ntfs_find_attr(ino, vol->at_index_root, "$I30")) {
		ntfs_attribute *at;
		at = ntfs_find_attr(ino, vol->at_index_allocation, "$I30");
		inode->i_size = at ? at->size : 0;
		inode->i_op = &ntfs_dir_inode_operations;
		inode->i_fop = &ntfs_dir_operations;
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
	} else {
		/* As long as ntfs_get_block() is just a call to BUG() do not
	 	 * define any [bm]map ops or we get the BUG() whenever someone
		 * runs mc or mpg123 on an ntfs partition!
		 * FIXME: Uncomment the below code when ntfs_get_block is
		 * implemented. */
		/* if (can_mmap) {
			inode->i_op = &ntfs_inode_operations;
			inode->i_fop = &ntfs_file_operations;
			inode->i_mapping->a_ops = &ntfs_aops;
			inode->u.ntfs_i.mmu_private = inode->i_size;
		} else */ {
			inode->i_op = &ntfs_inode_operations_nobmap;
			inode->i_fop = &ntfs_file_operations_nommap;
		}
		inode->i_mode = S_IFREG | S_IRUGO;
	}
#ifdef CONFIG_NTFS_RW
	if (!data || !data->compressed)
		inode->i_mode |= S_IWUGO;
#endif
	inode->i_mode &= ~vol->umask;
	return;
sys_file_error:
	ntfs_error("Critical error. Tried to call ntfs_read_inode() before we "
		"have completed read_super() or VFS error.\n");
	// FIXME: Should we panic() at this stage?
}

#ifdef CONFIG_NTFS_RW
static void ntfs_write_inode(struct inode *ino, int unused)
{
	lock_kernel();
	ntfs_debug(DEBUG_LINUX, "ntfs_write_inode 0x%x\n", ino->i_ino);
	ntfs_update_inode(NTFS_LINO2NINO(ino));
	unlock_kernel();
}
#endif

static void _ntfs_clear_inode(struct inode *inode)
{
	ntfs_inode *ino;
	ntfs_volume *vol;
	
	lock_kernel();
	ntfs_debug(DEBUG_OTHER, "_ntfs_clear_inode 0x%x\n", inode->i_ino);
	vol = NTFS_INO2VOL(inode);
	if (!vol)
		ntfs_error("_ntfs_clear_inode: vol = NTFS_INO2VOL(inode) is NULL.\n");
	switch (inode->i_ino) {
	case FILE_$Mft:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to _clear_inode of $MFT!\n");
			goto unl_out;
		}
		if (vol->mft_ino && ((vol->ino_flags & 1) == 0)) {
			ino = (ntfs_inode*)ntfs_malloc(sizeof(ntfs_inode));
			ntfs_memcpy(ino, &inode->u.ntfs_i, sizeof(ntfs_inode));
			vol->mft_ino = ino;
			vol->ino_flags |= 1;
			goto unl_out;
		}
		break;
	case FILE_$MftMirr:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to _clear_inode of $MFTMirr!\n");
			goto unl_out;
		}
		if (vol->mftmirr && ((vol->ino_flags & 2) == 0)) {
			ino = (ntfs_inode*)ntfs_malloc(sizeof(ntfs_inode));
			ntfs_memcpy(ino, &inode->u.ntfs_i, sizeof(ntfs_inode));
			vol->mftmirr = ino;
			vol->ino_flags |= 2;
			goto unl_out;
		}
		break;
	case FILE_$BitMap:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to _clear_inode of $Bitmap!\n");
			goto unl_out;
		}
		if (vol->bitmap && ((vol->ino_flags & 4) == 0)) {
			ino = (ntfs_inode*)ntfs_malloc(sizeof(ntfs_inode));
			ntfs_memcpy(ino, &inode->u.ntfs_i, sizeof(ntfs_inode));
			vol->bitmap = ino;
			vol->ino_flags |= 4;
			goto unl_out;
		}
		break;
	case FILE_$LogFile ... FILE_$AttrDef:
	case FILE_$Boot ... FILE_$UpCase:
		if (vol->ngt != ngt_full) {
			ntfs_error("Trying to _clear_inode of system file %i! "
					"Shouldn't happen.\n", inode->i_ino);
			goto unl_out;
		} /* Do the default for ngt_full. */
	default:
		/* Nothing. Just clear the inode and exit. */
	}
	ntfs_clear_inode(&inode->u.ntfs_i);
unl_out:
	unlock_kernel();
	return;
}

/* Called when umounting a filesystem by do_umount() in fs/super.c. */
static void ntfs_put_super(struct super_block *sb)
{
	ntfs_volume *vol;

	ntfs_debug(DEBUG_OTHER, "ntfs_put_super\n");
	vol = NTFS_SB2VOL(sb);
	ntfs_release_volume(vol);
	if (vol->nls_map)
		unload_nls(vol->nls_map);
	ntfs_debug(DEBUG_OTHER, "ntfs_put_super: done\n");
}

/* Called by the kernel when asking for stats. */
static int ntfs_statfs(struct super_block *sb, struct statfs *sf)
{
	struct inode *mft;
	ntfs_volume *vol;
	__s64 size;
	int error;

	ntfs_debug(DEBUG_OTHER, "ntfs_statfs\n");
	vol = NTFS_SB2VOL(sb);
	sf->f_type = NTFS_SUPER_MAGIC;
	sf->f_bsize = vol->cluster_size;
	error = ntfs_get_volumesize(NTFS_SB2VOL(sb), &size);
	if (error)
		return error;
	sf->f_blocks = size;	/* Volumesize is in clusters. */
	size = (__s64)ntfs_get_free_cluster_count(vol->bitmap);
	/* Just say zero if the call failed. */
	if (size < 0LL)
		size = 0;
	sf->f_bfree = sf->f_bavail = size;
	ntfs_debug(DEBUG_OTHER, "ntfs_statfs: calling mft = iget(sb, FILE_$Mft)\n");
	mft = iget(sb, FILE_$Mft);
	ntfs_debug(DEBUG_OTHER, "ntfs_statfs: iget(sb, FILE_$Mft) returned 0x%x\n", mft);
	if (!mft)
		return -EIO;
	sf->f_files = mft->i_size >> vol->mft_record_size_bits;
	ntfs_debug(DEBUG_OTHER, "ntfs_statfs: calling iput(mft)\n");
	iput(mft);
	/* Should be read from volume. */
	sf->f_namelen = 255;
	return 0;
}

/* Called when remounting a filesystem by do_remount_sb() in fs/super.c. */
static int ntfs_remount_fs(struct super_block *sb, int *flags, char *options)
{
	if (!parse_options(NTFS_SB2VOL(sb), options, 1))
		return -EINVAL;
	return 0;
}

/* Define the super block operation that are implemented */
static struct super_operations ntfs_super_operations = {
	read_inode:	ntfs_read_inode,
#ifdef CONFIG_NTFS_RW
	write_inode:	ntfs_write_inode,
#endif
	put_super:	ntfs_put_super,
	statfs:		ntfs_statfs,
	remount_fs:	ntfs_remount_fs,
	clear_inode:	_ntfs_clear_inode,
};

/**
 * is_boot_sector_ntfs - check an NTFS boot sector for validity
 * @b:		buffer containing bootsector to check
 * 
 * Check whether @b contains a valid NTFS boot sector.
 * Return 1 if @b is a valid NTFS bootsector or 0 if not.
 */
static int is_boot_sector_ntfs(ntfs_u8 *b)
{
	ntfs_u32 i;

	/* FIXME: We don't use checksumming yet as NT4(SP6a) doesn't either...
	 * But we might as well have the code ready to do it. (AIA) */
#if 0
	/* Calculate the checksum. */
	if (b < b + 0x50) {
		ntfs_u32 *u;
		ntfs_u32 *bi = (ntfs_u32 *)(b + 0x50);
		
		for (u = bi, i = 0; u < bi; ++u)
			i += NTFS_GETU32(*u);
	}
#endif
	/* Check magic is "NTFS    " */
	if (b[3] != 0x4e) goto not_ntfs;
	if (b[4] != 0x54) goto not_ntfs;
	if (b[5] != 0x46) goto not_ntfs;
	if (b[6] != 0x53) goto not_ntfs;
	for (i = 7; i < 0xb; ++i)
		if (b[i] != 0x20) goto not_ntfs;
	/* Check bytes per sector value is between 512 and 4096. */
	if (b[0xb] != 0) goto not_ntfs;
	if (b[0xc] > 0x10) goto not_ntfs;
	/* Check sectors per cluster value is valid. */
	switch (b[0xd]) {
	case 1: case 2: case 4: case 8: case 16:
	case 32: case 64: case 128:
		break;
	default:
		goto not_ntfs;
	}
	/* Check reserved sectors value and four other fields are zero. */
	for (i = 0xe; i < 0x15; ++i) 
		if (b[i] != 0) goto not_ntfs;
	if (b[0x16] != 0) goto not_ntfs;
	if (b[0x17] != 0) goto not_ntfs;
	for (i = 0x20; i < 0x24; ++i)
		if (b[i] != 0) goto not_ntfs;
	/* Check clusters per file record segment value is valid. */
	if (b[0x40] < 0xe1 || b[0x40] > 0xf7) {
		switch (b[0x40]) {
		case 1: case 2: case 4: case 8: case 16: case 32: case 64:
			break;
		default:
			goto not_ntfs;
		}
	}
	/* Check clusters per index block value is valid. */
	if (b[0x44] < 0xe1 || b[0x44] > 0xf7) {
		switch (b[0x44]) {
		case 1: case 2: case 4: case 8: case 16: case 32: case 64:
			break;
		default:
			goto not_ntfs;
		}
	}
	return 1;
not_ntfs:
	return 0;
}

/* Called to mount a filesystem by read_super() in fs/super.c.
 * Return a super block, the main structure of a filesystem.
 *
 * NOTE : Don't store a pointer to an option, as the page containing the
 * options is freed after ntfs_read_super() returns.
 *
 * NOTE : A context switch can happen in kernel code only if the code blocks
 * (= calls schedule() in kernel/sched.c). */
struct super_block * ntfs_read_super(struct super_block *sb, void *options,
				     int silent)
{
	ntfs_volume *vol;
	struct buffer_head *bh;
	int i;

	ntfs_debug(DEBUG_OTHER, "ntfs_read_super\n");
	vol = NTFS_SB2VOL(sb);
	if (!parse_options(vol, (char*)options, 0))
		goto ntfs_read_super_vol;
	/* Assume a 512 bytes block device for now. */
	set_blocksize(sb->s_dev, 512);
	/* Read the super block (boot block). */
	if (!(bh = bread(sb->s_dev, 0, 512))) {
		ntfs_error("Reading super block failed\n");
		goto ntfs_read_super_unl;
	}
	ntfs_debug(DEBUG_OTHER, "Done reading boot block\n");
	/* Check for 'NTFS' magic number */
	if (!is_boot_sector_ntfs(bh->b_data)) {
		ntfs_debug(DEBUG_OTHER, "Not a NTFS volume\n");
		bforget(bh);
		goto ntfs_read_super_unl;
	}
	ntfs_debug(DEBUG_OTHER, "Going to init volume\n");
	if (ntfs_init_volume(vol, bh->b_data) < 0) {
		ntfs_debug(DEBUG_OTHER, "Init volume failed.\n");
		bforget(bh);
		goto ntfs_read_super_unl;
	}
	ntfs_debug(DEBUG_OTHER, "$Mft at cluster 0x%Lx\n", vol->mft_lcn);
	bforget(bh);
	NTFS_SB(vol) = sb;
	if (vol->cluster_size > PAGE_SIZE) {
		ntfs_error("Partition cluster size is not supported yet (it "
			   "is > max kernel blocksize).\n");
		goto ntfs_read_super_unl;
	}
	ntfs_debug(DEBUG_OTHER, "Done to init volume\n");
	/* Inform the kernel that a device block is a NTFS cluster. */
	sb->s_blocksize = vol->cluster_size;
	for (i = sb->s_blocksize, sb->s_blocksize_bits = 0; i != 1; i >>= 1)
		sb->s_blocksize_bits++;
	set_blocksize(sb->s_dev, sb->s_blocksize);
	ntfs_debug(DEBUG_OTHER, "set_blocksize\n");
	/* Allocate an MFT record (MFT record can be smaller than a cluster). */
	if (!(vol->mft = ntfs_malloc(max(int, vol->mft_record_size,
					 vol->cluster_size))))
		goto ntfs_read_super_unl;

	/* Read at least the MFT record for $Mft. */
	for (i = 0; i < max(int, vol->mft_clusters_per_record, 1); i++) {
		if (!(bh = bread(sb->s_dev, vol->mft_lcn + i,
							  vol->cluster_size))) {
			ntfs_error("Could not read $Mft record 0\n");
			goto ntfs_read_super_mft;
		}
		ntfs_memcpy(vol->mft + ((__s64)i << vol->cluster_size_bits),
						bh->b_data, vol->cluster_size);
		brelse(bh);
		ntfs_debug(DEBUG_OTHER, "Read cluster 0x%x\n",
							 vol->mft_lcn + i);
	}
	/* Check and fixup this MFT record */
	if (!ntfs_check_mft_record(vol, vol->mft)){
		ntfs_error("Invalid $Mft record 0\n");
		goto ntfs_read_super_mft;
	}
	/* Inform the kernel about which super operations are available. */
	sb->s_op = &ntfs_super_operations;
	sb->s_magic = NTFS_SUPER_MAGIC;
	sb->s_maxbytes = ~0ULL >> 1;
	ntfs_debug(DEBUG_OTHER, "Reading special files\n");
	if (ntfs_load_special_files(vol)) {
		ntfs_error("Error loading special files\n");
		goto ntfs_read_super_mft;
	}
	ntfs_debug(DEBUG_OTHER, "Getting RootDir\n");
	/* Get the root directory. */
	if (!(sb->s_root = d_alloc_root(iget(sb, FILE_$root)))) {
		ntfs_error("Could not get root dir inode\n");
		goto ntfs_read_super_mft;
	}
ntfs_read_super_ret:
	ntfs_debug(DEBUG_OTHER, "read_super: done\n");
	return sb;
ntfs_read_super_mft:
	ntfs_free(vol->mft);
ntfs_read_super_unl:
ntfs_read_super_vol:
	sb = NULL;
	goto ntfs_read_super_ret;
}

/* Define the filesystem */
static DECLARE_FSTYPE_DEV(ntfs_fs_type, "ntfs", ntfs_read_super);

static int __init init_ntfs_fs(void)
{
	/* Comment this if you trust klogd. There are reasons not to trust it */
#if defined(DEBUG) && !defined(MODULE)
	console_verbose();
#endif
	printk(KERN_NOTICE "NTFS version " NTFS_VERSION "\n");
	SYSCTL(1);
	ntfs_debug(DEBUG_OTHER, "registering %s\n", ntfs_fs_type.name);
	/* Add this filesystem to the kernel table of filesystems. */
	return register_filesystem(&ntfs_fs_type);
}

static void __exit exit_ntfs_fs(void)
{
	SYSCTL(0);
	ntfs_debug(DEBUG_OTHER, "unregistering %s\n", ntfs_fs_type.name);
	unregister_filesystem(&ntfs_fs_type);
}

EXPORT_NO_SYMBOLS;
/*
 * Not strictly true. The driver was written originally by Martin von Löwis.
 * I am just maintaining and rewriting it.
 */
MODULE_AUTHOR("Anton Altaparmakov <aia21@cus.cam.ac.uk>");
MODULE_DESCRIPTION("NTFS driver");
#ifdef DEBUG
MODULE_PARM(ntdebug, "i");
MODULE_PARM_DESC(ntdebug, "Debug level");
#endif

module_init(init_ntfs_fs)
module_exit(exit_ntfs_fs)


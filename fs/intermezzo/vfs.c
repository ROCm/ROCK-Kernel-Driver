/*
 * vfs.c
 *
 * This file implements kernel downcalls from lento.
 *
 * Author: Rob Simmonds <simmonds@stelias.com>
 *         Andreas Dilger <adilger@stelias.com>
 * Copyright (C) 2000 Stelias Computing Inc
 * Copyright (C) 2000 Red Hat Inc.
 *
 * Extended attribute support
 * Copyright (C) 2001 Shirish H. Phatak, Tacit Networks, Inc.
 *
 * This code is based on code from namei.c in the linux file system;
 * see copyright notice below.
 */

/** namei.c copyright **/

/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

/* [Feb 1997 T. Schoebel-Theuer] Complete rewrite of the pathname
 * lookup logic.
 */

/** end of namei.c copyright **/

#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/quotaops.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/semaphore.h>
#include <asm/pgtable.h>

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/blk.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>
#include <linux/intermezzo_kml.h>

#ifdef CONFIG_FS_EXT_ATTR
#include <linux/ext_attr.h>

#ifdef CONFIG_FS_POSIX_ACL
#include <linux/posix_acl.h>
#endif
#endif

extern struct inode_operations presto_sym_iops;

/*
 * It's inline, so penalty for filesystems that don't use sticky bit is
 * minimal.
 */
static inline int check_sticky(struct inode *dir, struct inode *inode)
{
        if (!(dir->i_mode & S_ISVTX))
                return 0;
        if (inode->i_uid == current->fsuid)
                return 0;
        if (dir->i_uid == current->fsuid)
                return 0;
        return !capable(CAP_FOWNER);
}

/* from linux/fs/namei.c */
static inline int may_delete(struct inode *dir,struct dentry *victim, int isdir)
{
        int error;
        if (!victim->d_inode || victim->d_parent->d_inode != dir)
                return -ENOENT;
        error = permission(dir,MAY_WRITE | MAY_EXEC);
        if (error)
                return error;
        if (IS_APPEND(dir))
                return -EPERM;
        if (check_sticky(dir, victim->d_inode)||IS_APPEND(victim->d_inode)||
            IS_IMMUTABLE(victim->d_inode))
                return -EPERM;
        if (isdir) {
                if (!S_ISDIR(victim->d_inode->i_mode))
                        return -ENOTDIR;
                if (IS_ROOT(victim))
                        return -EBUSY;
        } else if (S_ISDIR(victim->d_inode->i_mode))
                return -EISDIR;
        return 0;
}

/* from linux/fs/namei.c */
static inline int may_create(struct inode *dir, struct dentry *child) {
        if (child->d_inode)
                return -EEXIST;
        if (IS_DEADDIR(dir))
                return -ENOENT;
        return permission(dir,MAY_WRITE | MAY_EXEC);
}

#ifdef PRESTO_DEBUG
/* The loop_discard_io() function is available via a kernel patch to the
 * loop block device.  It "works" by accepting writes, but throwing them
 * away, rather than trying to write them to disk.  The old method worked
 * by setting the underlying device read-only, but that has the problem
 * that dirty buffers are kept in memory, and ext3 didn't like that at all.
 */
#ifdef CONFIG_LOOP_DISCARD
#define BLKDEV_FAIL(dev,fail) loop_discard_io(dev,fail)
#else
#define BLKDEV_FAIL(dev,fail) set_device_ro(dev, 1)
#endif

/* If a breakpoint has been set via /proc/sys/intermezzo/intermezzoX/errorval,
 * that is the same as "value", the underlying device will "fail" now.
 */
inline void presto_debug_fail_blkdev(struct presto_file_set *fset,
                                     unsigned long value)
{
        int minor = presto_f2m(fset);
        int errorval = upc_comms[minor].uc_errorval;
        kdev_t dev = fset->fset_mtpt->d_inode->i_dev;

        if (errorval && errorval == (long)value && !is_read_only(dev)) {
                CDEBUG(D_SUPER, "setting device %s read only\n", kdevname(dev));
                BLKDEV_FAIL(dev, 1);
                upc_comms[minor].uc_errorval = -dev;
        }
}
#else
#define presto_debug_fail_blkdev(dev,value) do {} while (0)
#endif


static inline int presto_do_kml(struct lento_vfs_context *info, struct inode* inode)
{
        if ( ! (info->flags & LENTO_FL_KML) ) 
                return 0;
        if ( inode->i_gid == presto_excluded_gid ) 
                return 0;
        return 1;
}

static inline int presto_do_expect(struct lento_vfs_context *info, struct inode *inode)
{
        if ( ! (info->flags & LENTO_FL_EXPECT) ) 
                return 0;
        if ( inode->i_gid == presto_excluded_gid ) 
                return 0;
        return 1;
}


/* XXX fixme: this should not fail, all these dentries are in memory
   when _we_ call this */
int presto_settime(struct presto_file_set *fset, 
                   struct dentry *dentry, 
                   struct lento_vfs_context *ctx, 
                   int valid)
{
        int error; 
        struct inode *inode = dentry->d_inode;
        struct inode_operations *iops;
        struct iattr iattr;

        ENTRY;
        if (ctx->flags &  LENTO_FL_IGNORE_TIME ) { 
                EXIT;
                return 0;
        }
        iattr.ia_ctime = ctx->updated_time;
        iattr.ia_mtime = ctx->updated_time;
        iattr.ia_valid = valid;

        error = -EROFS;
        if (IS_RDONLY(inode)) {
                EXIT;
                return -EROFS;
        }

        if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
                EXIT;
                return -EPERM;
        }

        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter); 
        if (!iops) { 
                EXIT;
                return error;
        }

        if (iops->setattr != NULL)
                error = iops->setattr(dentry, &iattr);
        else {
		error = 0;
                inode_setattr(dentry->d_inode, &iattr);
	}
        EXIT;
        return error;
}


int presto_do_setattr(struct presto_file_set *fset, struct dentry *dentry,
                      struct iattr *iattr, struct lento_vfs_context *info)
{
        struct rec_info rec;
        struct inode *inode = dentry->d_inode;
        struct inode_operations *iops;
        int error;
        struct presto_version old_ver, new_ver;
        void *handle;
	off_t old_size=inode->i_size;

        ENTRY;
        error = -EROFS;
        if (IS_RDONLY(inode)) {
                EXIT;
                return -EROFS;
        }

        if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
                EXIT;
                return -EPERM;
        }

        presto_getversion(&old_ver, dentry->d_inode);
        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter); 
        if (!iops &&
            !iops->setattr) {
                EXIT;
                return error;
        }

	error = presto_reserve_space(fset->fset_cache, 2*PRESTO_REQHIGH); 
	if (error) {
		EXIT;
		return error;
	}

	if  (iattr->ia_valid & ATTR_SIZE) { 
		handle = presto_trans_start(fset, dentry->d_inode, PRESTO_OP_TRUNC);
	} else {
		handle = presto_trans_start(fset, dentry->d_inode, PRESTO_OP_SETATTR);
	}

        if ( IS_ERR(handle) ) {
                printk("presto_do_setattr: no space for transaction\n");
		presto_release_space(fset->fset_cache, 2*PRESTO_REQHIGH); 
                return -ENOSPC;
        }

        if (dentry->d_inode && iops->setattr) {
                error = iops->setattr(dentry, iattr);
        } else {
                error = inode_change_ok(dentry->d_inode, iattr);
                if (!error) 
                        inode_setattr(inode, iattr);
        }

	if (!error && (iattr->ia_valid & ATTR_SIZE))
		vmtruncate(inode, iattr->ia_size);

        if (error) {
                EXIT;
                goto exit;
        }

        presto_debug_fail_blkdev(fset, PRESTO_OP_SETATTR | 0x10);

        if ( presto_do_kml(info, dentry->d_inode) ) {
                if ((iattr->ia_valid & ATTR_SIZE) && (old_size != inode->i_size)) {
                	struct file file;
                	/* Journal a close whenever we see a potential truncate
                 	* At the receiving end, lento should explicitly remove
                 	* ATTR_SIZE from the list of valid attributes */
                	presto_getversion(&new_ver, inode);
                	file.private_data = NULL;
                	file.f_dentry = dentry;
                	error=presto_journal_close(&rec, fset, &file, dentry, &new_ver);
            	}

		if (!error)
                	error = presto_journal_setattr(&rec, fset, dentry, &old_ver, iattr);
        }

        presto_debug_fail_blkdev(fset, PRESTO_OP_SETATTR | 0x20);
        if ( presto_do_expect(info, dentry->d_inode) )
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_SETATTR | 0x30);

        EXIT;
exit:
	presto_release_space(fset->fset_cache, 2*PRESTO_REQHIGH); 
        presto_trans_commit(fset, handle);
        return error;
}

int lento_setattr(const char *name, struct iattr *iattr,
                  struct lento_vfs_context *info)
{
        struct nameidata nd;
        struct dentry *dentry;
        struct presto_file_set *fset;
        int error;
#ifdef  CONFIG_FS_POSIX_ACL
        int (*set_posix_acl)(struct inode *, int type, posix_acl_t *)=NULL;
#endif

        ENTRY;
        CDEBUG(D_PIOCTL,"name %s, valid %#x, mode %#o, uid %d, gid %d, size %Ld\n",
               name, iattr->ia_valid, iattr->ia_mode, iattr->ia_uid,
               iattr->ia_gid, iattr->ia_size);
        CDEBUG(D_PIOCTL, "atime %#lx, mtime %#lx, ctime %#lx, attr_flags %#x\n",
               iattr->ia_atime, iattr->ia_mtime, iattr->ia_ctime,
               iattr->ia_attr_flags);
        CDEBUG(D_PIOCTL, "offset %d, recno %d, flags %#x\n",
               info->slot_offset, info->recno, info->flags);

        lock_kernel();
        error = presto_walk(name, &nd);
        if (error) {
                EXIT;
                goto exit;
        }
        dentry = nd.dentry;
        
        fset = presto_fset(dentry);
        error = -EINVAL;
        if ( !fset ) {
                printk("No fileset!\n");
                EXIT;
                goto exit_lock;
        }

        /* NOTE: this prevents us from changing the filetype on setattr,
         *       as we normally only want to change permission bits.
         *       If this is not correct, then we need to fix the perl code
         *       to always send the file type OR'ed with the permission.
         */
        if (iattr->ia_valid & ATTR_MODE) {
                int set_mode = iattr->ia_mode;
                iattr->ia_mode = (iattr->ia_mode & S_IALLUGO) |
                                 (dentry->d_inode->i_mode & ~S_IALLUGO);
                CDEBUG(D_PIOCTL, "chmod: orig %#o, set %#o, result %#o\n",
                       dentry->d_inode->i_mode, set_mode, iattr->ia_mode);
#ifdef CONFIG_FS_POSIX_ACL
                /* ACl code interacts badly with setattr 
                 * since it tries to modify the ACL using 
                 * set_ext_attr which recurses back into presto.  
                 * This only happens if ATTR_MODE is set.
                 * Here we are doing a "forced" mode set 
                 * (initiated by lento), so we disable the 
                 * set_posix_acl operation which 
                 * prevents such recursion.  -SHP
                 *
                 * This will probably still be required when native
                 * acl journalling is in place.
                 */
                set_posix_acl=dentry->d_inode->i_op->set_posix_acl;
                dentry->d_inode->i_op->set_posix_acl=NULL;
#endif
        }

        error = presto_do_setattr(fset, dentry, iattr, info);

#ifdef CONFIG_FS_POSIX_ACL
        /* restore the inode_operations if we changed them*/
        if (iattr->ia_valid & ATTR_MODE) 
                dentry->d_inode->i_op->set_posix_acl=set_posix_acl;
#endif


        EXIT;
exit_lock:
        path_release(&nd);
exit:
        unlock_kernel();
        return error;
}

int presto_do_create(struct presto_file_set *fset, struct dentry *dir,
                     struct dentry *dentry, int mode,
                     struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error;
        struct presto_version tgt_dir_ver, new_file_ver;
        struct inode_operations *iops;
        void *handle;

        ENTRY;
        mode &= S_IALLUGO;
        mode |= S_IFREG;

        down(&dir->d_inode->i_zombie);
	error = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH); 
	if (error) {
		EXIT;
        	up(&dir->d_inode->i_zombie);
		return error;
	}

        error = may_create(dir->d_inode, dentry);
        if (error) {
                EXIT;
                goto exit_pre_lock;
        }

        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter);
        if (!iops->create) {
                EXIT;
                goto exit_pre_lock;
        }

        presto_getversion(&tgt_dir_ver, dir->d_inode);
        handle = presto_trans_start(fset, dir->d_inode, PRESTO_OP_CREATE);
        if ( IS_ERR(handle) ) {
                EXIT;
		presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
                printk("presto_do_create: no space for transaction\n");
                error=-ENOSPC;
		goto exit_pre_lock;
        }
        DQUOT_INIT(dir->d_inode);
        lock_kernel();
        error = iops->create(dir->d_inode, dentry, mode);
        if (error) {
                EXIT;
                goto exit_lock;
        }

        if (dentry->d_inode && 
            dentry->d_inode->i_gid != presto_excluded_gid) {
                struct presto_cache *cache = fset->fset_cache;
                /* was this already done? */
                presto_set_ops(dentry->d_inode, cache->cache_filter);

                filter_setup_dentry_ops(cache->cache_filter, 
                                        dentry->d_op, 
                                        &presto_dentry_ops);
                dentry->d_op = filter_c2udops(cache->cache_filter);

                /* if Lento creates this file, we won't have data */
                if ( ISLENTO(presto_c2m(cache)) ) {
                        presto_set(dentry, PRESTO_ATTR);
                } else {
                        presto_set(dentry, PRESTO_ATTR | PRESTO_DATA);
                }
        }

        error = presto_settime(fset, dir, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit_lock;
        }
        error = presto_settime(fset, dentry, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit_lock;
        }


        presto_debug_fail_blkdev(fset, PRESTO_OP_CREATE | 0x10);
        presto_getversion(&new_file_ver, dentry->d_inode);
        if ( presto_do_kml(info, dentry->d_inode) )
                error = presto_journal_create(&rec, fset, dentry, &tgt_dir_ver,
                                              &new_file_ver, 
					      dentry->d_inode->i_mode);

        presto_debug_fail_blkdev(fset, PRESTO_OP_CREATE | 0x20);
        if ( presto_do_expect(info, dentry->d_inode) )
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_CREATE | 0x30);
        EXIT;

 exit_lock:
        unlock_kernel();
        presto_trans_commit(fset, handle);
 exit_pre_lock:
	presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
        up(&dir->d_inode->i_zombie);
        return error;
}

/* from namei.c */
static struct dentry *lookup_create(struct nameidata *nd, int is_dir)
{
        struct dentry *dentry;

        down(&nd->dentry->d_inode->i_sem);
        dentry = ERR_PTR(-EEXIST);
        if (nd->last_type != LAST_NORM)
                goto fail;
        dentry = lookup_hash(&nd->last, nd->dentry);
        if (IS_ERR(dentry))
                goto fail;
        if (!is_dir && nd->last.name[nd->last.len] && !dentry->d_inode)
                goto enoent;
        return dentry;
enoent:
        dput(dentry);
        dentry = ERR_PTR(-ENOENT);
fail:
        return dentry;
}

int lento_create(const char *name, int mode, struct lento_vfs_context *info)
{
        int error;
        struct nameidata nd;
        char * pathname;
        struct dentry *dentry;
        struct presto_file_set *fset;

        ENTRY;
        pathname = getname(name);
        error = PTR_ERR(pathname);
        if (IS_ERR(pathname)) {
                EXIT;
                goto exit;
        }

        /* this looks up the parent */
//        if (path_init(pathname, LOOKUP_FOLLOW | LOOKUP_POSITIVE, &nd))
        if (path_init(pathname,  LOOKUP_PARENT, &nd))
                error = path_walk(pathname, &nd);
        if (error) {
		EXIT;
                goto exit;
	}
        dentry = lookup_create(&nd, 0);
        error = PTR_ERR(dentry);
        if (IS_ERR(dentry)) {
                EXIT;
                goto exit_lock;
        }

        fset = presto_fset(dentry);
        error = -EINVAL;
        if ( !fset ) {
                printk("No fileset!\n");
                EXIT;
                goto exit_lock;
        }
        error = presto_do_create(fset, dentry->d_parent, dentry, (mode&S_IALLUGO)|S_IFREG,
                                 info);

        EXIT;

 exit_lock:
        path_release (&nd);
	dput(dentry); 
        up(&dentry->d_parent->d_inode->i_sem);
        putname(pathname);
exit:
        return error;
}

int presto_do_link(struct presto_file_set *fset, struct dentry *old_dentry,
                   struct dentry *dir, struct dentry *new_dentry,
                   struct lento_vfs_context *info)
{
        struct rec_info rec;
        struct inode *inode;
        int error;
        struct inode_operations *iops;
        struct presto_version tgt_dir_ver;
        struct presto_version new_link_ver;
        void *handle;

        down(&dir->d_inode->i_zombie);
	error = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH); 
	if (error) {
		EXIT;
        	up(&dir->d_inode->i_zombie);
		return error;
	}
        error = -ENOENT;
        inode = old_dentry->d_inode;
        if (!inode)
                goto exit_lock;

        error = may_create(dir->d_inode, new_dentry);
        if (error)
                goto exit_lock;

        error = -EXDEV;
        if (dir->d_inode->i_dev != inode->i_dev)
                goto exit_lock;

        /*
         * A link to an append-only or immutable file cannot be created.
         */
        error = -EPERM;
        if (IS_APPEND(inode) || IS_IMMUTABLE(inode)) {
                EXIT;
                goto exit_lock;
        }

        iops = filter_c2cdiops(fset->fset_cache->cache_filter);
        if (!iops->link) {
                EXIT;
                goto exit_lock;
        }


        presto_getversion(&tgt_dir_ver, dir->d_inode);
        handle = presto_trans_start(fset, dir->d_inode, PRESTO_OP_LINK);
        if ( IS_ERR(handle) ) {
		presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
                printk("presto_do_link: no space for transaction\n");
                return -ENOSPC;
        }

        DQUOT_INIT(dir->d_inode);
        lock_kernel();
        error = iops->link(old_dentry, dir->d_inode, new_dentry);
        unlock_kernel();
        if (error) {
                EXIT;
                goto exit_lock;
        }

        error = presto_settime(fset, dir, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit_lock;
        }
        error = presto_settime(fset, new_dentry, info, ATTR_CTIME);
        if (error) { 
                EXIT;
                goto exit_lock;
        }

        presto_debug_fail_blkdev(fset, PRESTO_OP_LINK | 0x10);
        presto_getversion(&new_link_ver, new_dentry->d_inode);
        if ( presto_do_kml(info, old_dentry->d_inode) )
                error = presto_journal_link(&rec, fset, old_dentry, new_dentry,
                                            &tgt_dir_ver, &new_link_ver);

        presto_debug_fail_blkdev(fset, PRESTO_OP_LINK | 0x20);
        if ( presto_do_expect(info, old_dentry->d_inode) )
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_LINK | 0x30);
        EXIT;
        presto_trans_commit(fset, handle);
exit_lock:
	presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
        up(&dir->d_inode->i_zombie);
        return error;
}


int lento_link(const char * oldname, const char * newname, 
                         struct lento_vfs_context *info)
{
        int error;
        char * from;
        char * to;
        struct presto_file_set *fset;

        from = getname(oldname);
        if(IS_ERR(from))
                return PTR_ERR(from);
        to = getname(newname);
        error = PTR_ERR(to);
        if (!IS_ERR(to)) {
                struct dentry *new_dentry;
                struct nameidata nd, old_nd;

                error = 0;
                if (path_init(from, LOOKUP_POSITIVE, &old_nd))
                        error = path_walk(from, &old_nd);
                if (error)
                        goto exit;
                if (path_init(to, LOOKUP_PARENT, &nd))
                        error = path_walk(to, &nd);
                if (error)
                        goto out;
                error = -EXDEV;
                if (old_nd.mnt != nd.mnt)
                        goto out;
                new_dentry = lookup_create(&nd, 0);
                error = PTR_ERR(new_dentry);

                if (!IS_ERR(new_dentry)) {
                        fset = presto_fset(new_dentry);
                        error = -EINVAL;
                        if ( !fset ) {
                                printk("No fileset!\n");
                                EXIT;
                                goto out2;
                        }
                        error = presto_do_link(fset, old_nd.dentry, 
                                               nd.dentry,
                                               new_dentry, info);
                        dput(new_dentry);
                }
        out2:
                up(&nd.dentry->d_inode->i_sem);
                path_release(&nd);
        out:
                path_release(&old_nd);
        exit:
                putname(to);
        }
        putname(from);

        return error;
}


int presto_do_unlink(struct presto_file_set *fset, struct dentry *dir,
                     struct dentry *dentry, struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error;
        struct inode_operations *iops;
        struct presto_version tgt_dir_ver, old_file_ver;
        void *handle;
        int do_kml = 0, do_expect =0;
	int linkno = 0;
        ENTRY;
        down(&dir->d_inode->i_zombie);
        error = may_delete(dir->d_inode, dentry, 0);
        if (error) {
                EXIT;
                up(&dir->d_inode->i_zombie);
                return error;
        }

        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter);
        if (!iops->unlink) {
                EXIT;
                up(&dir->d_inode->i_zombie);
                return error;
        }

	error = presto_reserve_space(fset->fset_cache, PRESTO_REQLOW); 
	if (error) {
		EXIT;
        	up(&dir->d_inode->i_zombie);
		return error;
	}

        presto_getversion(&tgt_dir_ver, dir->d_inode);
        presto_getversion(&old_file_ver, dentry->d_inode);
        handle = presto_trans_start(fset, dir->d_inode, PRESTO_OP_UNLINK);
        if ( IS_ERR(handle) ) {
		presto_release_space(fset->fset_cache, PRESTO_REQLOW); 
                printk("ERROR: presto_do_unlink: no space for transaction. Tell Peter.\n");
                up(&dir->d_inode->i_zombie);
                return -ENOSPC;
        }
        DQUOT_INIT(dir->d_inode);
        if (d_mountpoint(dentry))
                error = -EBUSY;
        else {
                lock_kernel();
		linkno = dentry->d_inode->i_nlink;
		if (linkno > 1) {
			dget(dentry);
		}
                do_kml = presto_do_kml(info, dir->d_inode);
                do_expect = presto_do_expect(info, dir->d_inode);
                error = iops->unlink(dir->d_inode, dentry);
                unlock_kernel();
                if (!error)
                        d_delete(dentry);
        }

        if (linkno > 1) { 
                error = presto_settime(fset, dentry, info, ATTR_CTIME);
                dput(dentry); 
                if (error) { 
                        EXIT;
                        goto exit;
                }
        }

        error = presto_settime(fset, dir, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit;
        }

        up(&dir->d_inode->i_zombie);
        if (error) {
                EXIT;
                goto exit;
        }

        presto_debug_fail_blkdev(fset, PRESTO_OP_UNLINK | 0x10);
        if ( do_kml ) { 
                error = presto_journal_unlink(&rec, fset, dir, &tgt_dir_ver,
                                              &old_file_ver,
                                              dentry->d_name.len,
                                              dentry->d_name.name);
	}
        presto_debug_fail_blkdev(fset, PRESTO_OP_UNLINK | 0x20);
        if ( do_expect ) { 
                error = presto_write_last_rcvd(&rec, fset, info);
	}
        presto_debug_fail_blkdev(fset, PRESTO_OP_UNLINK | 0x30);
        EXIT;
exit:
	presto_release_space(fset->fset_cache, PRESTO_REQLOW); 
        presto_trans_commit(fset, handle);
        return error;
}


int lento_unlink(const char *pathname, struct lento_vfs_context *info)
{
        int error = 0;
        char * name;
        struct dentry *dentry;
        struct nameidata nd;
        struct presto_file_set *fset;

        ENTRY;

        name = getname(pathname);
        if(IS_ERR(name))
                return PTR_ERR(name);

        if (path_init(name, LOOKUP_PARENT, &nd))
                error = path_walk(name, &nd);
        if (error)
                goto exit;
        error = -EISDIR;
        if (nd.last_type != LAST_NORM)
                goto exit1;
        down(&nd.dentry->d_inode->i_sem);
        dentry = lookup_hash(&nd.last, nd.dentry);
        error = PTR_ERR(dentry);
        if (!IS_ERR(dentry)) {
                fset = presto_fset(dentry);
                error = -EINVAL;
                if ( !fset ) {
                        printk("No fileset!\n");
                        EXIT;
                        goto exit2;
                }
                /* Why not before? Because we want correct error value */
                if (nd.last.name[nd.last.len])
                        goto slashes;
                error = presto_do_unlink(fset, nd.dentry, dentry, info);
        exit2:
                EXIT;
                dput(dentry);
        }
        up(&nd.dentry->d_inode->i_sem);
exit1:
        path_release(&nd);
exit:
        putname(name);

        return error;

slashes:
        error = !dentry->d_inode ? -ENOENT :
                S_ISDIR(dentry->d_inode->i_mode) ? -EISDIR : -ENOTDIR;
        goto exit2;
}

int presto_do_symlink(struct presto_file_set *fset, struct dentry *dir,
                      struct dentry *dentry, const char *oldname,
                      struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error;
        struct presto_version tgt_dir_ver, new_link_ver;
        struct inode_operations *iops;
        void *handle;

        ENTRY;
        down(&dir->d_inode->i_zombie);
	/* record + max path len + space to free */ 
	error = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH + 4096); 
	if (error) {
		EXIT;
        	up(&dir->d_inode->i_zombie);
		return error;
	}

        error = may_create(dir->d_inode, dentry);
        if (error) {
                EXIT;
                goto exit_lock;
        }

        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter);
        if (!iops->symlink) {
                EXIT;
                goto exit_lock;
        }

        presto_getversion(&tgt_dir_ver, dir->d_inode);
        handle = presto_trans_start(fset, dir->d_inode, PRESTO_OP_SYMLINK);
        if ( IS_ERR(handle) ) {
		presto_release_space(fset->fset_cache, PRESTO_REQHIGH + 4096); 
                printk("ERROR: presto_do_symlink: no space for transaction. Tell Peter.\n"); 
                EXIT;
                return -ENOSPC;
        }
        DQUOT_INIT(dir->d_inode);
        lock_kernel();
        error = iops->symlink(dir->d_inode, dentry, oldname);
        if (error) {
                EXIT;
                goto exit;
        }

        if (dentry->d_inode &&
            dentry->d_inode->i_gid != presto_excluded_gid) {
                struct presto_cache *cache = fset->fset_cache;
                
                presto_set_ops(dentry->d_inode, cache->cache_filter);

                filter_setup_dentry_ops(cache->cache_filter, dentry->d_op, 
                                        &presto_dentry_ops);
                dentry->d_op = filter_c2udops(cache->cache_filter);
                /* XXX ? Cache state ? if Lento creates a symlink */
                if ( ISLENTO(presto_c2m(cache)) ) {
                        presto_set(dentry, PRESTO_ATTR);
                } else {
                        presto_set(dentry, PRESTO_ATTR | PRESTO_DATA);
                }
        }

        error = presto_settime(fset, dir, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit;
        }
        error = presto_settime(fset, dentry, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit;
        }

        presto_debug_fail_blkdev(fset, PRESTO_OP_SYMLINK | 0x10);
        presto_getversion(&new_link_ver, dentry->d_inode);
        if ( presto_do_kml(info, dentry->d_inode) )
                error = presto_journal_symlink(&rec, fset, dentry, oldname,
                                               &tgt_dir_ver, &new_link_ver);

        presto_debug_fail_blkdev(fset, PRESTO_OP_SYMLINK | 0x20);
        if ( presto_do_expect(info, dentry->d_inode) )
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_SYMLINK | 0x30);
        EXIT;
exit:
        unlock_kernel();
        presto_trans_commit(fset, handle);
 exit_lock:
	presto_release_space(fset->fset_cache, PRESTO_REQHIGH + 4096); 
        up(&dir->d_inode->i_zombie);
        return error;
}

int lento_symlink(const char *oldname, const char *newname,
                  struct lento_vfs_context *info)
{
        int error;
        char *from;
        char *to;
        struct dentry *dentry;
        struct presto_file_set *fset;
        struct nameidata nd;

        ENTRY;
        lock_kernel();
        from = getname(oldname);
        error = PTR_ERR(from);
        if (IS_ERR(from)) {
                EXIT;
                goto exit;
        }

        to = getname(newname);
        error = PTR_ERR(to);
        if (IS_ERR(to)) {
                EXIT;
                goto exit_from;
        }

        if (path_init(to, LOOKUP_PARENT, &nd)) 
                error = path_walk(to, &nd);
        if (error) {
                EXIT;
                goto exit_to;
        }

        dentry = lookup_create(&nd, 0);
        error = PTR_ERR(dentry);
        if (IS_ERR(dentry)) {
        	path_release(&nd);
                EXIT;
                goto exit_to;
        }

        fset = presto_fset(dentry);
        error = -EINVAL;
        if ( !fset ) {
                printk("No fileset!\n");
        	path_release(&nd);
                EXIT;
                goto exit_lock;
        }
        error = presto_do_symlink(fset, nd.dentry,
                                  dentry, oldname, info);
        path_release(&nd);
        EXIT;
 exit_lock:
        up(&nd.dentry->d_inode->i_sem);
        dput(dentry);
 exit_to:
        putname(to);
 exit_from:
        putname(from);
 exit:
        unlock_kernel();
        return error;
}

int presto_do_mkdir(struct presto_file_set *fset, struct dentry *dir,
                    struct dentry *dentry, int mode,
                    struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error;
        struct presto_version tgt_dir_ver, new_dir_ver;
        void *handle;

        ENTRY;
        down(&dir->d_inode->i_zombie);
	/* one journal record + directory block + room for removals*/ 
	error = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH + 4096); 
	if (error) { 
                EXIT;
        	up(&dir->d_inode->i_zombie);
                return error;
        }

        error = may_create(dir->d_inode, dentry);
        if (error) {
                EXIT;
                goto exit_lock;
        }

        error = -EPERM;
        if (!filter_c2cdiops(fset->fset_cache->cache_filter)->mkdir) {
                EXIT;
                goto exit_lock;
        }

        error = -ENOSPC;
        presto_getversion(&tgt_dir_ver, dir->d_inode);
        handle = presto_trans_start(fset, dir->d_inode, PRESTO_OP_MKDIR);
        if ( IS_ERR(handle) ) {
		presto_release_space(fset->fset_cache, PRESTO_REQHIGH + 4096); 
                printk("presto_do_mkdir: no space for transaction\n");
                goto exit_lock;
        }

        DQUOT_INIT(dir->d_inode);
        mode &= (S_IRWXUGO|S_ISVTX);
        lock_kernel();
        error = filter_c2cdiops(fset->fset_cache->cache_filter)->mkdir(dir->d_inode, dentry, mode);
        if (error) {
                EXIT;
                goto exit;
        }

        if ( dentry->d_inode && !error && 
             dentry->d_inode->i_gid != presto_excluded_gid) {
                struct presto_cache *cache = fset->fset_cache;

                presto_set_ops(dentry->d_inode, cache->cache_filter);

                filter_setup_dentry_ops(cache->cache_filter, 
                                        dentry->d_op, 
                                        &presto_dentry_ops);
                dentry->d_op = filter_c2udops(cache->cache_filter);
                /* if Lento does this, we won't have data */
                if ( ISLENTO(presto_c2m(cache)) ) {
                        presto_set(dentry, PRESTO_ATTR);
                } else {
                        presto_set(dentry, PRESTO_ATTR | PRESTO_DATA);
                }
        }

        error = presto_settime(fset, dir, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit;
        }
        error = presto_settime(fset, dentry, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit;
        }

        presto_debug_fail_blkdev(fset, PRESTO_OP_MKDIR | 0x10);
        presto_getversion(&new_dir_ver, dentry->d_inode);
        if ( presto_do_kml(info, dentry->d_inode) )
                error = presto_journal_mkdir(&rec, fset, dentry, &tgt_dir_ver,
                                             &new_dir_ver, 
					     dentry->d_inode->i_mode);

        presto_debug_fail_blkdev(fset, PRESTO_OP_MKDIR | 0x20);
        if ( presto_do_expect(info, dentry->d_inode) )
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_MKDIR | 0x30);
        EXIT;
exit:
        unlock_kernel();
        presto_trans_commit(fset, handle);
 exit_lock:
	presto_release_space(fset->fset_cache, PRESTO_REQHIGH + 4096); 
        up(&dir->d_inode->i_zombie);
        return error;
}

/*
 * Look out: this function may change a normal dentry
 * into a directory dentry (different size)..
 */
int lento_mkdir(const char *name, int mode, struct lento_vfs_context *info)
{
        int error;
        char *pathname;
        struct dentry *dentry;
        struct presto_file_set *fset;
        struct nameidata nd;

        ENTRY;
        CDEBUG(D_PIOCTL, "name: %s, mode %o, offset %d, recno %d, flags %x\n",
               name, mode, info->slot_offset, info->recno, info->flags);
        pathname = getname(name);
        error = PTR_ERR(pathname);
        if (IS_ERR(pathname)) {
                EXIT;
                return error;
        }

        if (path_init(pathname, LOOKUP_PARENT, &nd))
                error = path_walk(pathname, &nd);
        if (error)
                goto out_name;

        dentry = lookup_create(&nd, 1);
        error = PTR_ERR(dentry);
        if (!IS_ERR(dentry)) {
                fset = presto_fset(dentry);
                error = -EINVAL;
                if ( !fset ) {
                        printk("No fileset!\n");
                        EXIT;
                        goto out_dput;
                }

                error = presto_do_mkdir(fset, nd.dentry, dentry, 
                                        mode & S_IALLUGO, info);
out_dput:
		dput(dentry);
        }
	up(&nd.dentry->d_inode->i_sem);
	path_release(&nd);
out_name:
        EXIT;
        putname(pathname);
	CDEBUG(D_PIOCTL, "error: %d\n", error);
        return error;
}

static void d_unhash(struct dentry *dentry)
{
        dget(dentry);
        switch (atomic_read(&dentry->d_count)) {
        default:
                shrink_dcache_parent(dentry);
                if (atomic_read(&dentry->d_count) != 2)
                        break;
        case 2:
                d_drop(dentry);
        }
}

int presto_do_rmdir(struct presto_file_set *fset, struct dentry *dir,
                    struct dentry *dentry, struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error;
        struct presto_version tgt_dir_ver, old_dir_ver;
        struct inode_operations *iops;
        void *handle;
        int do_kml, do_expect;
	int size;

        ENTRY;
        error = may_delete(dir->d_inode, dentry, 1);
        if (error)
                return error;

        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter);
        if (!iops->rmdir) {
                EXIT;
                return error;
        }

	size = PRESTO_REQHIGH - dentry->d_inode->i_size; 
	error = presto_reserve_space(fset->fset_cache, size); 
	if (error) { 
		EXIT;
		return error;
	}

        presto_getversion(&tgt_dir_ver, dir->d_inode);
        presto_getversion(&old_dir_ver, dentry->d_inode);
        handle = presto_trans_start(fset, dir->d_inode, PRESTO_OP_RMDIR);
        if ( IS_ERR(handle) ) {
		presto_release_space(fset->fset_cache, size); 
                printk("ERROR: presto_do_rmdir: no space for transaction. Tell Peter.\n");
                return -ENOSPC;
        }

        DQUOT_INIT(dir->d_inode);

        do_kml = presto_do_kml(info, dir->d_inode);
        do_expect = presto_do_expect(info, dir->d_inode);

        double_down(&dir->d_inode->i_zombie, &dentry->d_inode->i_zombie);
        d_unhash(dentry);
        if (IS_DEADDIR(dir->d_inode))
                error = -ENOENT;
        else if (d_mountpoint(dentry))
                error = -EBUSY;
        else {
                lock_kernel();
                error = iops->rmdir(dir->d_inode, dentry);
                unlock_kernel();
                if (!error) {
                        dentry->d_inode->i_flags |= S_DEAD;
			error = presto_settime(fset, dir, info, 
					       ATTR_CTIME | ATTR_MTIME);
		}
        }
        double_up(&dir->d_inode->i_zombie, &dentry->d_inode->i_zombie);
        if (!error)
                d_delete(dentry);
        dput(dentry);

        presto_debug_fail_blkdev(fset, PRESTO_OP_RMDIR | 0x10);
        if ( !error && do_kml )
                error = presto_journal_rmdir(&rec, fset, dir, &tgt_dir_ver,
                                             &old_dir_ver,
                                             dentry->d_name.len,
                                             dentry->d_name.name);

        presto_debug_fail_blkdev(fset, PRESTO_OP_RMDIR | 0x20);
        if ( !error && do_expect ) 
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_RMDIR | 0x30);
        EXIT;

        presto_trans_commit(fset, handle);
	presto_release_space(fset->fset_cache, size); 
        return error;
}

int lento_rmdir(const char *pathname, struct lento_vfs_context *info)
{
        int error = 0;
        char * name;
        struct dentry *dentry;
        struct presto_file_set *fset;
        struct nameidata nd;

        ENTRY;
        name = getname(pathname);
        if(IS_ERR(name))
                return PTR_ERR(name);

        if (path_init(name, LOOKUP_PARENT, &nd))
                error = path_walk(name, &nd);
        if (error)
                goto exit;

        switch(nd.last_type) {
                case LAST_DOTDOT:
                        error = -ENOTEMPTY;
                        goto exit1;
                case LAST_ROOT: case LAST_DOT:
                        error = -EBUSY;
                        goto exit1;
        }
        down(&nd.dentry->d_inode->i_sem);
        dentry = lookup_hash(&nd.last, nd.dentry);
        error = PTR_ERR(dentry);
        if (!IS_ERR(dentry)) {
                fset = presto_fset(dentry);
                error = -EINVAL;
                if ( !fset ) {
                        printk("No fileset!\n");
                        EXIT;
                        goto exit_put;
                }
                error = presto_do_rmdir(fset, nd.dentry, dentry, info);
        exit_put:
                dput(dentry);
        }
        up(&nd.dentry->d_inode->i_sem);
exit1:
        EXIT;
        path_release(&nd);
exit:
        EXIT;
        putname(name);
        return error;
}

int presto_do_mknod(struct presto_file_set *fset, struct dentry *dir,
                    struct dentry *dentry, int mode, dev_t dev,
                    struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error = -EPERM;
        struct presto_version tgt_dir_ver, new_node_ver;
        struct inode_operations *iops;
        void *handle;

        ENTRY;

        down(&dir->d_inode->i_zombie);
	/* one KML entry */ 
	error = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH); 
	if (error) {
		EXIT;
        	up(&dir->d_inode->i_zombie);
		return error;
	}

        if ((S_ISCHR(mode) || S_ISBLK(mode)) && !capable(CAP_MKNOD)) {
                EXIT;
                goto exit_lock;
        }

        error = may_create(dir->d_inode, dentry);
        if (error) {
                EXIT;
                goto exit_lock;
        }

        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter);
        if (!iops->mknod) {
                EXIT;
                goto exit_lock;
        }

        DQUOT_INIT(dir->d_inode);
        lock_kernel();
        
        error = -ENOSPC;
        presto_getversion(&tgt_dir_ver, dir->d_inode);
        handle = presto_trans_start(fset, dir->d_inode, PRESTO_OP_MKNOD);
        if ( IS_ERR(handle) ) {
		presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
                printk("presto_do_mknod: no space for transaction\n");
                goto exit_lock2;
        }

        error = iops->mknod(dir->d_inode, dentry, mode, dev);
        if ( dentry->d_inode &&
             dentry->d_inode->i_gid != presto_excluded_gid) {
                struct presto_cache *cache = fset->fset_cache;

                presto_set_ops(dentry->d_inode, cache->cache_filter);

                filter_setup_dentry_ops(cache->cache_filter, dentry->d_op, 
                                        &presto_dentry_ops);
                dentry->d_op = filter_c2udops(cache->cache_filter);

                /* if Lento does this, we won't have data */
                if ( ISLENTO(presto_c2m(cache)) ) {
                        presto_set(dentry, PRESTO_ATTR);
                } else {
                        presto_set(dentry, PRESTO_ATTR | PRESTO_DATA);
                }
        }

        error = presto_settime(fset, dir, info, ATTR_MTIME);
        if (error) { 
                EXIT;
        }
        error = presto_settime(fset, dentry, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
        }

        presto_debug_fail_blkdev(fset, PRESTO_OP_MKNOD | 0x10);
        presto_getversion(&new_node_ver, dentry->d_inode);
        if ( presto_do_kml(info, dentry->d_inode) )
                error = presto_journal_mknod(&rec, fset, dentry, &tgt_dir_ver,
                                             &new_node_ver, 
					     dentry->d_inode->i_mode,
                                             MAJOR(dev), MINOR(dev) );

        presto_debug_fail_blkdev(fset, PRESTO_OP_MKNOD | 0x20);
        if ( presto_do_expect(info, dentry->d_inode) ) 
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_MKNOD | 0x30);
        EXIT;
        presto_trans_commit(fset, handle);
 exit_lock2:
        unlock_kernel();
 exit_lock:
	presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
        up(&dir->d_inode->i_zombie);
        return error;
}

int lento_mknod(const char *filename, int mode, dev_t dev,
                struct lento_vfs_context *info)
{
        int error = 0;
        char * tmp;
        struct dentry * dentry;
        struct nameidata nd;
        struct presto_file_set *fset;

        ENTRY;

        if (S_ISDIR(mode))
                return -EPERM;
        tmp = getname(filename);
        if (IS_ERR(tmp))
                return PTR_ERR(tmp);

        if (path_init(tmp, LOOKUP_PARENT, &nd))
                error = path_walk(tmp, &nd);
        if (error)
                goto out;
        dentry = lookup_create(&nd, 0);
        error = PTR_ERR(dentry);
        if (!IS_ERR(dentry)) {
                fset = presto_fset(dentry);
                error = -EINVAL;
                if ( !fset ) {
                        printk("No fileset!\n");
                        EXIT;
                        goto exit_put;
                }
                switch (mode & S_IFMT) {
                case 0: case S_IFREG:
                        error = -EOPNOTSUPP;
                        break;
                case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
                        error = presto_do_mknod(fset, nd.dentry, dentry, 
                                                mode, dev, info);
                        break;
                case S_IFDIR:
                        error = -EPERM;
                        break;
                default:
                        error = -EINVAL;
                }
        exit_put:
                dput(dentry);
        }
        up(&nd.dentry->d_inode->i_sem);
        path_release(&nd);
out:
        putname(tmp);

        return error;
}

static int do_rename(struct presto_file_set *fset,
                     struct dentry *old_parent, struct dentry *old_dentry,
                     struct dentry *new_parent, struct dentry *new_dentry,
                     struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error;
        struct inode_operations *iops;
        struct presto_version src_dir_ver, tgt_dir_ver;
        void *handle;
	int new_inode_unlink = 0;
        struct inode *old_dir = old_parent->d_inode;
        struct inode *new_dir = new_parent->d_inode;

        ENTRY;
        presto_getversion(&src_dir_ver, old_dir);
        presto_getversion(&tgt_dir_ver, new_dir);

        error = -EPERM;
        iops = filter_c2cdiops(fset->fset_cache->cache_filter);
        if (!iops || !iops->rename) {
                EXIT;
                return error;
        }

	error = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH); 
	if (error) {
		EXIT;
		return error;
	}
        handle = presto_trans_start(fset, old_dir, PRESTO_OP_RENAME);
        if ( IS_ERR(handle) ) {
		presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
                printk("presto_do_rename: no space for transaction\n");
                return -ENOSPC;
        }
        if (new_dentry->d_inode && new_dentry->d_inode->i_nlink > 1) { 
                dget(new_dentry); 
                new_inode_unlink = 1;
        }

        error = iops->rename(old_dir, old_dentry, new_dir, new_dentry);

        if (error) {
                EXIT;
                goto exit;
        }

        if (new_inode_unlink) { 
                error = presto_settime(fset, old_dentry, info, ATTR_CTIME);
                dput(old_dentry); 
                if (error) { 
                        EXIT;
                        goto exit;
                }
        }
        error = presto_settime(fset, old_parent, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit;
        }
        error = presto_settime(fset, new_parent, info, ATTR_CTIME | ATTR_MTIME);
        if (error) { 
                EXIT;
                goto exit;
        }

        /* XXX make a distinction between cross file set
         * and intra file set renames here
         */
        presto_debug_fail_blkdev(fset, PRESTO_OP_RENAME | 0x10);
        if ( presto_do_kml(info, old_dir) )
                error = presto_journal_rename(&rec, fset, old_dentry, new_dentry,
                                              &src_dir_ver, &tgt_dir_ver);

        presto_debug_fail_blkdev(fset, PRESTO_OP_RENAME | 0x20);

        if ( presto_do_expect(info, new_dir) ) 
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_RENAME | 0x30);
        EXIT;
exit:
        presto_trans_commit(fset, handle);
	presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
        return error;
}

static
int presto_rename_dir(struct presto_file_set *fset, struct dentry *old_parent,
                      struct dentry *old_dentry, struct dentry *new_parent,
                      struct dentry *new_dentry, struct lento_vfs_context *info)
{
        int error;
        struct inode *target;
        struct inode *old_dir = old_parent->d_inode;
        struct inode *new_dir = new_parent->d_inode;

        if (old_dentry->d_inode == new_dentry->d_inode)
                return 0;

        error = may_delete(old_dir, old_dentry, 1);
        if (error)
                return error;

        if (new_dir->i_dev != old_dir->i_dev)
                return -EXDEV;

        if (!new_dentry->d_inode)
                error = may_create(new_dir, new_dentry);
        else
                error = may_delete(new_dir, new_dentry, 1);
        if (error)
                return error;

        if (!old_dir->i_op || !old_dir->i_op->rename)
                return -EPERM;

        /*
         * If we are going to change the parent - check write permissions,
         * we'll need to flip '..'.
         */
        if (new_dir != old_dir) {
                error = permission(old_dentry->d_inode, MAY_WRITE);
        }
        if (error)
                return error;

        DQUOT_INIT(old_dir);
        DQUOT_INIT(new_dir);
        down(&old_dir->i_sb->s_vfs_rename_sem);
        error = -EINVAL;
        if (is_subdir(new_dentry, old_dentry))
                goto out_unlock;
        target = new_dentry->d_inode;
        if (target) { /* Hastur! Hastur! Hastur! */
                triple_down(&old_dir->i_zombie,
                            &new_dir->i_zombie,
                            &target->i_zombie);
                d_unhash(new_dentry);
        } else
                double_down(&old_dir->i_zombie,
                            &new_dir->i_zombie);
        if (IS_DEADDIR(old_dir)||IS_DEADDIR(new_dir))
                error = -ENOENT;
        else if (d_mountpoint(old_dentry)||d_mountpoint(new_dentry))
                error = -EBUSY;
        else 
                error = do_rename(fset, old_parent, old_dentry,
                                         new_parent, new_dentry, info);
        if (target) {
                if (!error)
                        target->i_flags |= S_DEAD;
                triple_up(&old_dir->i_zombie,
                          &new_dir->i_zombie,
                          &target->i_zombie);
                if (d_unhashed(new_dentry))
                        d_rehash(new_dentry);
                dput(new_dentry);
        } else
                double_up(&old_dir->i_zombie,
                          &new_dir->i_zombie);
                
        if (!error)
                d_move(old_dentry,new_dentry);
out_unlock:
        up(&old_dir->i_sb->s_vfs_rename_sem);
        return error;
}

static
int presto_rename_other(struct presto_file_set *fset, struct dentry *old_parent,
                        struct dentry *old_dentry, struct dentry *new_parent,
                        struct dentry *new_dentry, struct lento_vfs_context *info)
{
        struct inode *old_dir = old_parent->d_inode;
        struct inode *new_dir = new_parent->d_inode;
        int error;

        if (old_dentry->d_inode == new_dentry->d_inode)
                return 0;

        error = may_delete(old_dir, old_dentry, 0);
        if (error)
                return error;

        if (new_dir->i_dev != old_dir->i_dev)
                return -EXDEV;

        if (!new_dentry->d_inode)
                error = may_create(new_dir, new_dentry);
        else
                error = may_delete(new_dir, new_dentry, 0);
        if (error)
                return error;

        if (!old_dir->i_op || !old_dir->i_op->rename)
                return -EPERM;

        DQUOT_INIT(old_dir);
        DQUOT_INIT(new_dir);
        double_down(&old_dir->i_zombie, &new_dir->i_zombie);
        if (d_mountpoint(old_dentry)||d_mountpoint(new_dentry))
                error = -EBUSY;
        else
                error = do_rename(fset, old_parent, old_dentry,
                                         new_parent, new_dentry, info);
        double_up(&old_dir->i_zombie, &new_dir->i_zombie);
        if (error)
                return error;
        /* The following d_move() should become unconditional */
        if (!(old_dir->i_sb->s_type->fs_flags & FS_ODD_RENAME)) {
                d_move(old_dentry, new_dentry);
        }
        return 0;
}

int presto_do_rename(struct presto_file_set *fset, 
              struct dentry *old_parent, struct dentry *old_dentry,
              struct dentry *new_parent, struct dentry *new_dentry,
              struct lento_vfs_context *info)
{
        if (S_ISDIR(old_dentry->d_inode->i_mode))
                return presto_rename_dir(fset, old_parent,old_dentry,new_parent,
                                      new_dentry, info);
        else
                return presto_rename_other(fset, old_parent, old_dentry,
                                           new_parent,new_dentry, info);
}


int lento_do_rename(const char *oldname, const char *newname,
                 struct lento_vfs_context *info)
{
        int error = 0;
        struct dentry * old_dir, * new_dir;
        struct dentry * old_dentry, *new_dentry;
        struct nameidata oldnd, newnd;
        struct presto_file_set *fset;

        ENTRY;

        if (path_init(oldname, LOOKUP_PARENT, &oldnd))
                error = path_walk(oldname, &oldnd);

        if (error)
                goto exit;

        if (path_init(newname, LOOKUP_PARENT, &newnd))
                error = path_walk(newname, &newnd);
        if (error)
                goto exit1;

        error = -EXDEV;
        if (oldnd.mnt != newnd.mnt)
                goto exit2;

        old_dir = oldnd.dentry;
        error = -EBUSY;
        if (oldnd.last_type != LAST_NORM)
                goto exit2;

        new_dir = newnd.dentry;
        if (newnd.last_type != LAST_NORM)
                goto exit2;

        double_lock(new_dir, old_dir);

        old_dentry = lookup_hash(&oldnd.last, old_dir);
        error = PTR_ERR(old_dentry);
        if (IS_ERR(old_dentry))
                goto exit3;
        /* source must exist */
        error = -ENOENT;
        if (!old_dentry->d_inode)
                goto exit4;
        fset = presto_fset(old_dentry);
        error = -EINVAL;
        if ( !fset ) {
                printk("No fileset!\n");
                EXIT;
                goto exit4;
        }
        /* unless the source is a directory trailing slashes give -ENOTDIR */
        if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
                error = -ENOTDIR;
                if (oldnd.last.name[oldnd.last.len])
                        goto exit4;
                if (newnd.last.name[newnd.last.len])
                        goto exit4;
        }
        new_dentry = lookup_hash(&newnd.last, new_dir);
        error = PTR_ERR(new_dentry);
        if (IS_ERR(new_dentry))
                goto exit4;

        lock_kernel();
        error = presto_do_rename(fset, old_dir, old_dentry,
                                   new_dir, new_dentry, info);
        unlock_kernel();

        dput(new_dentry);
exit4:
        dput(old_dentry);
exit3:
        double_up(&new_dir->d_inode->i_sem, &old_dir->d_inode->i_sem);
exit2:
        path_release(&newnd);
exit1:
        path_release(&oldnd);
exit:
        return error;
}

int  lento_rename(const char * oldname, const char * newname,
                  struct lento_vfs_context *info)
{
        int error;
        char * from;
        char * to;

        from = getname(oldname);
        if(IS_ERR(from))
                return PTR_ERR(from);
        to = getname(newname);
        error = PTR_ERR(to);
        if (!IS_ERR(to)) {
                error = lento_do_rename(from,to, info);
                putname(to);
        }
        putname(from);
        return error;
}

struct dentry *presto_iopen(struct dentry *dentry,
                            ino_t ino, unsigned int generation)
{
        struct presto_file_set *fset;
        char name[48];
        int error;

        ENTRY;
        /* see if we already have the dentry we want */
        if (dentry->d_inode && dentry->d_inode->i_ino == ino &&
            dentry->d_inode->i_generation == generation) {
                EXIT;
                return dentry;
        }

        /* Make sure we have a cache beneath us.  We should always find at
         * least one dentry inside the cache (if it exists), otherwise not
         * even the cache root exists, or we passed in a bad name.
         */
        fset = presto_fset(dentry);
        error = -EINVAL;
        if (!fset) {
                printk("No fileset for %*s!\n",
                       dentry->d_name.len, dentry->d_name.name);
                EXIT;
                dput(dentry);
                return ERR_PTR(error);
        }
        dput(dentry);

        sprintf(name, "%s%#lx%c%#x",
                PRESTO_ILOOKUP_MAGIC, ino, PRESTO_ILOOKUP_SEP, generation);
        CDEBUG(D_PIOCTL, "opening %ld by number (as %s)\n", ino, name);
        return lookup_one_len(name, fset->fset_mtpt, strlen(name));
}

static struct file *presto_filp_dopen(struct dentry *dentry, int flags)
{
        struct file *f;
        struct inode *inode;
        int flag, error;

        ENTRY;
        error = -ENFILE;
        f = get_empty_filp();
        if (!f) {
                CDEBUG(D_PIOCTL, "error getting file pointer\n");
                EXIT;
                goto out;
        }
        f->f_flags = flag = flags;
        f->f_mode = (flag+1) & O_ACCMODE;
        inode = dentry->d_inode;
        if (f->f_mode & FMODE_WRITE) {
                error = get_write_access(inode);
                if (error) {
                        CDEBUG(D_PIOCTL, "error getting write access\n");
                        EXIT;
                        goto cleanup_file;
                }
        }

        f->f_dentry = dentry;
        f->f_pos = 0;
        f->f_reada = 0;
        f->f_op = NULL;
        if (inode->i_op)
                /* XXX should we set to presto ops, or leave at cache ops? */
                f->f_op = inode->i_fop;
        if (f->f_op && f->f_op->open) {
                error = f->f_op->open(inode, f);
                if (error) {
                        CDEBUG(D_PIOCTL, "error calling cache 'open'\n");
                        EXIT;
                        goto cleanup_all;
                }
        }
        f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

        return f;

cleanup_all:
        if (f->f_mode & FMODE_WRITE)
                put_write_access(inode);
cleanup_file:
        put_filp(f);
out:
        return ERR_PTR(error);
}


/* Open an inode by number.  We pass in the cache root name (or a subdirectory
 * from the cache that is guaranteed to exist) to be able to access the cache.
 */
int lento_iopen(const char *name, ino_t ino, unsigned int generation,
                int flags)
{
        char * tmp;
        struct dentry *dentry;
        struct nameidata nd;
        int fd;
        int error;

        ENTRY;
        CDEBUG(D_PIOCTL,
               "open %s:inode %#lx (%ld), generation %x (%d), flags %d \n",
               name, ino, ino, generation, generation, flags);
        /* We don't allow creation of files by number only, as it would
         * lead to a dangling files not in any directory.  We could also
         * just turn off the flag and ignore it.
         */
        if (flags & O_CREAT) {
                printk(KERN_WARNING __FUNCTION__
                       ": create file by inode number (%ld) not allowed\n",ino);
                EXIT;
                return -EACCES;
        }

        tmp = getname(name);
        if (IS_ERR(tmp)) {
                EXIT;
                return PTR_ERR(tmp);
        }

        lock_kernel();
again:  /* look the named file or a parent directory so we can get the cache */
        error = presto_walk(tmp, &nd);
        if ( error && error != -ENOENT ) {
                EXIT;
                return error;
        } 
        if (error == -ENOENT)
                dentry = NULL;
        else 
                dentry = nd.dentry;

        /* we didn't find the named file, so see if a parent exists */
        if (!dentry) {
                char *slash;

                slash = strrchr(tmp, '/');
                if (slash && slash != tmp) {
                        *slash = '\0';
			path_release(&nd);
                        goto again;
                }
                /* we should never get here... */
                CDEBUG(D_PIOCTL, "no more path components to try!\n");
                fd = -ENOENT;
                goto exit;
        }
        CDEBUG(D_PIOCTL, "returned dentry %p\n", dentry);

        dentry = presto_iopen(dentry, ino, generation);
        fd = PTR_ERR(dentry);
        if (IS_ERR(dentry)) {
                EXIT;
                goto exit;
        }

        /* XXX start of code that might be replaced by something like:
         * if (flags & (O_WRONLY | O_RDWR)) {
         *      error = get_write_access(dentry->d_inode);
         *      if (error) {
         *              EXIT;
         *              goto cleanup_dput;
         *      }
         * }
         * fd = open_dentry(dentry, flags);
         *
         * including the presto_filp_dopen() function (check dget counts!)
         */
        fd = get_unused_fd();
        if (fd < 0) {
                EXIT;
                goto cleanup_dput;
        }

        {
                int error;
                struct file * f = presto_filp_dopen(dentry, flags);
                error = PTR_ERR(f);
                if (IS_ERR(f)) {
                        put_unused_fd(fd);
                        fd = error;
                        EXIT;
                        goto cleanup_dput;
                }
                fd_install(fd, f);
        }
        /* end of code that might be replaced by open_dentry */

        EXIT;
exit:
        unlock_kernel();
	path_release(&nd);
        putname(tmp);
        return fd;

cleanup_dput:
        putname(&nd);
        goto exit;
}

int lento_close(unsigned int fd, struct lento_vfs_context *info)
{
        struct rec_info rec;
        int error;
        struct file * filp;
        struct dentry *dentry;
        int do_kml, do_expect;

        ENTRY;
        lock_kernel();

        error = -EBADF;
        filp = fcheck(fd);
        if (filp) {

                struct files_struct * files = current->files;
                dentry = filp->f_dentry;
                dget(dentry);
                do_kml = presto_do_kml(info, dentry->d_inode);
                do_expect = presto_do_expect(info, dentry->d_inode);
                files->fd[fd] = NULL;
                put_unused_fd(fd);
                FD_CLR(fd, files->close_on_exec);
                error = filp_close(filp, files);
        } else {
                EXIT;
                return error;
        }

        if (error) {
                EXIT;
                goto exit;
        }

        if ( do_kml ) { 
                struct presto_file_set *fset;
                struct presto_version new_file_ver;

                fset = presto_fset(dentry);
                error = -EINVAL;
                if (!fset) {
                        printk("No fileset for %*s!\n",
                               dentry->d_name.len, dentry->d_name.name);
                        EXIT;
                        goto exit;
                }
                presto_getversion(&new_file_ver, dentry->d_inode);
                error = presto_journal_close(&rec, fset, filp, dentry, 
					     &new_file_ver);
                if ( error ) {
                        printk("presto: close error %d!\n", error);
                        EXIT;
                        goto exit;
                }
                if ( do_expect ) 

                        error = presto_write_last_rcvd(&rec, fset, info);
        }

        EXIT;
exit:
        dput(dentry);
        unlock_kernel();
        return error;
}

#ifdef CONFIG_FS_EXT_ATTR

#ifdef CONFIG_FS_POSIX_ACL
/* Posix ACL code changes i_mode without using a notify_change (or
 * a mark_inode_dirty!). We need to duplicate this at the reintegrator
 * which is done by this function. This function also takes care of 
 * resetting the cached posix acls in this inode. If we don't reset these
 * VFS continues using the old acl information, which by now may be out of
 * date.
 */
int presto_setmode(struct presto_file_set *fset, struct dentry *dentry,
                   mode_t mode)
{
        struct inode *inode = dentry->d_inode;

        ENTRY;
        /* The extended attributes for this inode were modified. 
         * At this point we can not be sure if any of the ACL 
         * information for this inode was updated. So we will 
         * force VFS to reread the acls. Note that we do this 
         * only when called from the SETEXTATTR ioctl, which is why we
         * do this while setting the mode of the file. Also note
         * that mark_inode_dirty is not be needed for i_*acl only
         * to force i_mode info to disk, and should be removed once
         * we use notify_change to update the mode.
         * XXX: is mode setting really needed? Just setting acl's should
         * be enough! VFS should change the i_mode as needed? SHP
         */
        if (inode->i_acl && 
            inode->i_acl != POSIX_ACL_NOT_CACHED) 
            posix_acl_release(inode->i_acl);
        if (inode->i_default_acl && 
            inode->i_default_acl != POSIX_ACL_NOT_CACHED) 
            posix_acl_release(inode->i_default_acl);
        inode->i_acl = POSIX_ACL_NOT_CACHED;
        inode->i_default_acl = POSIX_ACL_NOT_CACHED;
        inode->i_mode = mode;
        /* inode should already be dirty...but just in case */
        mark_inode_dirty(inode);
        return 0;

#if 0
        /* XXX: The following code is the preferred way to set mode, 
         * however, I need to carefully go through possible recursion
         * paths back into presto. See comments in presto_do_setattr.
         */
        {    
        int error=0; 
        struct super_operations *sops;
        struct iattr iattr;

        iattr.ia_mode = mode;
        iattr.ia_valid = ATTR_MODE|ATTR_FORCE;

        error = -EPERM;
        sops = filter_c2csops(fset->fset_cache->cache_filter); 
        if (!sops &&
            !sops->notify_change) {
                EXIT;
                return error;
        }

        error = sops->notify_change(dentry, &iattr);

        EXIT;
        return error;
        }
#endif
}
#endif

/* setextattr Interface to cache filesystem */
int presto_do_set_ext_attr(struct presto_file_set *fset, 
                           struct dentry *dentry, 
                           const char *name, void *buffer,
                           size_t buffer_len, int flags, mode_t *mode,
                           struct lento_vfs_context *info) 
{
        struct rec_info rec;
        struct inode *inode = dentry->d_inode;
        struct inode_operations *iops;
        int error;
        struct presto_version ver;
        void *handle;
        char temp[PRESTO_EXT_ATTR_NAME_MAX+1];

        ENTRY;
        error = -EROFS;
        if (IS_RDONLY(inode)) {
                EXIT;
                return -EROFS;
        }

        if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
                EXIT;
                return -EPERM;
        }

        presto_getversion(&ver, inode);
        error = -EPERM;
        /* We need to invoke different filters based on whether
         * this dentry is a regular file, directory or symlink.
         */
        switch (inode->i_mode & S_IFMT) {
                case S_IFLNK: /* symlink */
                    iops = filter_c2csiops(fset->fset_cache->cache_filter); 
                    break;
                case S_IFDIR: /* directory */
                    iops = filter_c2cdiops(fset->fset_cache->cache_filter); 
                    break;
                case S_IFREG:
                default: /* everything else including regular files */
                    iops = filter_c2cfiops(fset->fset_cache->cache_filter); 
        }

        if (!iops && !iops->set_ext_attr) {
                EXIT;
                return error;
        }

        error = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH); 
        if (error) {
                EXIT;
                return error;
        }

        
        handle = presto_trans_start(fset,dentry->d_inode,PRESTO_OP_SETEXTATTR);
        if ( IS_ERR(handle) ) {
                printk("presto_do_set_ext_attr: no space for transaction\n");
                presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
                return -ENOSPC;
        }

        /* We first "truncate" name to the maximum allowable in presto */
        /* This simulates the strncpy_from_use code in fs/ext_attr.c */
        strncpy(temp,name,sizeof(temp));

        /* Pass down to cache*/
        error = iops->set_ext_attr(inode,temp,buffer,buffer_len,flags);
        if (error) {
                EXIT;
                goto exit;
        }

#ifdef CONFIG_FS_POSIX_ACL
        /* Reset mode if specified*/
        /* XXX: when we do native acl support, move this code out! */
        if (mode != NULL) {
                error = presto_setmode(fset, dentry, *mode);
                if (error) { 
                    EXIT;
                    goto exit;
                }
        }
#endif

        /* Reset ctime. Only inode change time (ctime) is affected */
        error = presto_settime(fset, dentry, info, ATTR_CTIME);
        if (error) { 
                EXIT;
                goto exit;
        }

        if (flags & EXT_ATTR_FLAG_USER) {
                printk(" USER flag passed to presto_do_set_ext_attr!\n");
                *(int *)0 = 1;
        }

        /* We are here, so set_ext_attr succeeded. We no longer need to keep
         * track of EXT_ATTR_FLAG_{EXISTS,CREATE}, instead, we will force
         * the attribute value during log replay. -SHP
         */
        flags &= ~(EXT_ATTR_FLAG_EXISTS | EXT_ATTR_FLAG_CREATE);

        presto_debug_fail_blkdev(fset, PRESTO_OP_SETEXTATTR | 0x10);
        if ( presto_do_kml(info, dentry->d_inode) )
                error = presto_journal_set_ext_attr
                        (&rec, fset, dentry, &ver, name, buffer, 
                         buffer_len, flags);

        presto_debug_fail_blkdev(fset, PRESTO_OP_SETEXTATTR | 0x20);
        if ( presto_do_expect(info, dentry->d_inode) )
                error = presto_write_last_rcvd(&rec, fset, info);

        presto_debug_fail_blkdev(fset, PRESTO_OP_SETEXTATTR | 0x30);
        EXIT;
exit:
        presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 
        presto_trans_commit(fset, handle);

        return error;
}
#endif

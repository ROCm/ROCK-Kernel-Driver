/*
 *
 *
 *  Copyright (C) 2000 Stelias Computing, Inc.
 *  Copyright (C) 2000 Red Hat, Inc.
 *  Copyright (C) 2000 Tacitus Systems
 *  Copyright (C) 2000 Peter J. Braam
 *
 */


#include <stdarg.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/smp_lock.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#define __NO_VERSION__
#include <linux/module.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>

static inline void presto_relock_sem(struct inode *dir) 
{
	/* the lock from sys_mkdir / lookup_create */
	down(&dir->i_sem);
	/* the rest is done by the do_{create,mkdir, ...} */
}

static inline void presto_relock_other(struct inode *dir) 
{
	/* vfs_mkdir locks */
        down(&dir->i_zombie);
	lock_kernel(); 
}

static inline void presto_fulllock(struct inode *dir) 
{
	/* the lock from sys_mkdir / lookup_create */
	down(&dir->i_sem);
	/* vfs_mkdir locks */
        down(&dir->i_zombie);
	lock_kernel(); 
}

static inline void presto_unlock(struct inode *dir) 
{
	/* vfs_mkdir locks */
	unlock_kernel(); 
        up(&dir->i_zombie);
	/* the lock from sys_mkdir / lookup_create */
	up(&dir->i_sem);
}


/*
 * these are initialized in super.c
 */
extern int presto_permission(struct inode *inode, int mask);
int presto_ilookup_uid = 0;

extern int presto_prep(struct dentry *, struct presto_cache **,
                       struct presto_file_set **);

static int dentry2id(struct dentry *dentry, ino_t *id, unsigned int *generation)
{
        char *tmpname;
        char *next;
        int error = 0;

        ENTRY;
        if (dentry->d_name.len > EXT2_NAME_LEN) {
                EXIT;
                return -ENAMETOOLONG;
        }

        /* prefix is 7 characters: '...ino:' */
        if ( dentry->d_name.len < 7 ||
             memcmp(dentry->d_name.name, PRESTO_ILOOKUP_MAGIC, 7) != 0 ) {
                EXIT;
                return 1;
        }

        PRESTO_ALLOC(tmpname, char *, dentry->d_name.len - 7 + 1);
        if ( !tmpname ) {
                EXIT;
                return -ENOMEM;
        }

        memcpy(tmpname, dentry->d_name.name + 7, dentry->d_name.len - 7);
        *(tmpname + dentry->d_name.len) = '\0';

        /* name is of the form <inode number>:<generation> */
        *id = simple_strtoul(tmpname, &next, 0);
        if ( *next == PRESTO_ILOOKUP_SEP ) {
                *generation = simple_strtoul(next + 1, 0, 0);
                CDEBUG(D_INODE, "INO to find = %s\n", tmpname);
                CDEBUG(D_INODE, "Id = %lx (%lu), generation %x (%d)\n",
                       *id, *id, *generation, *generation);
        } else
                error = 1;

        PRESTO_FREE(tmpname, dentry->d_name.len - 7 + 1);
        EXIT;
        return error;
}

static int presto_opendir_upcall(int minor, struct dentry *de, 
                          struct dentry *root, int async)
{
        int rc;
        char *path, *buffer;
        int pathlen;

        PRESTO_ALLOC(buffer, char *, PAGE_SIZE);
        if ( !buffer ) {
                printk("PRESTO: out of memory!\n");
                return ENOMEM;
        }
        path = presto_path(de, root, buffer, PAGE_SIZE);
        pathlen = MYPATHLEN(buffer, path);
        CDEBUG(D_INODE, "path: %*s, len %d\n", pathlen, path, pathlen);
        rc = lento_opendir(minor, pathlen, path, async);
        PRESTO_FREE(buffer, PAGE_SIZE);
        return rc;
}

inline int presto_can_ilookup(void)
{
        return (current->euid == presto_ilookup_uid ||
                capable(CAP_DAC_READ_SEARCH));
}

struct dentry *presto_ilookup(struct inode *dir, struct dentry *dentry,
                            ino_t ino, unsigned int generation)
{
        struct inode *inode;
        int error;

        ENTRY;

        /* if we can't ilookup, forbid anything with this name to
         * avoid any security issues/name clashes.
         */
        if ( !presto_can_ilookup() ) {
                CDEBUG(D_CACHE, "ilookup denied: euid %u, ilookup_uid %u\n",
                       current->euid, presto_ilookup_uid);
                EXIT;
                return ERR_PTR(-EPERM);
        }
        inode = iget(dir->i_sb, ino);
        if (!inode || is_bad_inode(inode)) {
                CDEBUG(D_PIOCTL, "fatal: invalid inode %ld (%s).\n",
                       ino, inode ? inode->i_nlink ? "bad inode" :
                       "no links" : "NULL");
                error = -ENOENT;
                EXIT;
                goto cleanup_iput;
        } else if (inode->i_nlink == 0) {
                /* This is quite evil, but we have little choice.  If we were
                 * to iput() again with i_nlink == 0, delete_inode would get
                 * called again, which ext3 really Does Not Like. */
                atomic_dec(&inode->i_count);
                EXIT;
                return ERR_PTR(-ENOENT);
        }

        /* We need to make sure we have the right inode (by checking the
         * generation) so we don't write into the wrong file (old inode was
         * deleted and then a new one was created with the same number).
         */
        if (inode->i_generation != generation) {
                CDEBUG(D_PIOCTL, "fatal: bad generation %u (want %u)\n",
                       inode->i_generation, generation);
                error = -ENOENT;
                EXIT;
                goto cleanup_iput;
        }

        d_instantiate(dentry, inode);
        dentry->d_flags |= DCACHE_NFSD_DISCONNECTED; /* NFS hack */

        EXIT;
        return NULL;

cleanup_iput:
        if (inode)
                iput(inode);
        return ERR_PTR(error);
}


struct dentry *presto_lookup(struct inode * dir, struct dentry *dentry)
{
        int rc = 0;
        struct dentry *de;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        int error; 
        int minor;
        ino_t ino;
        unsigned int generation;

        ENTRY;
        CDEBUG(D_CACHE, "calling presto_prep on dentry %p\n", dentry);
        error = presto_prep(dentry->d_parent, &cache, &fset);
        if ( error  ) {
                EXIT;
                return ERR_PTR(error);
        }
        minor = presto_c2m(cache);

        CDEBUG(D_CACHE, "dir ino: %ld, name: %*s\n",
               dir->i_ino, dentry->d_name.len, dentry->d_name.name);
        if ( ISLENTO(minor) )
                CDEBUG(D_CACHE, "We are lento\n");

        rc = dentry2id(dentry, &ino, &generation);
        CDEBUG(D_CACHE, "dentry2id returned %d\n", rc);
        if ( rc < 0 ) {
                EXIT;
                goto exit;
        }

        if ( rc == 0 ) {
                de = presto_ilookup(dir, dentry, ino, generation);
        } else {
                struct inode_operations *iops = filter_c2cdiops(cache->cache_filter);
                rc = 0;
                /* recursively do a cache lookup in dir */
                if (iops && iops->lookup) 
                        de = iops->lookup(dir, dentry);
                else {
		        printk("filesystem has no lookup\n");
                        EXIT;
                        goto exit;
                }
        }
        /* XXX this needs some work to handle returning de if we get it */
        filter_setup_dentry_ops(cache->cache_filter, 
                                dentry->d_op, &presto_dentry_ops);
        dentry->d_op = filter_c2udops(cache->cache_filter);
        if ( IS_ERR(de) ) {
                rc = PTR_ERR(de);
                CDEBUG(D_CACHE, "dentry lookup error %d\n", rc);
                EXIT;
                goto exit;
        }

        presto_set_dd(dentry);

        /* some file systems set the methods in lookup, not in
           read_inode, as a result we should set the methods here 
           as well as in read_inode 
        */
	if (dentry->d_inode) {
		presto_set_ops(dentry->d_inode, cache->cache_filter); 
	}
        EXIT;
exit:
        return ERR_PTR(rc);
}

int presto_setattr(struct dentry *de, struct iattr *iattr)
{
        int error;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        struct lento_vfs_context info = { 0, 0, 0 };

        ENTRY;
        error = presto_prep(de, &cache, &fset);
        if ( error ) {
                EXIT;
                return error;
        }

        if (!iattr->ia_valid)
                CDEBUG(D_INODE, "presto_setattr: iattr is not valid\n");

        CDEBUG(D_INODE, "valid %#x, mode %#o, uid %u, gid %u, size %Lu, "
               "atime %lu mtime %lu ctime %lu flags %d\n",
               iattr->ia_valid, iattr->ia_mode, iattr->ia_uid, iattr->ia_gid,
               iattr->ia_size, iattr->ia_atime, iattr->ia_mtime,
               iattr->ia_ctime, iattr->ia_attr_flags);
        
        if ( presto_get_permit(de->d_inode) < 0 ) {
                EXIT;
                return -EROFS;
        }

        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_setattr(fset, de, iattr, &info);
        presto_put_permit(de->d_inode);
        return error;
}

/*
 *  Now the meat: the fs operations that require journaling
 *
 *
 *  XXX: some of these need modifications for hierarchical filesets
 */

int presto_prep(struct dentry *dentry, struct presto_cache **cache,
                struct presto_file_set **fset)
{
        *fset = presto_fset(dentry);
        if ( !*fset ) {
                CDEBUG(D_INODE, "No file set for dentry at %p\n", dentry);
                return -EROFS;
        }

        *cache = (*fset)->fset_cache;
        if ( !*cache ) {
                printk("PRESTO: BAD, BAD: cannot find cache\n");
                return -EBADF;
        }

        CDEBUG(D_PIOCTL, "---> cache flags %x, fset flags %x\n",
              (*cache)->cache_flags, (*fset)->fset_flags);
        if( presto_is_read_only(*fset) ) {
                printk("PRESTO: cannot modify read-only fileset, minor %d.\n",
                       presto_c2m(*cache));
                return -EROFS;
        }
        return 0;
}

static int presto_create(struct inode * dir, struct dentry * dentry, int mode)
{
        int error;
        struct presto_cache *cache;
        struct dentry *parent = dentry->d_parent;
        struct lento_vfs_context info;
        struct presto_file_set *fset;

        ENTRY;
        error = presto_prep(dentry->d_parent, &cache, &fset);
        if ( error ) {
                EXIT;
                return error;
        }
	presto_unlock(dir);

        /* Does blocking and non-blocking behavious need to be 
           checked for.  Without blocking (return 1), the permit
           was acquired without reintegration
        */
        if ( presto_get_permit(dir) < 0 ) {
                EXIT;
                presto_fulllock(dir);
                return -EROFS;
        }

        presto_relock_sem(dir);
	parent = dentry->d_parent; 
        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_create(fset, parent, dentry, mode, &info);
        presto_relock_other(dir);
        presto_put_permit(dir);
        EXIT;
        return error;
}

static int presto_link(struct dentry *old_dentry, struct inode *dir,
                struct dentry *new_dentry)
{
        int error;
        struct presto_cache *cache, *new_cache;
        struct presto_file_set *fset, *new_fset;
        struct dentry *parent = new_dentry->d_parent;
        struct lento_vfs_context info;

        ENTRY;
        error = presto_prep(old_dentry, &cache, &fset);
        if ( error ) {
                EXIT;
                return error;
        }

        error = presto_prep(new_dentry->d_parent, &new_cache, &new_fset);
        if ( error ) {
                EXIT;
                return error;
        }

        if (fset != new_fset) { 
                EXIT;
                return -EXDEV;
        }

        presto_unlock(dir);
        if ( presto_get_permit(old_dentry->d_inode) < 0 ) {
                EXIT;
                presto_fulllock(dir);
                return -EROFS;
        }

        if ( presto_get_permit(dir) < 0 ) {
                EXIT;
                presto_fulllock(dir);
                return -EROFS;
        }

	presto_relock_sem(dir);
        parent = new_dentry->d_parent;

        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_link(fset, old_dentry, parent,
                               new_dentry, &info);
        presto_relock_other(dir);
        presto_put_permit(dir);
        presto_put_permit(old_dentry->d_inode);
        return error;
}

static int presto_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
        int error;
        struct presto_file_set *fset;
        struct presto_cache *cache;
        struct dentry *parent = dentry->d_parent;
        struct lento_vfs_context info;

        ENTRY;

        error = presto_prep(dentry->d_parent, &cache, &fset);
        if ( error  ) {
                EXIT;
                return error;
        }

	presto_unlock(dir); 

        if ( presto_get_permit(dir) < 0 ) {
                EXIT;
                presto_fulllock(dir);
                return -EROFS;
        }

        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;

	presto_relock_sem(dir); 
	parent = dentry->d_parent;
        error = presto_do_mkdir(fset, parent, dentry, mode, &info);
	presto_relock_other(dir); 
        presto_put_permit(dir);
        return error;
}


static int presto_symlink(struct inode *dir, struct dentry *dentry,
                   const char *name)
{
        int error;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        struct dentry *parent = dentry->d_parent;
        struct lento_vfs_context info;

        ENTRY;
        error = presto_prep(dentry->d_parent, &cache, &fset);
        if ( error ) {
                EXIT;
                return error;
        }

        presto_unlock(dir);
        if ( presto_get_permit(dir) < 0 ) {
                EXIT;
		presto_fulllock(dir);
                return -EROFS;
        }

	presto_relock_sem(dir);
        parent = dentry->d_parent;
        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_symlink(fset, parent, dentry, name, &info);
        presto_relock_other(dir);
        presto_put_permit(dir);
        return error;
}

int presto_unlink(struct inode *dir, struct dentry *dentry)
{
        int error;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        struct dentry *parent = dentry->d_parent;
        struct lento_vfs_context info;

        ENTRY;
        error = presto_prep(dentry->d_parent, &cache, &fset);
        if ( error  ) {
                EXIT;
                return error;
        }

        presto_unlock(dir);
        if ( presto_get_permit(dir) < 0 ) {
                EXIT;
		presto_fulllock(dir);
                return -EROFS;
        }

	presto_relock_sem(dir);
        parent = dentry->d_parent;
        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_unlink(fset, parent, dentry, &info);
        presto_relock_other(dir);
        presto_put_permit(dir);
        return error;
}

static int presto_rmdir(struct inode *dir, struct dentry *dentry)
{
        int error;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        struct dentry *parent = dentry->d_parent;
        struct lento_vfs_context info;

        ENTRY;
        CDEBUG(D_FILE, "prepping presto\n");
        error = presto_prep(dentry->d_parent, &cache, &fset);
        if ( error ) {
                EXIT;
                return error;
        }

        CDEBUG(D_FILE, "unlocking\n");
        /* We need to dget() before the dput in double_unlock, to ensure we
         * still have dentry references.  double_lock doesn't do dget for us.
         */
	unlock_kernel();
	if (d_unhashed(dentry))
		d_rehash(dentry);
        double_up(&dir->i_zombie, &dentry->d_inode->i_zombie);
        double_up(&dir->i_sem, &dentry->d_inode->i_sem);

        CDEBUG(D_FILE, "getting permit\n");
        if ( presto_get_permit(parent->d_inode) < 0 ) {
                EXIT;
		double_down(&dir->i_sem, &dentry->d_inode->i_sem);
		double_down(&dir->i_zombie, &dentry->d_inode->i_zombie);
		
		lock_kernel();
                return -EROFS;
        }
        CDEBUG(D_FILE, "locking\n");

	double_down(&dir->i_sem, &dentry->d_inode->i_sem);
	parent = dentry->d_parent;
        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_rmdir(fset, parent, dentry, &info);
        presto_put_permit(parent->d_inode);
	lock_kernel();
        EXIT;
        return error;
}

static int presto_mknod(struct inode * dir, struct dentry * dentry, int mode, int rdev)
{
        int error;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        struct dentry *parent = dentry->d_parent;
        struct lento_vfs_context info;

        ENTRY;
        error = presto_prep(dentry->d_parent, &cache, &fset);
        if ( error  ) {
                EXIT;
                return error;
        }

        presto_unlock(dir);
        if ( presto_get_permit(dir) < 0 ) {
                EXIT;
                presto_fulllock(dir);
                return -EROFS;
        }
	
	presto_relock_sem(dir);
        parent = dentry->d_parent;
        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_mknod(fset, parent, dentry, mode, rdev, &info);
        presto_relock_other(dir);
        presto_put_permit(dir);
        EXIT;
        return error;
}

inline void presto_triple_unlock(struct inode *old_dir, struct inode *new_dir, 
				 struct dentry *old_dentry, 
				 struct dentry *new_dentry, int triple)
{
	/* rename_dir case */ 
	if (S_ISDIR(old_dentry->d_inode->i_mode)) { 
		if (triple) {			
			triple_up(&old_dir->i_zombie,
				  &new_dir->i_zombie,
				  &new_dentry->d_inode->i_zombie);
		} else { 
			double_up(&old_dir->i_zombie,
				  &new_dir->i_zombie);
		}
		up(&old_dir->i_sb->s_vfs_rename_sem);
	} else /* this case is rename_other */
		double_up(&old_dir->i_zombie, &new_dir->i_zombie);
	/* done by do_rename */
	unlock_kernel();
	double_up(&old_dir->i_sem, &new_dir->i_sem);
}

inline void presto_triple_fulllock(struct inode *old_dir, 
				   struct inode *new_dir, 
				   struct dentry *old_dentry, 
				   struct dentry *new_dentry, int triple)
{
	/* done by do_rename */
	double_down(&old_dir->i_sem, &new_dir->i_sem);
	lock_kernel();
	/* rename_dir case */ 
	if (S_ISDIR(old_dentry->d_inode->i_mode)) { 
		down(&old_dir->i_sb->s_vfs_rename_sem);
		if (triple) {			
			triple_down(&old_dir->i_zombie,
				  &new_dir->i_zombie,
				  &new_dentry->d_inode->i_zombie);
		} else { 
			double_down(&old_dir->i_zombie,
				  &new_dir->i_zombie);
		}
	} else /* this case is rename_other */
		double_down(&old_dir->i_zombie, &new_dir->i_zombie);
}

inline void presto_triple_relock_sem(struct inode *old_dir, 
				   struct inode *new_dir, 
				   struct dentry *old_dentry, 
				   struct dentry *new_dentry, int triple)
{
	/* done by do_rename */
	double_down(&old_dir->i_sem, &new_dir->i_sem);
	lock_kernel();
}

inline void presto_triple_relock_other(struct inode *old_dir, 
				   struct inode *new_dir, 
				   struct dentry *old_dentry, 
				   struct dentry *new_dentry, int triple)
{
	/* rename_dir case */ 
	if (S_ISDIR(old_dentry->d_inode->i_mode)) { 
		down(&old_dir->i_sb->s_vfs_rename_sem);
		if (triple) {			
			triple_down(&old_dir->i_zombie,
				  &new_dir->i_zombie,
				  &new_dentry->d_inode->i_zombie);
		} else { 
			double_down(&old_dir->i_zombie,
				  &new_dir->i_zombie);
		}
	} else /* this case is rename_other */
		double_down(&old_dir->i_zombie, &new_dir->i_zombie);
}


// XXX this can be optimized: renamtes across filesets only require 
//     multiple KML records, but can locally be executed normally. 
int presto_rename(struct inode *old_dir, struct dentry *old_dentry,
                  struct inode *new_dir, struct dentry *new_dentry)
{
        int error;
        struct presto_cache *cache, *new_cache;
        struct presto_file_set *fset, *new_fset;
        struct lento_vfs_context info;
        struct dentry *old_parent = old_dentry->d_parent;
        struct dentry *new_parent = new_dentry->d_parent;
        int triple;

        ENTRY;
        error = presto_prep(old_dentry, &cache, &fset);
        if ( error ) {
                EXIT;
                return error;
        }
        error = presto_prep(new_parent, &new_cache, &new_fset);
        if ( error ) {
                EXIT;
                return error;
        }

        if ( fset != new_fset ) {
                EXIT;
                return -EXDEV;
        }

        /* We need to do dget before the dput in double_unlock, to ensure we
         * still have dentry references.  double_lock doesn't do dget for us.
         */

        triple = (S_ISDIR(old_dentry->d_inode->i_mode) && new_dentry->d_inode)?
                1:0;

	presto_triple_unlock(old_dir, new_dir, old_dentry, new_dentry, triple); 

        if ( presto_get_permit(old_dir) < 0 ) {
                EXIT;
		presto_triple_fulllock(old_dir, new_dir, old_dentry, new_dentry, triple); 
                return -EROFS;
        }
        if ( presto_get_permit(new_dir) < 0 ) {
                EXIT;
		presto_triple_fulllock(old_dir, new_dir, old_dentry, new_dentry, triple); 
                return -EROFS;
        }

	presto_triple_relock_sem(old_dir, new_dir, old_dentry, new_dentry, triple); 
        memset(&info, 0, sizeof(info));
        if (!ISLENTO(presto_c2m(cache)))
                info.flags = LENTO_FL_KML;
	info.flags |= LENTO_FL_IGNORE_TIME;
        error = presto_do_rename(fset, old_parent, old_dentry, new_parent,
                                 new_dentry, &info);
	presto_triple_relock_other(old_dir, new_dir, old_dentry, new_dentry, triple); 

        presto_put_permit(new_dir);
        presto_put_permit(old_dir);
        return error;
}

/* basically this allows the ilookup processes access to all files for
 * reading, while not making ilookup totally insecure.  This could all
 * go away if we could set the CAP_DAC_READ_SEARCH capability for the client.
 */
/* If posix acls are available, the underlying cache fs will export the
 * appropriate permission function. Thus we do not worry here about ACLs
 * or EAs. -SHP
 */
int presto_permission(struct inode *inode, int mask)
{
        unsigned short mode = inode->i_mode;
        struct presto_cache *cache;
        int rc;

        ENTRY;
        if ( presto_can_ilookup() && !(mask & S_IWOTH)) {
                CDEBUG(D_CACHE, "ilookup on %ld OK\n", inode->i_ino);
                EXIT;
                return 0;
        }

        cache = presto_get_cache(inode);

        if ( cache ) {
                /* we only override the file/dir permission operations */
                struct inode_operations *fiops = filter_c2cfiops(cache->cache_filter);
                struct inode_operations *diops = filter_c2cdiops(cache->cache_filter);

                if ( S_ISREG(mode) && fiops && fiops->permission ) {
                        EXIT;
                        return fiops->permission(inode, mask);
                }
                if ( S_ISDIR(mode) && diops && diops->permission ) {
                        EXIT;
                        return diops->permission(inode, mask);
                }
        }

        /* The cache filesystem doesn't have its own permission function,
         * but we don't want to duplicate the VFS code here.  In order
         * to avoid looping from permission calling this function again,
         * we temporarily override the permission operation while we call
         * the VFS permission function.
         */
        inode->i_op->permission = NULL;
        rc = permission(inode, mask);
        inode->i_op->permission = &presto_permission;

        EXIT;
        return rc;
}


static int presto_dir_open(struct inode *inode, struct file *file)
{
        int rc = 0;
        struct dentry *de = file->f_dentry;
        struct file_operations *fops;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        int minor;
        int error; 

        ENTRY;

        error = presto_prep(file->f_dentry, &cache, &fset);
        if ( error  ) {
                EXIT;
                make_bad_inode(inode);
                return error;
        }
        minor = presto_c2m(cache);

        CDEBUG(D_CACHE, "minor %d, DATA_OK: %d, ino: %ld\n",
               minor, presto_chk(de, PRESTO_DATA), inode->i_ino);

        if ( ISLENTO(minor) )
                goto cache;

        if ( !presto_chk(de, PRESTO_DATA) ) {
                CDEBUG(D_CACHE, "doing lento_opendir\n");
                rc = presto_opendir_upcall(minor, file->f_dentry, fset->fset_mtpt, SYNCHRONOUS);
        }

        if ( rc ) {
                printk("presto_dir_open: DATA_OK: %d, ino: %ld, error %d\n",
                       presto_chk(de, PRESTO_DATA), inode->i_ino, rc);
                return rc ;
        }

 cache:
        fops = filter_c2cdfops(cache->cache_filter);
        if ( fops->open ) {
                rc = fops->open(inode, file);
        }
        presto_set(de, PRESTO_DATA | PRESTO_ATTR);
        CDEBUG(D_CACHE, "returns %d, data %d, attr %d\n", rc,
               presto_chk(de, PRESTO_DATA), presto_chk(de, PRESTO_ATTR));
        return 0;
}

struct file_operations presto_dir_fops = {
        open: presto_dir_open
};

struct inode_operations presto_dir_iops = {
        create: presto_create,
        lookup: presto_lookup,
        link:   presto_link,
        unlink: presto_unlink,
        symlink:        presto_symlink,
        mkdir:  presto_mkdir,
        rmdir:  presto_rmdir,
        mknod:  presto_mknod,
        rename: presto_rename,
        permission:     presto_permission,
        setattr:        presto_setattr,
#ifdef CONFIG_FS_EXT_ATTR
	set_ext_attr:	presto_set_ext_attr,
#endif

};

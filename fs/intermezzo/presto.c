/*
 * intermezzo.c
 *
 * This file implements basic routines supporting the semantics
 *
 * Author: Peter J. Braam  <braam@cs.cmu.edu>
 * Copyright (C) 1998 Stelias Computing Inc
 * Copyright (C) 1999 Red Hat Inc.
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/locks.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/smp_lock.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>
#include <linux/intermezzo_kml.h>

extern int presto_init_last_rcvd_file(struct presto_file_set *);
extern int presto_init_lml_file(struct presto_file_set *);
extern int presto_init_kml_file(struct presto_file_set *);

int presto_walk(const char *name, struct nameidata *nd)
{
        int err;
        /* we do not follow symlinks to support symlink operations 
           correctly. The vfs should always hand us resolved dentries
           so we should not be required to use LOOKUP_FOLLOW. At the
	   reintegrating end, lento again should be working with the 
           resolved pathname and not the symlink. SHP
           XXX: This code implies that direct symlinks do not work. SHP
        */
        unsigned int flags = LOOKUP_POSITIVE;

        ENTRY;
        err = 0;
        if (path_init(name, flags, nd)) 
                err = path_walk(name, nd);
        return err;
}

inline struct presto_dentry_data *presto_d2d(struct dentry *dentry)
{
        return (struct presto_dentry_data *)dentry->d_fsdata;
}

static inline struct presto_file_set *presto_dentry2fset(struct dentry *dentry)
{
        if (dentry->d_fsdata == NULL) {
                printk("fucked dentry: %p\n", dentry);
                BUG();
        }
        return presto_d2d(dentry)->dd_fset;
}

/* find the presto minor device for this inode */
int presto_i2m(struct inode *inode)
{
        struct presto_cache *cache;
        ENTRY;
        cache = presto_get_cache(inode);
        CDEBUG(D_PSDEV, "\n");
        if ( !cache ) {
                printk("PRESTO: BAD: cannot find cache for dev %d, ino %ld\n",
                       inode->i_dev, inode->i_ino);
                EXIT;
                return -1;
        }
        EXIT;
        return cache->cache_psdev->uc_minor;
}

inline int presto_f2m(struct presto_file_set *fset)
{
        return fset->fset_cache->cache_psdev->uc_minor;

}

inline int presto_c2m(struct presto_cache *cache)
{
        return cache->cache_psdev->uc_minor;

}

int presto_has_all_data(struct inode *inode)
{
        ENTRY;

        if ( (inode->i_size >> inode->i_sb->s_blocksize_bits) >
             inode->i_blocks) {
                EXIT;
                return 0;
        }
        EXIT;
        return 1;

}

/* find the fileset dentry for this dentry */
struct presto_file_set *presto_fset(struct dentry *de)
{
        struct dentry *fsde;
        ENTRY;
        fsde = de;
        for ( ; ; ) {
                if ( presto_dentry2fset(fsde) ) {
                        EXIT;
                        return presto_dentry2fset(fsde);
                }
                /* are we at the cache "/" ?? */
                if ( fsde->d_parent == fsde ) {
                        if ( !de->d_inode ) {
                                printk("Warning %*s has no fileset inode.\n",
                                       de->d_name.len, de->d_name.name);
                        }
                        /* better to return a BAD thing */
                        EXIT;
                        return NULL;
                }
                fsde = fsde->d_parent;
        }
        /* not reached */
        EXIT;
        return NULL;
}

/* XXX check this out */
struct presto_file_set *presto_path2fileset(const char *name)
{
        struct nameidata nd;
        struct presto_file_set *fileset;
        int error;
        ENTRY;

        error = presto_walk(name, &nd);
        if (!error) { 
#if 0
                error = do_revalidate(nd.dentry);
#endif
                if (!error) 
                        fileset = presto_fset(nd.dentry); 
                path_release(&nd); 
                EXIT;
        } else 
                fileset = ERR_PTR(error);

        EXIT;
        return fileset;
}

/* check a flag on this dentry or fset root.  Semantics:
   - most flags: test if it is set
   - PRESTO_ATTR, PRESTO_DATA return 1 if PRESTO_FSETINSYNC is set
*/
int presto_chk(struct dentry *dentry, int flag)
{
        int minor;
        struct presto_file_set *fset = presto_fset(dentry);

        ENTRY;
        minor = presto_i2m(dentry->d_inode);
        if ( upc_comms[minor].uc_no_filter ) {
                EXIT;
                return ~0;
        }

        /* if the fileset is in sync DATA and ATTR are OK */
        if ( fset &&
             (flag == PRESTO_ATTR || flag == PRESTO_DATA) &&
             (fset->fset_flags & FSET_INSYNC) ) {
                CDEBUG(D_INODE, "fset in sync (ino %ld)!\n",
                       fset->fset_mtpt->d_inode->i_ino);
                EXIT;
                return 1;
        }

        EXIT;
        return (presto_d2d(dentry)->dd_flags & flag);
}

/* set a bit in the dentry flags */
void presto_set(struct dentry *dentry, int flag)
{

        ENTRY;
        if ( dentry->d_inode ) {
                CDEBUG(D_INODE, "SET ino %ld, flag %x\n",
                       dentry->d_inode->i_ino, flag);
        }
        presto_d2d(dentry)->dd_flags |= flag;
        EXIT;
}

/* given a path: complete the closes on the fset */
int lento_complete_closes(char *path)
{
        struct nameidata nd;
        struct dentry *dentry;
        int error;
        struct presto_file_set *fset;
        ENTRY;


        error = presto_walk(path, &nd);
        if (error) {
                EXIT;
                return error;
        }

        dentry = nd.dentry;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto out_complete;
        }
        
        fset = presto_fset(dentry);
        error = -EINVAL;
        if ( !fset ) {
                printk("No fileset!\n");
                EXIT;
                goto out_complete;
        }
        
        /* transactions and locking are internal to this function */ 
        error = presto_complete_lml(fset);
        
        EXIT;
 out_complete:
        path_release(&nd); 
        return error;
}       

/* set the fset recno and offset to a given value */ 
int lento_reset_fset(char *path, __u64 offset, __u32 recno)
{
        struct nameidata nd;
        struct dentry *dentry;
        int error;
        struct presto_file_set *fset;
        ENTRY;


        error = presto_walk(path, &nd);
        if (error)
                return error;

        dentry = nd.dentry;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto out_complete;
        }
        
        fset = presto_fset(dentry);
        error = -EINVAL;
        if ( !fset ) {
                printk("No fileset!\n");
                EXIT;
                goto out_complete;
        }

        write_lock(&fset->fset_kml.fd_lock);
        fset->fset_kml.fd_recno = recno;
        fset->fset_kml.fd_offset = offset;
        read_lock(&fset->fset_kml.fd_lock);
        
        EXIT;
 out_complete:
        path_release(&nd);
        return error;
}       



/* given a path, write an LML record for it - thus must have root's 
   group array settings, since lento is doing this 
*/ 
int lento_write_lml(char *path,
                     __u64 remote_ino, 
                     __u32 remote_generation,
                     __u32 remote_version,
                     struct presto_version *remote_file_version)
{
        struct nameidata nd; 
        struct rec_info rec;
        struct dentry *dentry;
        struct file file;
        int error;
        struct presto_file_set *fset;
        ENTRY;

        error = presto_walk(path, &nd);
        if (error) {
                EXIT;
                return error;
        }
        dentry = nd.dentry;

        file.f_dentry = dentry;
        file.private_data = NULL;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto out_lml;
        }
        
        fset = presto_fset(dentry);
        error = -EINVAL;
        if ( !fset ) {
                printk("No fileset!\n");
                EXIT;
                goto out_lml;
        }

        
        /* setting offset to -1 appends */
        rec.offset = -1;
        /* this only requires a transaction below which is automatic */
        error = presto_write_lml_close(&rec, 
                                       fset,
                                       &file, 
                                       remote_ino,
                                       remote_generation,
                                       remote_version,
                                       remote_file_version);
        
        EXIT;
 out_lml:
        path_release(&nd);
        return error;
}       

/* given a path: write a close record and cancel an LML record, finally
   call truncate LML.  Lento is doing this so it goes in with uid/gid's 
   root. 
*/ 
int lento_cancel_lml(char *path, 
                     __u64 lml_offset, 
                     __u64 remote_ino, 
                     __u32 remote_generation,
                     __u32 remote_version, 
                     struct lento_vfs_context *info)
{
        struct nameidata nd;
        struct rec_info rec;
        struct dentry *dentry;
        int error;
        struct presto_file_set *fset;
        void *handle; 
        struct presto_version new_ver;
        ENTRY;


        error = presto_walk(path, &nd);
        if (error) {
                EXIT;
                return error;
        }
        dentry = nd.dentry;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto out_cancel_lml;
        }
        
        fset = presto_fset(dentry);

        error=-EINVAL;
        if (fset==NULL) {
                printk("No fileset!\n");
                EXIT;
                goto out_cancel_lml;
        }
        
        /* this only requires a transaction below which is automatic */
        handle = presto_trans_start(fset, dentry->d_inode, PRESTO_OP_RELEASE); 
        if ( !handle ) {
                error = -ENOMEM; 
                EXIT; 
                goto out_cancel_lml; 
        } 
        
        if (info->flags & LENTO_FL_CANCEL_LML) {
                error = presto_clear_lml_close(fset, lml_offset);
                if ( error ) {
                        presto_trans_commit(fset, handle);
                        EXIT; 
                        goto out_cancel_lml;
                }
        }


        if (info->flags & LENTO_FL_WRITE_KML) {
                struct file file;
                file.private_data = NULL;
                file.f_dentry = dentry; 
                presto_getversion(&new_ver, dentry->d_inode);
                error = presto_journal_close(&rec, fset, &file, dentry, 
                                             &new_ver);
                if ( error ) {
                        EXIT; 
                        presto_trans_commit(fset, handle);
                        goto out_cancel_lml;
                }
        }

        if (info->flags & LENTO_FL_WRITE_EXPECT) {
                error = presto_write_last_rcvd(&rec, fset, info); 
                if ( error ) {
                        EXIT; 
                        presto_trans_commit(fset, handle);
                        goto out_cancel_lml;
                }
        }

        presto_trans_commit(fset, handle);

        if (info->flags & LENTO_FL_CANCEL_LML) {
            presto_truncate_lml(fset); 
        }
                

 out_cancel_lml:
        EXIT;
        path_release(&nd); 
        return error;
}       


/* given a path, operate on the flags in its dentry.  Used by downcalls */
int presto_mark_dentry(const char *name, int and_flag, int or_flag, 
                       int *res)
{
        struct nameidata nd;
        struct dentry *dentry;
        int error;

        error = presto_walk(name, &nd);
        if (error)
                return error;
        dentry = nd.dentry;

        CDEBUG(D_INODE, "name: %s, and flag %x, or flag %x, dd_flags %x\n",
               name, and_flag, or_flag, presto_d2d(dentry)->dd_flags);


        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) )
                goto out;

        error = 0;

        presto_d2d(dentry)->dd_flags  &= and_flag;
        presto_d2d(dentry)->dd_flags  |= or_flag;
        if (res) 
                *res = presto_d2d(dentry)->dd_flags;

        // XXX this check makes no sense as d_count can change anytime.
        /* indicate if we were the only users while changing the flag */
        if ( atomic_read(&dentry->d_count) > 1 )
                error = -EBUSY;

out:
        path_release(&nd);
        return error;
}

/* given a path, operate on the flags in its cache.  Used by mark_ioctl */
int presto_mark_cache(const char *name, int and_flag, int or_flag, 
                      int *res)
{
        struct nameidata nd;
        struct dentry *dentry;
        struct presto_cache *cache;
        int error;

        CDEBUG(D_INODE,
               "presto_mark_cache :: name: %s, and flag %x, or flag %x\n",
               name, and_flag, or_flag);

        error = presto_walk(name, &nd);
        if (error)
                return error;

        dentry = nd.dentry;
        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) )
                goto out;

        error = -EBADF;
        cache = presto_get_cache(dentry->d_inode);
        if ( !cache ) {
                printk("PRESTO: BAD: cannot find cache in presto_mark_cache\n");
                make_bad_inode(dentry->d_inode);
                goto out;
        }
        error = 0;
        ((int)cache->cache_flags) &= and_flag;
        ((int)cache->cache_flags) |= or_flag;
        if (res) {
                *res = (int)cache->cache_flags;
        }

out:
        path_release(&nd);
        return error;
}

int presto_mark_fset_dentry(struct dentry *dentry, int and_flag, int or_flag, 
                     int * res)
{
        int error;
        struct presto_file_set *fset;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) )
                return error;

        error = -EBADF;
        fset = presto_fset(dentry);
        if ( !fset ) {
                printk("PRESTO: BAD: cannot find cache in presto_mark_cache\n");
                make_bad_inode(dentry->d_inode);
                return error;
        }
        error = 0;
        ((int)fset->fset_flags) &= and_flag;
        ((int)fset->fset_flags) |= or_flag;
        if (res) { 
                *res = (int)fset->fset_flags;
        }

        return error;
}

/* given a path, operate on the flags in its cache.  Used by mark_ioctl */
inline int presto_mark_fset(const char *name, int and_flag, int or_flag, 
                     int * res)
{
        struct nameidata nd;
        struct dentry *dentry;
        int error;
        ENTRY;

        error = presto_walk(name, &nd);
        if (error)
                return error;


        dentry = nd.dentry;
        error = presto_mark_fset_dentry(dentry, and_flag, or_flag, res);

        path_release(&nd);
        return error;
}


/* talk to Lento about the permit */
static int presto_permit_upcall(struct dentry *dentry)
{
        int rc;
        char *path, *buffer;
        int pathlen;
        int minor;
        int fsetnamelen;
        struct presto_file_set *fset = NULL;

        if ( (minor = presto_i2m(dentry->d_inode)) < 0)
                return -EINVAL;

        fset = presto_fset(dentry);
        if (!fset) {
                EXIT;
                return -ENOTCONN;
        }
        
        if ( !presto_lento_up(minor) ) {
                if ( fset->fset_flags & FSET_STEAL_PERMIT ) {
                        return 0;
                } else {
                        return -ENOTCONN;
                }
        }

        PRESTO_ALLOC(buffer, char *, PAGE_SIZE);
        if ( !buffer ) {
                printk("PRESTO: out of memory!\n");
                return -ENOMEM;
        }
        path = presto_path(dentry, fset->fset_mtpt, buffer, PAGE_SIZE);
        pathlen = MYPATHLEN(buffer, path);
        fsetnamelen = strlen(fset->fset_name); 
        rc = lento_permit(minor, pathlen, fsetnamelen, path, fset->fset_name);
        PRESTO_FREE(buffer, PAGE_SIZE);
        return rc;
}

/* get a write permit for the fileset of this inode
 *  - if this returns a negative value there was an error
 *  - if 0 is returned the permit was already in the kernel -- or --
 *    Lento gave us the permit without reintegration
 *  - lento returns the number of records it reintegrated 
 */
int presto_get_permit(struct inode * inode)
{
        struct dentry *de;
        struct presto_file_set *fset;
        int minor = presto_i2m(inode);
        int rc;

        ENTRY;
        if (minor < 0) {
                EXIT;
                return -1;
        }

        if ( ISLENTO(minor) ) {
                EXIT;
                return -EINVAL;
        }

        if (list_empty(&inode->i_dentry)) {
                printk("No alias for inode %d\n", (int) inode->i_ino);
                EXIT;
                return -EINVAL;
        }

        de = list_entry(inode->i_dentry.next, struct dentry, d_alias);

        fset = presto_fset(de);
        if ( !fset ) {
                printk("Presto: no fileset in presto_get_permit!\n");
                EXIT;
                return -EINVAL;
        }

        if (fset->fset_flags & FSET_HASPERMIT) {
                lock_kernel();
                fset->fset_permit_count++;
                CDEBUG(D_INODE, "permit count now %d, inode %lx\n", 
                       fset->fset_permit_count, inode->i_ino);
                unlock_kernel();
                EXIT;
                return 0;
        } else {
		/* Allow reintegration to proceed without locks -SHP */
                rc = presto_permit_upcall(fset->fset_mtpt);
                lock_kernel();
                if ( !rc ) { 
                	presto_mark_fset_dentry
				(fset->fset_mtpt, ~0, FSET_HASPERMIT, NULL);
                	fset->fset_permit_count++;
                }
                CDEBUG(D_INODE, "permit count now %d, ino %lx (likely 1), rc %d\n", 
        		fset->fset_permit_count, inode->i_ino, rc);
                unlock_kernel();
                EXIT;
                return rc;
        }
}

int presto_put_permit(struct inode * inode)
{
        struct dentry *de;
        struct presto_file_set *fset;
        int minor = presto_i2m(inode);

        ENTRY;
        if (minor < 0) {
                EXIT;
                return -1;
        }

        if ( ISLENTO(minor) ) {
                EXIT;
                return -1;
        }

        if (list_empty(&inode->i_dentry)) {
                printk("No alias for inode %d\n", (int) inode->i_ino);
                EXIT;
                return -1;
        }

        de = list_entry(inode->i_dentry.next, struct dentry, d_alias);

        fset = presto_fset(de);
        if ( !fset ) {
                printk("Presto: no fileset in presto_get_permit!\n");
                EXIT;
                return -1;
        }

        lock_kernel();
        if (fset->fset_flags & FSET_HASPERMIT) {
                if (fset->fset_permit_count > 0) fset->fset_permit_count--;
                else printk("Put permit while permit count is 0, inode %lx!\n",
                                inode->i_ino); 
        } else {
        	fset->fset_permit_count=0;
        	printk("Put permit while no permit, inode %lx, flags %x!\n", 
               		inode->i_ino, fset->fset_flags);
        }

        CDEBUG(D_INODE, "permit count now %d, inode %lx\n", 
        		fset->fset_permit_count, inode->i_ino);

        if (fset->fset_flags & FSET_PERMIT_WAITING &&
                    fset->fset_permit_count == 0) {
                CDEBUG(D_INODE, "permit count now 0, ino %lx, notify Lento\n", 
                       inode->i_ino);
                presto_mark_fset_dentry(fset->fset_mtpt, ~FSET_PERMIT_WAITING, 0, NULL);
                presto_mark_fset_dentry(fset->fset_mtpt, ~FSET_HASPERMIT, 0, NULL);
                lento_release_permit(fset->fset_cache->cache_psdev->uc_minor,
                                     fset->fset_permit_cookie);
                fset->fset_permit_cookie = 0; 
        }
        unlock_kernel();

        EXIT;
        return 0;
}


void presto_getversion(struct presto_version * presto_version,
                       struct inode * inode)
{
        presto_version->pv_mtime = cpu_to_le64((__u64)inode->i_mtime);
        presto_version->pv_ctime = cpu_to_le64((__u64)inode->i_ctime);
        presto_version->pv_size = cpu_to_le64((__u64)inode->i_size);
}

/*
 *  note: this routine "pins" a dentry for a fileset root
 */
int presto_set_fsetroot(char *path, char *fsetname, unsigned int fsetid,
                        unsigned int flags)
{
        struct presto_file_set *fset;
        struct presto_file_set *fset2;
        struct dentry *dentry;
        struct presto_cache *cache;
        int error;

        ENTRY;

        PRESTO_ALLOC(fset, struct presto_file_set *, sizeof(*fset));
        error = -ENOMEM;
        if ( !fset ) {
                printk(KERN_ERR "No memory allocating fset for %s\n", fsetname);
                EXIT;
                return -ENOMEM;
        }
        CDEBUG(D_INODE, "fset at %p\n", fset);

        printk("presto: fsetroot: path %s, fileset name %s\n", path, fsetname);
        error = presto_walk(path, &fset->fset_nd);
        CDEBUG(D_INODE, "\n");
        if (error) {
                EXIT;
                goto out_free;
        }
        dentry = fset->fset_nd.dentry;
        CDEBUG(D_INODE, "\n");

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto out_dput;
        }

        CDEBUG(D_INODE, "\n");
        cache = presto_get_cache(dentry->d_inode);
        if (!cache) {
                printk(KERN_ERR "No cache found for %s\n", path);
                EXIT;
                goto out_dput;
        }

        CDEBUG(D_INODE, "\n");
        error = -EINVAL;
        if ( !cache->cache_mtpt) {
                printk(KERN_ERR "Presto - no mountpoint: fsetroot fails!\n");
                EXIT;
                goto out_dput;
        }
        CDEBUG(D_INODE, "\n");

        if (!cache->cache_root_fileset)  {
                printk(KERN_ERR "Presto - no file set: fsetroot fails!\n");
                EXIT;
                goto out_dput;
        }

        error = -EEXIST;
        CDEBUG(D_INODE, "\n");

        fset2 = presto_fset(dentry);
        if (fset2 && (fset2->fset_mtpt == dentry) ) { 
                printk(KERN_ERR "Fsetroot already set (path %s)\n", path);
                EXIT;
                goto out_dput;
        }

        fset->fset_cache = cache;
        fset->fset_mtpt = dentry;
        fset->fset_name = fsetname;
        fset->fset_chunkbits = CHUNK_BITS;
        fset->fset_flags = flags;
	fset->fset_file_maxio = FSET_DEFAULT_MAX_FILEIO; 

        presto_d2d(dentry)->dd_fset = fset;
        list_add(&fset->fset_list, &cache->cache_fset_list);

        error = presto_init_kml_file(fset);
        if ( error ) {
                EXIT;
                CDEBUG(D_JOURNAL, "Error init_kml %d\n", error);
                goto out_list_del;
        }

        error = presto_init_last_rcvd_file(fset);
        if ( error ) {
                int rc;
                EXIT;
                rc = presto_close_journal_file(fset);
                CDEBUG(D_JOURNAL, "Error init_lastrcvd %d, cleanup %d\n", error, rc);
                goto out_list_del;
        }

        error = presto_init_lml_file(fset);
        if ( error ) {
                int rc;
                EXIT;
                rc = presto_close_journal_file(fset);
                CDEBUG(D_JOURNAL, "Error init_lml %d, cleanup %d\n", error, rc);
                goto out_list_del;
        }

#ifdef  CONFIG_KREINT
        /* initialize kml reint buffer */
        error = kml_init (fset); 
        if ( error ) {
                int rc;
                EXIT;
                rc = presto_close_journal_file(fset);
                CDEBUG(D_JOURNAL, "Error init kml reint %d, cleanup %d\n", 
                                error, rc);
                goto out_list_del;
        }
#endif
        if ( dentry->d_inode == dentry->d_inode->i_sb->s_root->d_inode) {
                cache->cache_flags |= CACHE_FSETROOT_SET;
        }

        CDEBUG(D_PIOCTL, "-------> fset at %p, dentry at %p, mtpt %p, fset %s, cache %p, presto_d2d(dentry)->dd_fset %p\n",
               fset, dentry, fset->fset_mtpt, fset->fset_name, cache, presto_d2d(dentry)->dd_fset);

        EXIT;
        return 0;

 out_list_del:
        list_del(&fset->fset_list);
        presto_d2d(dentry)->dd_fset = NULL;
 out_dput:
        path_release(&fset->fset_nd); 
 out_free:
        PRESTO_FREE(fset, sizeof(*fset));
        return error;
}

int presto_get_kmlsize(char *path, size_t *size)
{
        struct nameidata nd;
        struct presto_file_set *fset;
        struct dentry *dentry;
        int error;

        ENTRY;
        error = presto_walk(path, &nd);
        if (error) {
                EXIT;
                return error;
        }
        dentry = nd.dentry;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto kml_out;
        }

        error = -EINVAL;
        if ( ! presto_dentry2fset(dentry)) {
                EXIT;
                goto kml_out;
        }

        fset = presto_dentry2fset(dentry);
        if (!fset) {
                EXIT;
                goto kml_out;
        }
        error = 0;
        *size = fset->fset_kml.fd_offset;

 kml_out:
        path_release(&nd);
        return error;
}

static void presto_cleanup_fset(struct presto_file_set *fset)
{
	int error;
	struct presto_cache *cache;

	ENTRY;
#ifdef  CONFIG_KREINT
        error = kml_cleanup (fset);
        if ( error ) {
                printk("InterMezzo: Closing kml for fset %s: %d\n",
                       fset->fset_name, error);
        }
#endif

        error = presto_close_journal_file(fset);
        if ( error ) {
                printk("InterMezzo: Closing journal for fset %s: %d\n",
                       fset->fset_name, error);
        }
        cache = fset->fset_cache;
        cache->cache_flags &= ~CACHE_FSETROOT_SET;

        list_del(&fset->fset_list);

	presto_d2d(fset->fset_mtpt)->dd_fset = NULL;
        path_release(&fset->fset_nd);

        fset->fset_mtpt = NULL;
        PRESTO_FREE(fset->fset_name, strlen(fset->fset_name) + 1);
        PRESTO_FREE(fset, sizeof(*fset));
        EXIT;
}

int presto_clear_fsetroot(char *path)
{
        struct nameidata nd;
        struct presto_file_set *fset;
        struct dentry *dentry;
        int error;

        ENTRY;
        error = presto_walk(path, &nd);
        if (error) {
                EXIT;
                return error;
        }
        dentry = nd.dentry;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto put_out;
        }

        error = -EINVAL;
        if ( ! presto_dentry2fset(dentry)) {
                EXIT;
                goto put_out;
        }

        fset = presto_dentry2fset(dentry);
        if (!fset) {
                EXIT;
                goto put_out;
        }

	presto_cleanup_fset(fset);
        EXIT;

put_out:
        path_release(&nd); /* for our lookup */
        return error;
}

int presto_clear_all_fsetroots(char *path)
{
        struct nameidata nd;
        struct presto_file_set *fset;
        struct dentry *dentry;
        struct presto_cache *cache;
        int error;
        struct list_head *tmp,*tmpnext;


        ENTRY;
        error = presto_walk(path, &nd);
        if (error) {
                EXIT;
                return error;
        }
        dentry = nd.dentry;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto put_out;
        }

        error = -EINVAL;
        if ( ! presto_dentry2fset(dentry)) {
                EXIT;
                goto put_out;
        }

        fset = presto_dentry2fset(dentry);
        if (!fset) {
                EXIT;
                goto put_out;
        }

	error = 0;
        cache = fset->fset_cache;
        cache->cache_flags &= ~CACHE_FSETROOT_SET;

        tmp = &cache->cache_fset_list;
        tmpnext = tmp->next;
        while ( tmpnext != &cache->cache_fset_list) {
		tmp = tmpnext;
                tmpnext = tmp->next;
                fset = list_entry(tmp, struct presto_file_set, fset_list);

		presto_cleanup_fset(fset);
        }

        EXIT;
 put_out:
        path_release(&nd); /* for our lookup */
        return error;
}


int presto_get_lastrecno(char *path, off_t *recno)
{
        struct nameidata nd; 
        struct presto_file_set *fset;
        struct dentry *dentry;
        int error;
        ENTRY;

        error = presto_walk(path, &nd);
        if (error) {
                EXIT;
                return error;
        }

        dentry = nd.dentry;

        error = -ENXIO;
        if ( !presto_ispresto(dentry->d_inode) ) {
                EXIT;
                goto kml_out;
        }

        error = -EINVAL;
        if ( ! presto_dentry2fset(dentry)) {
                EXIT;
                goto kml_out;
        }

        fset = presto_dentry2fset(dentry);
        if (!fset) {
                EXIT;
                goto kml_out;
        }
        error = 0;
        *recno = fset->fset_kml.fd_recno;

 kml_out:
        path_release(&nd);
        return error;
}

/* 
   if *cookie != 0, lento must wait for this cookie
   before releasing the permit, operations are in progress. 
*/ 
int presto_permit_downcall( const char * path, int *cookie )
{
        int result;
        struct presto_file_set *fset; 

        fset = presto_path2fileset(path);
        if (IS_ERR(fset)) { 
                EXIT;
                return PTR_ERR(fset);
        }

	lock_kernel();
        if (fset->fset_permit_count != 0) {
                /* is there are previous cookie? */
                if (fset->fset_permit_cookie == 0) {
                        CDEBUG(D_CACHE, "presto installing cookie 0x%x, %s\n",
                               *cookie, path);
                        fset->fset_permit_cookie = *cookie;
                } else {
                        *cookie = fset->fset_permit_cookie;
                        CDEBUG(D_CACHE, "presto has cookie 0x%x, %s\n",
                               *cookie, path);
                }
                result = presto_mark_fset(path, 0, FSET_PERMIT_WAITING, NULL);
        } else {
                *cookie = 0;
                CDEBUG(D_CACHE, "presto releasing permit %s\n", path);
                result = presto_mark_fset(path, ~FSET_HASPERMIT, 0, NULL);
        }
	unlock_kernel();

        return result;
}

inline int presto_is_read_only(struct presto_file_set * fset)
{
        int minor, mask;
        struct presto_cache *cache = fset->fset_cache;

        minor= cache->cache_psdev->uc_minor;
        mask= (ISLENTO(minor)? FSET_LENTO_RO : FSET_CLIENT_RO);
        if ( fset->fset_flags & mask )
                return 1;
        mask= (ISLENTO(minor)? CACHE_LENTO_RO : CACHE_CLIENT_RO);
        return  ((cache->cache_flags & mask)? 1 : 0);
}


/*
 *
 *  Copyright (C) 2000 Stelias Computing, Inc.
 *  Copyright (C) 2000 Red Hat, Inc.
 *  Copyright (C) 2000 TurboLinux, Inc.
 *  Copyright (C) 2000 Los Alamos National Laboratory.
 *  Copyright (C) 2000 Tacitus Systems
 *  Copyright (C) 2000 Peter J. Braam
 *  Copyright (C) 2001 Mountain View Data, Inc. 
 *  Copyright (C) 2001 Cluster File Systems, Inc. 
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 */


#include <stdarg.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/system.h>

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
#include <linux/smp_lock.h>
#define __NO_VERSION__
#include <linux/module.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_kml.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>
#include <linux/fsfilter.h>
/*
 * these are initialized in super.c
 */
extern int presto_permission(struct inode *inode, int mask);
extern int presto_opendir_upcall(int minor, struct dentry *de, int async);

extern int presto_prep(struct dentry *, struct presto_cache **,
                       struct presto_file_set **);


#if 0
static int presto_open_upcall(int minor, struct dentry *de)
{
        int rc;
        char *path, *buffer;
        int pathlen;

        PRESTO_ALLOC(buffer, char *, PAGE_SIZE);
        if ( !buffer ) {
                printk("PRESTO: out of memory!\n");
                return ENOMEM;
        }
        path = presto_path(de, buffer, PAGE_SIZE);
        pathlen = MYPATHLEN(buffer, path);
        rc = lento_open(minor, pathlen, path);
        PRESTO_FREE(buffer, PAGE_SIZE);
        return rc;
}
#endif


static int presto_file_open(struct inode *inode, struct file *file)
{
        int rc = 0;
        struct file_operations *fops;
        struct presto_cache *cache;
        struct presto_file_data *fdata;
        int writable = (file->f_flags & (O_RDWR | O_WRONLY));
        int minor;
        int i;

        ENTRY;

        cache = presto_get_cache(inode);
        if ( !cache ) {
                printk("PRESTO: BAD, BAD: cannot find cache\n");
                EXIT;
                return -EBADF;
        }

        minor = presto_c2m(cache);

        CDEBUG(D_CACHE, "presto_file_open: DATA_OK: %d, ino: %ld\n",
               presto_chk(file->f_dentry, PRESTO_DATA), inode->i_ino);

        if ( ISLENTO(minor) )
                goto cache;

        if ( file->f_flags & O_RDWR || file->f_flags & O_WRONLY) {
                CDEBUG(D_CACHE, "presto_file_open: calling presto_get_permit\n");
                /* lock needed to protect permit_count manipulations -SHP */
                if ( presto_get_permit(inode) < 0 ) {
                        EXIT;
                        return -EROFS;
                }
                presto_put_permit(inode);
        }

        /* XXX name space synchronization here for data/streaming on demand?*/
        /* XXX Lento can make us wait here for backfetches to complete */
#if 0
        if ( !presto_chk(file->f_dentry, PRESTO_DATA) ||
             !presto_has_all_data(file->f_dentry->d_inode) ) {
                CDEBUG(D_CACHE, "presto_file_open: presto_open_upcall\n");
                rc = presto_open_upcall(minor, file->f_dentry);
        }

#endif
        rc = 0;
 cache:
        fops = filter_c2cffops(cache->cache_filter);
        if ( fops->open ) {
                CDEBUG(D_CACHE, "presto_file_open: calling fs open\n");
                rc = fops->open(inode, file);
        }
        if (rc) {
            EXIT;
            return rc;
        }

        CDEBUG(D_CACHE, "presto_file_open: setting DATA, ATTR\n");
        if( ISLENTO(minor) )
            presto_set(file->f_dentry, PRESTO_ATTR );
        else
                presto_set(file->f_dentry, PRESTO_ATTR | PRESTO_DATA);

        if (writable) { 
                PRESTO_ALLOC(fdata, struct presto_file_data *, sizeof(*fdata));
                if (!fdata) {
                        EXIT;
                        return -ENOMEM;
                }
                /* we believe that on open the kernel lock
                   assures that only one process will do this allocation */ 
                fdata->fd_do_lml = 0;
                fdata->fd_fsuid = current->fsuid;
                fdata->fd_fsgid = current->fsgid;
                fdata->fd_mode = file->f_dentry->d_inode->i_mode;
                fdata->fd_ngroups = current->ngroups;
                for (i=0 ; i<current->ngroups ; i++)
                        fdata->fd_groups[i] = current->groups[i]; 
                fdata->fd_bytes_written = 0; /*when open,written data is zero*/ 
                file->private_data = fdata; 
        } else {
                file->private_data = NULL;
        }

        return 0;
}

static int presto_file_release(struct inode *inode, struct file *file)
{
        struct rec_info rec;
        int rc;
        int writable = (file->f_flags & (O_RDWR | O_WRONLY));
        struct file_operations *fops;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        void *handle; 
        struct presto_file_data *fdata = 
                (struct presto_file_data *)file->private_data;

        ENTRY;
        rc = presto_prep(file->f_dentry, &cache, &fset);
        if ( rc ) {
                EXIT;
                return rc;
        }

        fops = filter_c2cffops(cache->cache_filter);
        rc = fops->release(inode, file);

        CDEBUG(D_CACHE, "islento = %d (minor %d), writable = %d, rc %d, data %p\n",
               ISLENTO(cache->cache_psdev->uc_minor), 
               cache->cache_psdev->uc_minor, 
               writable, rc, fdata);

        if (fdata && fdata->fd_do_lml) { 
                CDEBUG(D_CACHE, "LML at %lld\n", fdata->fd_lml_offset); 
        }

        /* don't journal close if file couldn't have been written to */
        /*    if (!ISLENTO(cache->cache_prestominor) && !rc && writable) {*/
        if (fdata && fdata->fd_do_lml && 
            !rc && writable && (! ISLENTO(cache->cache_psdev->uc_minor))) {
                struct presto_version new_ver;

                presto_getversion(&new_ver, inode);

                /* XXX: remove when lento gets file granularity cd */
                /* Lock needed to protect permit_count manipulations -SHP */
                if ( presto_get_permit(inode) < 0 ) {
                        EXIT;
                        return -EROFS;
                }
                CDEBUG(D_CACHE, "presto_file_release: writing journal\n");
        
                rc = presto_reserve_space(fset->fset_cache, PRESTO_REQHIGH); 
                if (rc) { 
                        presto_put_permit(inode); 
                        EXIT; 
                        return rc;
                }
                handle = presto_trans_start(fset, file->f_dentry->d_inode, 
                                            PRESTO_OP_RELEASE);
                if ( IS_ERR(handle) ) {
                        printk("presto_release: no space for transaction\n");
                        presto_put_permit(inode);
                        return -ENOSPC;
                }
                rc = presto_journal_close(&rec, fset, file, file->f_dentry, 
                                          &new_ver);
                if (rc) { 
                        printk("presto_close: cannot journal close\n");
                        /* XXX oops here to get this bug */ 
                        *(int *)0 = 1;
                        presto_put_permit(inode);
                        return -ENOSPC;
                }
                presto_trans_commit(fset, handle); 

                /* cancel the LML record */ 
                handle = presto_trans_start
                        (fset, inode, PRESTO_OP_WRITE);
                if ( IS_ERR(handle) ) {
                        printk("presto_release: no space for clear\n");
                        presto_put_permit(inode);
                        return -ENOSPC;
                }
                rc = presto_clear_lml_close(fset,
                                            fdata->fd_lml_offset); 
                if (rc < 0 ) { 
                        /* XXX oops here to get this bug */ 
                        *(int *)0 = 1;
                        presto_put_permit(inode);
                        printk("presto_close: cannot journal close\n");
                        return -ENOSPC;
                }
                presto_trans_commit(fset, handle); 
                presto_release_space(fset->fset_cache, PRESTO_REQHIGH); 

                presto_truncate_lml(fset);

                presto_put_permit(inode);
        }

        if (!rc && fdata) {
                PRESTO_FREE(fdata, sizeof(*fdata));
        }
        file->private_data = NULL; 
        
        EXIT;
        return rc;
}



static void presto_apply_write_policy(struct file *file, struct presto_file_set *fset, loff_t res)
{
        struct presto_file_data *fdata = (struct presto_file_data *)file->private_data;
        struct presto_cache *cache = fset->fset_cache;
        struct presto_version new_file_ver;
        int error;
        struct rec_info rec;

        /* Here we do a journal close after a fixed or a specified
         amount of KBytes, currently a global parameter set with
         sysctl. If files are open for a long time, this gives added
         protection. (XXX todo: per cache, add ioctl, handle
         journaling in a thread, add more options etc.)
        */ 
 
         if (  (fset->fset_flags & FSET_JCLOSE_ON_WRITE)
                 && (!ISLENTO(cache->cache_psdev->uc_minor)))  {
                 fdata->fd_bytes_written += res;
 
                 if (fdata->fd_bytes_written >= fset->fset_file_maxio) {
                         presto_getversion(&new_file_ver, file->f_dentry->d_inode);
                        /* This is really heavy weight and should be fixed
                           ASAP. At most we should be recording the number
                           of bytes written and not locking the kernel, 
                           wait for permits, etc, on the write path. SHP
                        */
                        lock_kernel();
                         if ( presto_get_permit(file->f_dentry->d_inode) < 0 ) {
                                 EXIT;
                                 /* we must be disconnected, not to worry */
                                return; 
                         }
                         error = presto_journal_close
                                (&rec, fset, file, file->f_dentry, &new_file_ver);
                         presto_put_permit(file->f_dentry->d_inode);
                        unlock_kernel();
                         if ( error ) {
                                 printk("presto_close: cannot journal close\n");
                                 /* XXX these errors are really bad */
                                /* panic(); */
                                 return;
                         }
                             fdata->fd_bytes_written = 0;
                     } 
        }
}

static ssize_t presto_file_write(struct file *file, const char *buf, size_t size, 
                          loff_t *off)
{
        struct rec_info rec;
        int error;
        struct presto_cache *cache;
        struct presto_file_set *fset;
        struct file_operations *fops;
        ssize_t res;
        int do_lml_here;
        void *handle = NULL;
        unsigned long blocks;
        struct presto_file_data *fdata;
        loff_t res_size; 

        error = presto_prep(file->f_dentry, &cache, &fset);
        if ( error ) {
                EXIT;
                return error;
        }

        blocks = (size >> file->f_dentry->d_inode->i_sb->s_blocksize_bits) + 1;
        /* XXX 3 is for ext2 indirect blocks ... */ 
        res_size = 2 * PRESTO_REQHIGH + ((blocks+3) 
                << file->f_dentry->d_inode->i_sb->s_blocksize_bits);

        error = presto_reserve_space(fset->fset_cache, res_size); 
        CDEBUG(D_INODE, "Reserved %Ld for %Zd\n", res_size, size); 
        if ( error ) { 
                EXIT;
                return -ENOSPC;
        }

        /* XXX lock something here */
        CDEBUG(D_INODE, "islento %d, minor: %d\n", ISLENTO(cache->cache_psdev->uc_minor),
               cache->cache_psdev->uc_minor); 
        read_lock(&fset->fset_lml.fd_lock); 
        fdata = (struct presto_file_data *)file->private_data;
        do_lml_here = (!ISLENTO(cache->cache_psdev->uc_minor)) &&
                size && (fdata->fd_do_lml == 0);

        if (do_lml_here)
                fdata->fd_do_lml = 1;
        read_unlock(&fset->fset_lml.fd_lock); 

        /* XXX we have two choices:
           - we do the transaction for the LML record BEFORE any write
           transaction starts - that has the benefit that no other
           short write can complete without the record being there. 
           The disadvantage is that even if no write happens we get 
           the LML record. 
           - we bundle the transaction with this write.  In that case
           we may not have an LML record is a short write goes through
           before this one (can that actually happen?).
        */
        res = 0;
        if (do_lml_here) {
                /* handle different space reqs from file system below! */
                handle = presto_trans_start(fset, file->f_dentry->d_inode, 
                                            PRESTO_OP_WRITE);
                if ( IS_ERR(handle) ) {
                        presto_release_space(fset->fset_cache, res_size); 
                        printk("presto_write: no space for transaction\n");
                        return -ENOSPC;
                }
                res = presto_journal_write(&rec, fset, file);
                fdata->fd_lml_offset = rec.offset;
                if ( res ) {
                        /* XXX oops here to get this bug */ 
                        /* *(int *)0 = 1; */
                        EXIT;
                        goto exit_write;
                }
                
                presto_trans_commit(fset, handle);
        }

        fops = filter_c2cffops(cache->cache_filter);
        res = fops->write(file, buf, size, off);
        if ( res != size ) {
                CDEBUG(D_FILE, "file write returns short write: size %Zd, res %Zd\n", size, res); 
        }

        if ( (res > 0) && fdata ) 
                 presto_apply_write_policy(file, fset, res);
  
 exit_write:
        presto_release_space(fset->fset_cache, res_size); 
        return res;
}

struct file_operations presto_file_fops = {
        write:   presto_file_write,
        open:    presto_file_open,
        release: presto_file_release
};

struct inode_operations presto_file_iops = {
        permission: presto_permission,
	setattr: presto_setattr,
#ifdef CONFIG_FS_EXT_ATTR
	set_ext_attr: presto_set_ext_attr,
#endif
};




/*
 *              An implementation of a loadable kernel mode driver providing
 *              multiple kernel/user space bidirectional communications links.
 *
 *              Author:         Alan Cox <alan@cymru.net>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 *              Adapted to become the Linux 2.0 Coda pseudo device
 *              Peter  Braam  <braam@maths.ox.ac.uk>
 *              Michael Callahan <mjc@emmy.smith.edu>
 *
 *              Changes for Linux 2.1
 *              Copyright (c) 1997 Carnegie-Mellon University
 *
 *              Redone again for InterMezzo
 *              Copyright (c) 1998 Peter J. Braam
 *              Copyright (c) 2000 Mountain View Data, Inc.
 *              Copyright (c) 2000 Tacitus Systems, Inc.
 *              Copyright (c) 2001 Cluster File Systems, Inc.
 *
 *		Extended attribute support
 *		Copyright (c) 2001 Shirish. H. Phatak
 *		Copyright (c) 2001 Tacit Networks, Inc.
 */


#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/lp.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/list.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/poll.h>
#include <asm/uaccess.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>
#include <linux/intermezzo_kml.h>


#ifdef PRESTO_DEVEL
int  presto_print_entry = 1;
int  presto_debug = 4095;
#else
int  presto_print_entry = 0;
int  presto_debug = 0;
#endif

/* Like inode.c (presto_sym_iops), the initializer is just to prevent
   upc_comms from appearing as a COMMON symbol (and therefore
   interfering with other modules that use the same variable name. */
struct upc_comm upc_comms[MAX_PRESTODEV] = {{0}};

/*
 * Device operations: map file to upcall structure
 */
static inline struct upc_comm *presto_psdev_f2u(struct file *file)
{
        int minor;

        if ( MAJOR(file->f_dentry->d_inode->i_rdev) != PRESTO_PSDEV_MAJOR ) {
                EXIT;
                return NULL;
        }

        minor = MINOR(file->f_dentry->d_inode->i_rdev);
        if ( minor < 0 || minor >= MAX_PRESTODEV ) {
                EXIT;
                return NULL;
        }

        return &(upc_comms[minor]);
}

inline int presto_lento_up(int minor) 
{
        return upc_comms[minor].uc_pid;
}


static unsigned int presto_psdev_poll(struct file *file, poll_table * wait)
{
        struct upc_comm *upccom;
        unsigned int mask = POLLOUT | POLLWRNORM;
        /* ENTRY; this will flood you */

        if ( ! (upccom = presto_psdev_f2u(file)) ) {
                kdev_t dev = file->f_dentry->d_inode->i_rdev;
                printk("InterMezzo: %s, bad device %s\n",
                       __FUNCTION__, kdevname(dev));
        }

        poll_wait(file, &(upccom->uc_waitq), wait);

        if (!list_empty(&upccom->uc_pending)) {
                CDEBUG(D_PSDEV, "Non-empty pending list.\n");
                mask |= POLLIN | POLLRDNORM;
        }

        /* EXIT; will flood you */
        return mask;
}



/*
 *      Receive a message written by Lento to the psdev
 */
static ssize_t presto_psdev_write(struct file *file, const char *buf,
                                  size_t count, loff_t *off)
{
        struct upc_comm *upccom;
        struct upc_req *req = NULL;
        struct upc_req *tmp;
        struct list_head *lh;
        struct lento_down_hdr hdr;
        int error;

        if ( ! (upccom = presto_psdev_f2u(file)) ) {
                kdev_t dev = file->f_dentry->d_inode->i_rdev;
                printk("InterMezzo: %s, bad device %s\n",
                       __FUNCTION__, kdevname(dev));
        }

        /* Peek at the opcode, uniquefier */
        if ( count < sizeof(hdr) ) {
              printk("presto_psdev_write: Lento didn't write full hdr.\n");
                return -EINVAL;
        }

        error = copy_from_user(&hdr, buf, sizeof(hdr));
        if ( error )
                return error;

        CDEBUG(D_PSDEV, "(process,opc,uniq)=(%d,%d,%d)\n",
               current->pid, hdr.opcode, hdr.unique);

        /* Look for the message on the processing queue. */
        lh  = &upccom->uc_processing;
        while ( (lh = lh->next) != &upccom->uc_processing ) {
                tmp = list_entry(lh, struct upc_req , rq_chain);
                if (tmp->rq_unique == hdr.unique) {
                        req = tmp;
                      /* unlink here: keeps search length minimal */
                        list_del(&req->rq_chain);
                      INIT_LIST_HEAD(&req->rq_chain);
                        CDEBUG(D_PSDEV,"Eureka opc %d uniq %d!\n",
                               hdr.opcode, hdr.unique);
                        break;
                }
        }
        if (!req) {
                printk("psdev_write: msg (%d, %d) not found\n",
                       hdr.opcode, hdr.unique);
                return(-ESRCH);
        }

        /* move data into response buffer. */
        if (req->rq_bufsize < count) {
                printk("psdev_write: too much cnt: %d, cnt: %d, "
                       "opc: %d, uniq: %d.\n",
                       req->rq_bufsize, count, hdr.opcode, hdr.unique);
                count = req->rq_bufsize; /* don't have more space! */
        }
        error = copy_from_user(req->rq_data, buf, count);
        if ( error )
                return error;

        /* adjust outsize: good upcalls can be aware of this */
        req->rq_rep_size = count;
        req->rq_flags |= REQ_WRITE;

        wake_up(&req->rq_sleep);
        return(count);
}

/*
 *      Read a message from the kernel to Lento
 */
static ssize_t presto_psdev_read(struct file * file, char * buf,
                                 size_t count, loff_t *off)
{
        struct upc_comm *upccom;
        struct upc_req *req;
        int result = count;

        if ( ! (upccom = presto_psdev_f2u(file)) ) {
                kdev_t dev = file->f_dentry->d_inode->i_rdev;
                printk("InterMezzo: %s, bad device %s\n",
                       __FUNCTION__, kdevname(dev));
        }

        CDEBUG(D_PSDEV, "count %d\n", count);
        if (list_empty(&(upccom->uc_pending))) {
                CDEBUG(D_UPCALL, "Empty pending list in read, not good\n");
                return -EINVAL;
        }

        req = list_entry((upccom->uc_pending.next), struct upc_req, rq_chain);
        list_del(&(req->rq_chain));
      if (! (req->rq_flags & REQ_ASYNC) ) {
              list_add(&(req->rq_chain), upccom->uc_processing.prev);
      }
      req->rq_flags |= REQ_READ;

        /* Move the input args into userspace */
        if (req->rq_bufsize <= count) {
                result = req->rq_bufsize;
        }

        if (count < req->rq_bufsize) {
                printk ("psdev_read: buffer too small, read %d of %d bytes\n",
                        count, req->rq_bufsize);
        }

        if ( copy_to_user(buf, req->rq_data, result) ) {
                return -EFAULT;
        }

        /* If request was asynchronous don't enqueue, but free */
        if (req->rq_flags & REQ_ASYNC) {
                CDEBUG(D_PSDEV, "psdev_read: async msg (%d, %d), result %d\n",
                       req->rq_opcode, req->rq_unique, result);
                PRESTO_FREE(req->rq_data, req->rq_bufsize);
                PRESTO_FREE(req, sizeof(*req));
                return result;
        }

        return result;
}

static int presto_psdev_ioctl(struct inode *inode, struct file *file,
                              unsigned int cmd, unsigned long arg)
{
        struct upc_comm *upccom;
        /* XXX is this rdev or dev? */
        kdev_t dev = inode->i_rdev;

        ENTRY;
        upccom = presto_psdev_f2u(file);
        if ( !upccom) {
                printk("InterMezzo: %s, bad device %s\n",
                       __FUNCTION__, kdevname(dev));
                EXIT;
                return -ENODEV;
        }

        switch(cmd) {

        case TCGETS:
                return -EINVAL;

        case PRESTO_GETMOUNT: {
                /* return all the mounts for this device.  */
                int minor = 0;
                int len, outlen;
                struct readmount readmount;
                struct readmount *user_readmount = (struct readmount *) arg;
                char * tmp;
                int error;

                error = copy_from_user(&readmount, (void *)arg,
                                       sizeof(readmount));
                if ( error )  {
                        printk("psdev: can't copy %d bytes from %p to %p\n",
                                sizeof(readmount), (struct readmount *) arg,
                                &readmount);
                        EXIT;
                        return error;
                }

                len = readmount.io_len;
                minor = MINOR(dev);
                PRESTO_ALLOC(tmp, char *, len);
                if (!tmp) {
                        EXIT;
                        return -ENOMEM;
                }

                outlen = presto_sprint_mounts(tmp, len, minor);
                CDEBUG(D_PSDEV, "presto_sprint_mounts returns %d bytes\n",
                                outlen);

                /* as this came out on 1/3/2000, it could NEVER work.
                 * So fix it ... RGM
                 * I mean, let's let the compiler do a little work ...
                 * gcc suggested the extra ()
                 */
                error = copy_to_user(readmount.io_string, tmp, outlen);
                if ( error ) {
                        CDEBUG(D_PSDEV, "Copy_to_user string 0x%p failed\n",
                               readmount.io_string);
                }
                if ((!error) && (error = copy_to_user(&(user_readmount->io_len),
                                                      &outlen, sizeof(int))) ) {
                        CDEBUG(D_PSDEV, "Copy_to_user len @0x%p failed\n",
                               &(user_readmount->io_len));
                }

                PRESTO_FREE(tmp, len);
                EXIT;
                return error;
        }

        case PRESTO_SETPID: {
                /*
                 * This ioctl is performed by each Lento that starts up
                 * and wants to do further communication with presto.
                 */
                CDEBUG(D_PSDEV, "Setting current pid to %d\n", current->pid);
                upccom->uc_pid = current->pid;
                if ( !list_empty(&upccom->uc_processing) ) {
                        struct list_head *lh;
                        struct upc_req *req;
                        printk("WARNING: setpid & processing not empty!\n");
                        lh = &upccom->uc_processing;
                        while ( (lh = lh->next) != &upccom->uc_processing) {
                                req = list_entry(lh, struct upc_req, rq_chain);
                                /* freeing of req and data is done by the sleeper */
                                wake_up(&req->rq_sleep);
                        }
                }
                if ( !list_empty(&upccom->uc_processing) ) {
                        printk("BAD: FAILDED TO CLEAN PROCESSING LIST!\n");
                }
                EXIT;
                return 0;
        }

        case PRESTO_CLEAR_FSETROOT: {
                /*
                 * Close KML files.
                 */
                int error;
                int saved_pid = upccom->uc_pid;
                char *path;
                struct {
                        char *path;
                        int   path_len;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                PRESTO_ALLOC(path, char *, input.path_len + 1);
                if ( !path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(path, input.path, input.path_len);
                if ( error ) {
                        PRESTO_FREE(path, input.path_len + 1);
                        EXIT;
                        return error;
                }
                path[input.path_len] = '\0';
                CDEBUG(D_PSDEV, "clear_fsetroot: path %s\n", path);

                upccom->uc_pid = current->pid;
                error = presto_clear_fsetroot(path);
                upccom->uc_pid = saved_pid;
                PRESTO_FREE(path, input.path_len + 1);
                EXIT;
                return error;
        }


        case PRESTO_CLEAR_ALL_FSETROOTS: {
                /*
                 * Close KML files.
                 */
                int error;
                int saved_pid = upccom->uc_pid;
                char *path;
                struct {
                        char *path;
                        int   path_len;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                PRESTO_ALLOC(path, char *, input.path_len + 1);
                if ( !path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(path, input.path, input.path_len);
                if ( error ) {
                        PRESTO_FREE(path, input.path_len + 1);
                        EXIT;
                        return error;
                }
                path[input.path_len] = '\0';
                CDEBUG(D_PSDEV, "clear_all_fsetroot: path %s\n", path);

                upccom->uc_pid = current->pid;
                error = presto_clear_all_fsetroots(path);
                upccom->uc_pid = saved_pid;
                PRESTO_FREE(path, input.path_len + 1);
                EXIT;
                return error;
        }

        case PRESTO_GET_KMLSIZE: {
                int error;
                int saved_pid = upccom->uc_pid;
                char *path;
                size_t size = 0;
                struct {
                        __u64 size;
                        char *path;
                        int   path_len;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                PRESTO_ALLOC(path, char *, input.path_len + 1);
                if ( !path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(path, input.path, input.path_len);
                if ( error ) {
                        PRESTO_FREE(path, input.path_len + 1);
                        EXIT;
                        return error;
                }
                path[input.path_len] = '\0';
                CDEBUG(D_PSDEV, "get_kmlsize: len %d path %s\n", 
                       input.path_len, path);

                upccom->uc_pid = current->pid;
                error = presto_get_kmlsize(path, &size);
                PRESTO_FREE(path, input.path_len + 1);
                if (error) {
                        EXIT;
                        return error;
                }
                input.size = size;
                upccom->uc_pid = saved_pid;

                CDEBUG(D_PSDEV, "get_kmlsize: size = %d\n", size);

                EXIT;
                return copy_to_user((char *)arg, &input, sizeof(input));
        }

        case PRESTO_GET_RECNO: {
                int error;
                int saved_pid = upccom->uc_pid;
                char *path;
                off_t recno = 0;
                struct {
                        __u64 recno;
                        char *path;
                        int   path_len;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                PRESTO_ALLOC(path, char *, input.path_len + 1);
                if ( !path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(path, input.path, input.path_len);
                if ( error ) {
                        PRESTO_FREE(path, input.path_len + 1);
                        EXIT;
                        return error;
                }
                path[input.path_len] = '\0';
                CDEBUG(D_PSDEV, "get_recno: len %d path %s\n", 
                       input.path_len, path);

                upccom->uc_pid = current->pid;
                error = presto_get_lastrecno(path, &recno);
                PRESTO_FREE(path, input.path_len + 1);
                if (error) {
                        EXIT;
                        return error;
                }
                input.recno = recno;
                upccom->uc_pid = saved_pid;

                CDEBUG(D_PSDEV, "get_recno: recno = %d\n", (int) recno);

                EXIT;
                return copy_to_user((char *)arg, &input, sizeof(input));
        }

        case PRESTO_SET_FSETROOT: {
                /*
                 * Save information about the cache, and initialize "special"
                 * cache files (KML, etc).
                 */
                int error;
                int saved_pid = upccom->uc_pid;
                char *fsetname;
                char *path;
                struct {
                        char *path;
                        int   path_len;
                        char *name;
                        int   name_len;
                        int   id;
                        int   flags;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                PRESTO_ALLOC(path, char *, input.path_len + 1);
                if ( !path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(path, input.path, input.path_len);
                if ( error ) {
                        EXIT;
                        goto exit_free_path;
                }
                path[input.path_len] = '\0';

                PRESTO_ALLOC(fsetname, char *, input.name_len + 1);
                if ( !fsetname ) {
                        error = -ENOMEM;
                        EXIT;
                        goto exit_free_path;
                }
                error = copy_from_user(fsetname, input.name, input.name_len);
                if ( error ) {
                        EXIT;
                        goto exit_free_fsetname;
                }
                fsetname[input.name_len] = '\0';

                CDEBUG(D_PSDEV,
                       "set_fsetroot: path %s name %s, id %d, flags %x\n",
                       path, fsetname, input.id, input.flags);
                upccom->uc_pid = current->pid;
                error = presto_set_fsetroot(path, fsetname, input.id,input.flags);
                upccom->uc_pid = saved_pid;
                if ( error ) {
                        EXIT;
                        goto exit_free_fsetname;
                }
                /* fsetname is kept in the fset, so don't free it now */
                PRESTO_FREE(path, input.path_len + 1);
                EXIT;
                return 0;

        exit_free_fsetname:
                PRESTO_FREE(fsetname, input.name_len + 1);
        exit_free_path:
                PRESTO_FREE(path, input.path_len + 1);
                return error;
        }

        case PRESTO_CLOSE_JOURNALF: {
                int saved_pid = upccom->uc_pid;
                int error;

                CDEBUG(D_SUPER, "HELLO\n");

                /* pretend we are lento: we should lock something */
                upccom->uc_pid = current->pid;
                error = presto_close_journal_file(NULL);
                CDEBUG(D_PSDEV, "error is %d\n", error);
                upccom->uc_pid = saved_pid;
                EXIT;
                return error;
        }

        case PRESTO_GETOPT:
        case PRESTO_SETOPT: {
                /* return all the mounts for this device.  */
                int dosetopt(int, struct psdev_opt *);
                int dogetopt(int, struct psdev_opt *);
                int minor = 0;
                struct psdev_opt kopt;
                struct psdev_opt *user_opt = (struct psdev_opt *) arg;
                int error;

                error = copy_from_user(&kopt, (void *)arg, sizeof(kopt));
                if ( error )  {
                        printk("psdev: can't copyin %d bytes from %p to %p\n",
                               sizeof(kopt), (struct kopt *) arg, &kopt);
                        EXIT;
                        return error;
                }
                minor = MINOR(dev);
                if (cmd == PRESTO_SETOPT)
                        error = dosetopt(minor, &kopt);

                if ( error ) {
                        CDEBUG(D_PSDEV,
                               "dosetopt failed minor %d, opt %d, val %d\n",
                               minor, kopt.optname, kopt.optval);
                        EXIT;
                        return error;
                }

                error = dogetopt(minor, &kopt);

                if ( error ) {
                        CDEBUG(D_PSDEV,
                               "dogetopt failed minor %d, opt %d, val %d\n",
                               minor, kopt.optname, kopt.optval);
                        EXIT;
                        return error;
                }

                error = copy_to_user(user_opt, &kopt, sizeof(kopt));
                if ( error ) {
                        CDEBUG(D_PSDEV, "Copy_to_user opt 0x%p failed\n",
                               user_opt);
                        EXIT;
                        return error;
                }
                CDEBUG(D_PSDEV, "dosetopt minor %d, opt %d, val %d return %d\n",
                         minor, kopt.optname, kopt.optval, error);
                EXIT;
                return 0;
        }

        case PRESTO_VFS_SETATTR: {
                int error;
                struct lento_input_attr input;
                struct iattr iattr;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }
                iattr.ia_valid = input.valid;
                iattr.ia_mode  = (umode_t)input.mode;
                iattr.ia_uid   = (uid_t)input.uid;
                iattr.ia_gid   = (gid_t)input.gid;
                iattr.ia_size  = (off_t)input.size;
                iattr.ia_atime = (time_t)input.atime;
                iattr.ia_mtime = (time_t)input.mtime;
                iattr.ia_ctime = (time_t)input.ctime;
                iattr.ia_attr_flags = input.attr_flags;

                error = lento_setattr(input.name, &iattr, &input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_CREATE: {
                int error;
                struct lento_input_mode input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_create(input.name, input.mode, &input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_LINK: {
                int error;
                struct lento_input_old_new input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_link(input.oldname, input.newname, &input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_UNLINK: {
                int error;
                struct lento_input input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_unlink(input.name, &input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_SYMLINK: {
                int error;
                struct lento_input_old_new input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_symlink(input.oldname, input.newname,&input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_MKDIR: {
                int error;
                struct lento_input_mode input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_mkdir(input.name, input.mode, &input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_RMDIR: {
                int error;
                struct lento_input input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_rmdir(input.name, &input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_MKNOD: {
                int error;
                struct lento_input_dev input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_mknod(input.name, input.mode,
                                    MKDEV(input.major,input.minor),&input.info);
                EXIT;
                return error;
        }

        case PRESTO_VFS_RENAME: {
                int error;
                struct lento_input_old_new input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                error = lento_rename(input.oldname, input.newname, &input.info);
                EXIT;
                return error;
        }

#ifdef CONFIG_FS_EXT_ATTR
        /* IOCTL to create/modify an extended attribute */
        case PRESTO_VFS_SETEXTATTR: {
                int error;
                struct lento_input_ext_attr input;
                char *name;
                char *buffer;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) { 
                    EXIT;
                    return error;
                }

                /* Now setup the input parameters */
                PRESTO_ALLOC(name, char *, input.name_len+1);
                /* We need null terminated strings for attr names */
                name[input.name_len] = '\0';
                error=copy_from_user(name, input.name, input.name_len);
                if ( error ) { 
                    EXIT;
                    PRESTO_FREE(name,input.name_len+1);
                    return error;
                }

                PRESTO_ALLOC(buffer, char *, input.buffer_len+1);
                error=copy_from_user(buffer, input.buffer, input.buffer_len);
                if ( error ) { 
                    EXIT;
                    PRESTO_FREE(name,input.name_len+1);
                    PRESTO_FREE(buffer,input.buffer_len+1);
                    return error;
                }
                /* Make null terminated for easy printing */
                buffer[input.buffer_len]='\0';
 
                CDEBUG(D_PSDEV," setextattr params: name %s, valuelen %d,"
                       " value %s, attr flags %x, mode %o, slot offset %d,"
                       " recno %d, kml offset %lu, flags %x, time %d\n", 
                       name, input.buffer_len, buffer, input.flags, input.mode,
                       input.info.slot_offset, input.info.recno,
                       (unsigned long) input.info.kml_offset, input.info.flags,
                       input.info.updated_time);

                error=lento_set_ext_attr
                      (input.path,name,buffer,input.buffer_len,
                       input.flags, input.mode, &input.info);

                PRESTO_FREE(name,input.name_len+1);
                PRESTO_FREE(buffer,input.buffer_len+1);
                EXIT;
                return error;
        }

        /* IOCTL to delete an extended attribute */
        case PRESTO_VFS_DELEXTATTR: {
                int error;
                struct lento_input_ext_attr input;
                char *name;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) { 
                    EXIT;
                    return error;
                }

                /* Now setup the input parameters */
                PRESTO_ALLOC(name, char *, input.name_len+1);
                /* We need null terminated strings for attr names */
                name[input.name_len] = '\0';
                error=copy_from_user(name, input.name, input.name_len);
                if ( error ) { 
                    EXIT;
                    PRESTO_FREE(name,input.name_len+1);
                    return error;
                }

                CDEBUG(D_PSDEV," delextattr params: name %s,"
                       " attr flags %x, mode %o, slot offset %d, recno %d,"
                       " kml offset %lu, flags %x, time %d\n", 
                       name, input.flags, input.mode,
                       input.info.slot_offset, input.info.recno,
                       (unsigned long) input.info.kml_offset, input.info.flags,
                       input.info.updated_time);

                error=lento_set_ext_attr
                      (input.path,name,NULL,0,input.flags,
                       input.mode,&input.info);
                PRESTO_FREE(name,input.name_len+1);
                EXIT;
                return error;
        }
#endif

        case PRESTO_VFS_IOPEN: {
                struct lento_input_iopen input;
                int error;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                input.fd = lento_iopen(input.name, (ino_t)input.ino,
                                       input.generation, input.flags);
                CDEBUG(D_PIOCTL, "lento_iopen file descriptor: %d\n", input.fd);
                if (input.fd < 0) {
                        EXIT;
                        return input.fd;
                }
                EXIT;
                return copy_to_user((char *)arg, &input, sizeof(input));
        }

        case PRESTO_VFS_CLOSE: {
                int error;
                struct lento_input_close input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }

                CDEBUG(D_PIOCTL, "lento_close file descriptor: %d\n", input.fd);
                error = lento_close(input.fd, &input.info);
                EXIT;
                return error;
        }

        case PRESTO_BACKFETCH_LML: {
                char *user_path;
                int error;
                struct lml_arg {
                        char *path;
                        __u32 path_len;
                        __u64 remote_ino;
                        __u32 remote_generation;
                        __u32 remote_version;
                        struct presto_version remote_file_version;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }
                user_path = input.path;

                PRESTO_ALLOC(input.path, char *, input.path_len + 1);
                if ( !input.path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(input.path, user_path, input.path_len);
                if ( error ) {
                        EXIT;
                        PRESTO_FREE(input.path, input.path_len + 1);
                        return error;
                }
                input.path[input.path_len] = '\0';

                CDEBUG(D_DOWNCALL, "lml name: %s\n", input.path);
                
                return lento_write_lml(input.path, 
                                       input.remote_ino, 
                                       input.remote_generation,
                                       input.remote_version,
                                       &input.remote_file_version); 

        }
                

        case PRESTO_CANCEL_LML: {
                char *user_path;
                int error;
                struct lml_arg {
                        char *path;
                        __u64 lml_offset; 
                        __u32 path_len;
                        __u64 remote_ino;
                        __u32 remote_generation;
                        __u32 remote_version;
                        struct lento_vfs_context info;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }
                user_path = input.path;

                PRESTO_ALLOC(input.path, char *, input.path_len + 1);
                if ( !input.path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(input.path, user_path, input.path_len);
                if ( error ) {
                        EXIT;
                        PRESTO_FREE(input.path, input.path_len + 1);
                        return error;
                }
                input.path[input.path_len] = '\0';

                CDEBUG(D_DOWNCALL, "lml name: %s\n", input.path);
                
                return lento_cancel_lml(input.path, 
                                        input.lml_offset, 
                                        input.remote_ino, 
                                        input.remote_generation,
                                        input.remote_version,
                                        &input.info); 

        }

        case PRESTO_COMPLETE_CLOSES: {
                char *user_path;
                int error;
                struct lml_arg {
                        char *path;
                        __u32 path_len;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }
                user_path = input.path;

                PRESTO_ALLOC(input.path, char *, input.path_len + 1);
                if ( !input.path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(input.path, user_path, input.path_len);
                if ( error ) {
                        EXIT;
                        PRESTO_FREE(input.path, input.path_len + 1);
                        return error;
                }
                input.path[input.path_len] = '\0';

                CDEBUG(D_DOWNCALL, "lml name: %s\n", input.path);
                
                error = lento_complete_closes(input.path);
                PRESTO_FREE(input.path, input.path_len + 1);
                return error;
        }

        case PRESTO_RESET_FSET: {
                char *user_path;
                int error;
                struct lml_arg {
                        char *path;
                        __u32 path_len;
                        __u64 offset;
                        __u32 recno;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }
                user_path = input.path;

                PRESTO_ALLOC(input.path, char *, input.path_len + 1);
                if ( !input.path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(input.path, user_path, input.path_len);
                if ( error ) {
                        EXIT;
                        PRESTO_FREE(input.path, input.path_len + 1);
                        return error;
                }
                input.path[input.path_len] = '\0';

                CDEBUG(D_DOWNCALL, "lml name: %s\n", input.path);
                
                return lento_reset_fset(input.path, input.offset, input.recno); 

        }
                

        case PRESTO_MARK: {
                char *user_path;
                int res = 0;  /* resulting flags - returned to user */
                int error;
                struct {
                        int  mark_what;
                        int  and_flag;
                        int  or_flag;
                        int path_len;
                        char *path;
                } input;

                error = copy_from_user(&input, (char *)arg, sizeof(input));
                if ( error ) {
                        EXIT;
                        return error;
                }
                user_path = input.path;

                PRESTO_ALLOC(input.path, char *, input.path_len + 1);
                if ( !input.path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(input.path, user_path, input.path_len);
                if ( error ) {
                        EXIT;
                        PRESTO_FREE(input.path, input.path_len + 1);
                        return error;
                }
                input.path[input.path_len] = '\0';

                CDEBUG(D_DOWNCALL, "mark name: %s, and: %x, or: %x, what %d\n",
                       input.path, input.and_flag, input.or_flag, 
                       input.mark_what);

                switch (input.mark_what) {
                case MARK_DENTRY:               
                        error = presto_mark_dentry(input.path,
                                                   input.and_flag,
                                                   input.or_flag, &res);
                        break;
                case MARK_FSET:
                        error = presto_mark_fset(input.path,
                                                   input.and_flag,
                                                   input.or_flag, &res);
                        break;
                case MARK_CACHE:
                        error = presto_mark_cache(input.path,
                                                   input.and_flag,
                                                   input.or_flag, &res);
                        break;
                case MARK_GETFL: {
                        int fflags, cflags;
                        input.and_flag = 0xffffffff;
                        input.or_flag = 0; 
                        error = presto_mark_dentry(input.path,
                                                   input.and_flag,
                                                   input.or_flag, &res);
                        if (error) 
                                break;
                        error = presto_mark_fset(input.path,
                                                   input.and_flag,
                                                   input.or_flag, &fflags);
                        if (error) 
                                break;
                        error = presto_mark_cache(input.path,
                                                   input.and_flag,
                                                   input.or_flag, &cflags);

                        if (error) 
                                break;
                        input.and_flag = fflags;
                        input.or_flag = cflags;
                	break;
                }
                default:
                        error = -EINVAL;
                }

                PRESTO_FREE(input.path, input.path_len + 1);
                if (error == -EBUSY) {
                        input.and_flag = error;
                        error = 0;
                }
                if (error) { 
                        EXIT;
                        return error;
                }
                /* return the correct cookie to wait for */
                input.mark_what = res;
                return copy_to_user((char *)arg, &input, sizeof(input));
        }

#ifdef  CONFIG_KREINT
        case PRESTO_REINT_BEGIN:
                return begin_kml_reint (file, arg);
        case PRESTO_DO_REINT:
                return do_kml_reint (file, arg);
        case PRESTO_REINT_END:
                return end_kml_reint (file, arg);
#endif

        case PRESTO_RELEASE_PERMIT: {
                int error;
                char *user_path;
                struct {
                        int  cookie;
                        int path_len;
                        char *path;
                } permit;
                
                error = copy_from_user(&permit, (char *)arg, sizeof(permit));
                if ( error ) {
                        EXIT;
                        return error;
                        }
                user_path = permit.path;
                
                PRESTO_ALLOC(permit.path, char *, permit.path_len + 1);
                if ( !permit.path ) {
                        EXIT;
                        return -ENOMEM;
                }
                error = copy_from_user(permit.path, user_path, permit.path_len);
                if ( error ) {
                        EXIT;
                        PRESTO_FREE(permit.path, permit.path_len + 1);
                        return error;
                }
                permit.path[permit.path_len] = '\0';
                
                CDEBUG(D_DOWNCALL, "release permit: %s, in cookie=%d\n",
                       permit.path, permit.cookie);
                error = presto_permit_downcall(permit.path, &permit.cookie);
                
                PRESTO_FREE(permit.path, permit.path_len + 1);
                if (error) {
                        EXIT;
                        return error;
                }
                /* return the correct cookie to wait for */
                return copy_to_user((char *)arg, &permit, sizeof(permit));
        }
        
        default:
                CDEBUG(D_PSDEV, "bad ioctl 0x%x, \n", cmd);
                CDEBUG(D_PSDEV, "valid are 0x%x - 0x%x, 0x%x - 0x%x \n",
                        PRESTO_GETMOUNT, PRESTO_GET_KMLSIZE,
                        PRESTO_VFS_SETATTR, PRESTO_VFS_IOPEN);
                EXIT;
        }

        return -EINVAL;
}


static int presto_psdev_open(struct inode * inode, struct file * file)
{
         struct upc_comm *upccom;
         ENTRY;

         if ( ! (upccom = presto_psdev_f2u(file)) ) {
                 kdev_t dev = file->f_dentry->d_inode->i_rdev;
                 printk("InterMezzo: %s, bad device %s\n",
                        __FUNCTION__, kdevname(dev));
                 EXIT;
                 return -EINVAL;
         }

        MOD_INC_USE_COUNT;

        CDEBUG(D_PSDEV, "Psdev_open: uc_pid: %d, caller: %d, flags: %d\n",
               upccom->uc_pid, current->pid, file->f_flags);

        EXIT;
        return 0;
}



static int presto_psdev_release(struct inode * inode, struct file * file)
{
        struct upc_comm *upccom;
        struct upc_req *req;
        struct list_head *lh;
        ENTRY;


        if ( ! (upccom = presto_psdev_f2u(file)) ) {
                kdev_t dev = file->f_dentry->d_inode->i_rdev;
                printk("InterMezzo: %s, bad device %s\n",
                       __FUNCTION__, kdevname(dev));
        }

        if ( upccom->uc_pid != current->pid ) {
                printk("psdev_release: Not lento.\n");
                MOD_DEC_USE_COUNT;
                return 0;
        }

        MOD_DEC_USE_COUNT;
        CDEBUG(D_PSDEV, "Lento: pid %d\n", current->pid);
        upccom->uc_pid = 0;

        /* Wake up clients so they can return. */
        CDEBUG(D_PSDEV, "Wake up clients sleeping for pending.\n");
        lh = &upccom->uc_pending;
        while ( (lh = lh->next) != &upccom->uc_pending) {
                req = list_entry(lh, struct upc_req, rq_chain);

                /* Async requests stay around for a new lento */
                if (req->rq_flags & REQ_ASYNC) {
                        continue;
                }
                /* the sleeper will free the req and data */
                req->rq_flags |= REQ_DEAD; 
                wake_up(&req->rq_sleep);
        }

        CDEBUG(D_PSDEV, "Wake up clients sleeping for processing\n");
        lh = &upccom->uc_processing;
        while ( (lh = lh->next) != &upccom->uc_processing) {
                req = list_entry(lh, struct upc_req, rq_chain);
                /* freeing of req and data is done by the sleeper */
                req->rq_flags |= REQ_DEAD; 
                wake_up(&req->rq_sleep);
        }
        CDEBUG(D_PSDEV, "Done.\n");

        EXIT;
        return 0;
}

static struct file_operations presto_psdev_fops = {
        read:    presto_psdev_read,
        write:   presto_psdev_write,
        poll:    presto_psdev_poll,
        ioctl:   presto_psdev_ioctl,
        open:    presto_psdev_open,
        release: presto_psdev_release
};


int  presto_psdev_init(void)
{
        int i;

#ifdef PRESTO_DEVEL
        if (register_chrdev(PRESTO_PSDEV_MAJOR, "intermezzo_psdev_devel",
                           &presto_psdev_fops)) {
                printk(KERN_ERR "presto_psdev: unable to get major %d\n",
                       PRESTO_PSDEV_MAJOR);
                return -EIO;
        }
#else
        if (register_chrdev(PRESTO_PSDEV_MAJOR, "intermezzo_psdev",
                           &presto_psdev_fops)) {
                printk("presto_psdev: unable to get major %d\n",
                       PRESTO_PSDEV_MAJOR);
                return -EIO;
        }
#endif

        memset(&upc_comms, 0, sizeof(upc_comms));
        for ( i = 0 ; i < MAX_PRESTODEV ; i++ ) {
                char *name;
                struct upc_comm *psdev = &upc_comms[i];
                INIT_LIST_HEAD(&psdev->uc_pending);
                INIT_LIST_HEAD(&psdev->uc_processing);
                INIT_LIST_HEAD(&psdev->uc_cache_list);
                init_waitqueue_head(&psdev->uc_waitq);
                psdev->uc_hard = 0;
                psdev->uc_no_filter = 0;
                psdev->uc_no_journal = 0;
                psdev->uc_no_upcall = 0;
                psdev->uc_timeout = 30;
                psdev->uc_errorval = 0;
                psdev->uc_minor = i;
                PRESTO_ALLOC(name, char *, strlen(PRESTO_PSDEV_NAME "256")+1);
                if (!name) { 
                        printk("Unable to allocate memory for device name\n");
                        continue;
                }
                sprintf(name, PRESTO_PSDEV_NAME "%d", i); 
                psdev->uc_devname = name;
        }
        return 0;
}

void presto_psdev_cleanup(void)
{
        int i;

        for ( i = 0 ; i < MAX_PRESTODEV ; i++ ) {
                struct upc_comm *psdev = &upc_comms[i];
                struct list_head *lh;

                if ( ! list_empty(&psdev->uc_pending)) { 
                        printk("Weird, tell Peter: module cleanup and pending list not empty dev %d\n", i);
                }
                if ( ! list_empty(&psdev->uc_processing)) { 
                        printk("Weird, tell Peter: module cleanup and processing list not empty dev %d\n", i);
                }
                if ( ! list_empty(&psdev->uc_cache_list)) { 
                        printk("Weird, tell Peter: module cleanup and cache listnot empty dev %d\n", i);
                }
                if (psdev->uc_devname) {
                        PRESTO_FREE(psdev->uc_devname,
                                    strlen(PRESTO_PSDEV_NAME "256")+1);
                }
                lh = psdev->uc_pending.next;
                while ( lh != &psdev->uc_pending) {
                        struct upc_req *req;

                        req = list_entry(lh, struct upc_req, rq_chain);
                        lh = lh->next;
                        if ( req->rq_flags & REQ_ASYNC ) {
                                list_del(&(req->rq_chain));
                                CDEBUG(D_UPCALL, "free pending upcall type %d\n",
                                       req->rq_opcode);
                                PRESTO_FREE(req->rq_data, req->rq_bufsize);
                                PRESTO_FREE(req, sizeof(struct upc_req));
                        } else {
                                req->rq_flags |= REQ_DEAD; 
                                wake_up(&req->rq_sleep);
                        }
                }
                lh = &psdev->uc_processing;
                while ( (lh = lh->next) != &psdev->uc_processing ) {
                        struct upc_req *req;
                        req = list_entry(lh, struct upc_req, rq_chain);
                        list_del(&(req->rq_chain));
                        req->rq_flags |= REQ_DEAD; 
                        wake_up(&req->rq_sleep);
                }
        }
}

/*
 * lento_upcall and lento_downcall routines
 */
static inline unsigned long lento_waitfor_upcall(struct upc_req *req,
                                                 int minor)
{
        DECLARE_WAITQUEUE(wait, current);
        unsigned long posttime;

        req->rq_posttime = posttime = jiffies;

        add_wait_queue(&req->rq_sleep, &wait);
        for (;;) {
                if ( upc_comms[minor].uc_hard == 0 )
                        current->state = TASK_INTERRUPTIBLE;
                else
                        current->state = TASK_UNINTERRUPTIBLE;

                /* got a reply */
                if ( req->rq_flags & (REQ_WRITE | REQ_DEAD) )
                        break;

                if ( !upc_comms[minor].uc_hard && signal_pending(current) ) {
                        /* if this process really wants to die, let it go */
                        if (sigismember(&(current->pending.signal), SIGKILL)||
                            sigismember(&(current->pending.signal), SIGINT) )
                                break;
                        /* signal is present: after timeout always return
                           really smart idea, probably useless ... */
                        if ( jiffies > req->rq_posttime +
                             upc_comms[minor].uc_timeout * HZ )
                                break;
                }
                schedule();

        }
      list_del(&req->rq_chain); 
      INIT_LIST_HEAD(&req->rq_chain); 
        remove_wait_queue(&req->rq_sleep, &wait);
        current->state = TASK_RUNNING;

        CDEBUG(D_SPECIAL, "posttime: %ld, returned: %ld\n",
               posttime, jiffies-posttime);
        return  (jiffies - posttime);

}

/*
 * lento_upcall will return an error in the case of
 * failed communication with Lento _or_ will peek at Lento
 * reply and return Lento's error.
 *
 * As lento has 2 types of errors, normal errors (positive) and internal
 * errors (negative), normal errors are negated, while internal errors
 * are all mapped to -EINTR, while showing a nice warning message. (jh)
 *
 * lento_upcall will always free buffer, either directly, when an upcall
 * is read (in presto_psdev_read), when the filesystem is unmounted, or
 * when the module is unloaded.
 */
int lento_upcall(int minor, int bufsize, int *rep_size, union up_args *buffer,
                 int async, struct upc_req *rq)
{
        unsigned long runtime;
        struct upc_comm *upc_commp;
        union down_args *out;
        struct upc_req *req;
        int error = 0;

        ENTRY;
        upc_commp = &(upc_comms[minor]);

        if (upc_commp->uc_no_upcall) {
                EXIT;
                goto exit_buf;
        }
        if (!upc_commp->uc_pid && !async) {
                EXIT;
                error = -ENXIO;
                goto exit_buf;
        }

        /* Format the request message. */
        CDEBUG(D_UPCALL, "buffer at %p, size %d\n", buffer, bufsize);
        PRESTO_ALLOC(req, struct upc_req *, sizeof(struct upc_req));
        if ( !req ) {
                EXIT;
                error = -ENOMEM;
                goto exit_buf;
        }
        req->rq_data = (void *)buffer;
        req->rq_flags = 0;
        req->rq_bufsize = bufsize;
        req->rq_rep_size = 0;
        req->rq_opcode = ((union up_args *)buffer)->uh.opcode;
        req->rq_unique = ++upc_commp->uc_seq;
        init_waitqueue_head(&req->rq_sleep);

        /* Fill in the common input args. */
        ((union up_args *)buffer)->uh.unique = req->rq_unique;
        /* Append msg to pending queue and poke Lento. */
        list_add(&req->rq_chain, upc_commp->uc_pending.prev);
        CDEBUG(D_UPCALL,
               "Proc %d waking Lento %d for(opc,uniq) =(%d,%d) msg at %p.\n",
               current->pid, upc_commp->uc_pid, req->rq_opcode,
               req->rq_unique, req);

        wake_up_interruptible(&upc_commp->uc_waitq);

        if ( async ) {
                req->rq_flags = REQ_ASYNC;
                if( rq != NULL ) {
                        *rq = *req; /* struct copying */
                }
                /* req, rq_data are freed in presto_psdev_read for async */
                EXIT;
                return 0;
        }

        /* We can be interrupted while we wait for Lento to process
         * our request.  If the interrupt occurs before Lento has read
         * the request, we dequeue and return. If it occurs after the
         * read but before the reply, we dequeue, send a signal
         * message, and return. If it occurs after the reply we ignore
         * it. In no case do we want to restart the syscall.  If it
         * was interrupted by a lento shutdown (psdev_close), return
         * ENODEV.  */

        /* Go to sleep.  Wake up on signals only after the timeout. */
        runtime = lento_waitfor_upcall(req, minor);

        CDEBUG(D_TIMING, "opc: %d time: %ld uniq: %d size: %d\n",
               req->rq_opcode, jiffies - req->rq_posttime,
               req->rq_unique, req->rq_rep_size);
        CDEBUG(D_UPCALL,
               "..process %d woken up by Lento for req at 0x%x, data at %x\n",
               current->pid, (int)req, (int)req->rq_data);

        if (upc_commp->uc_pid) {      /* i.e. Lento is still alive */
          /* Op went through, interrupt or not we go on */
            if (req->rq_flags & REQ_WRITE) {
                    out = (union down_args *)req->rq_data;
                    /* here we map positive Lento errors to kernel errors */
                    if ( out->dh.result < 0 ) {
                            printk("Tell Peter: Lento returns negative error %d, for oc %d!\n",
                                   out->dh.result, out->dh.opcode);
                          out->dh.result = EINVAL;
                    }
                    error = -out->dh.result;
                    CDEBUG(D_UPCALL, "upcall: (u,o,r) (%d, %d, %d) out at %p\n",
                           out->dh.unique, out->dh.opcode, out->dh.result, out);
                    *rep_size = req->rq_rep_size;
                    EXIT;
                    goto exit_req;
            }
            /* Interrupted before lento read it. */
            if ( !(req->rq_flags & REQ_READ) && signal_pending(current)) {
                    CDEBUG(D_UPCALL,
                           "Interrupt before read: (op,un)=(%d,%d), flags %x\n",
                           req->rq_opcode, req->rq_unique, req->rq_flags);
                    /* perhaps the best way to convince the app to give up? */
                    error = -EINTR;
                    EXIT;
                    goto exit_req;
            }

            /* interrupted after Lento did its read, send signal */
            if ( (req->rq_flags & REQ_READ) && signal_pending(current) ) {
                    union up_args *sigargs;
                    struct upc_req *sigreq;

                    CDEBUG(D_UPCALL,"Sending for: op = %d.%d, flags = %x\n",
                           req->rq_opcode, req->rq_unique, req->rq_flags);

                    error = -EINTR;

                    /* req, rq_data are freed in presto_psdev_read for async */
                    PRESTO_ALLOC(sigreq, struct upc_req *,
                                 sizeof (struct upc_req));
                    if (!sigreq) {
                            error = -ENOMEM;
                            EXIT;
                            goto exit_req;
                    }
                    PRESTO_ALLOC((sigreq->rq_data), char *,
                                 sizeof(struct lento_up_hdr));
                    if (!(sigreq->rq_data)) {
                            PRESTO_FREE(sigreq, sizeof (struct upc_req));
                            error = -ENOMEM;
                            EXIT;
                            goto exit_req;
                    }

                    sigargs = (union up_args *)sigreq->rq_data;
                    sigargs->uh.opcode = LENTO_SIGNAL;
                    sigargs->uh.unique = req->rq_unique;

                    sigreq->rq_flags = REQ_ASYNC;
                    sigreq->rq_opcode = sigargs->uh.opcode;
                    sigreq->rq_unique = sigargs->uh.unique;
                    sigreq->rq_bufsize = sizeof(struct lento_up_hdr);
                    sigreq->rq_rep_size = 0;
                    CDEBUG(D_UPCALL,
                           "presto_upcall: enqueing signal msg (%d, %d)\n",
                           sigreq->rq_opcode, sigreq->rq_unique);

                    /* insert at head of queue! */
                    list_add(&sigreq->rq_chain, &upc_commp->uc_pending);
                    wake_up_interruptible(&upc_commp->uc_waitq);
            } else {
                  printk("Lento: Strange interruption - tell Peter.\n");
                    error = -EINTR;
            }
        } else {        /* If lento died i.e. !UC_OPEN(upc_commp) */
                printk("presto_upcall: Lento dead on (op,un) (%d.%d) flags %d\n",
                       req->rq_opcode, req->rq_unique, req->rq_flags);
                error = -ENODEV;
        }

exit_req:
        PRESTO_FREE(req, sizeof(struct upc_req));
exit_buf:
        PRESTO_FREE(buffer, bufsize);
        return error;
}



/*
 *  presto's super.c
 *
 *  Copyright (C) 1998 Peter J. Braam
 *  Copyright (C) 2000 Stelias Computing, Inc.
 *  Copyright (C) 2000 Red Hat, Inc.
 *
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
#define __NO_VERSION__
#include <linux/module.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>

#ifdef PRESTO_DEBUG
long presto_vmemory = 0;
long presto_kmemory = 0;
#endif

extern struct presto_cache *presto_init_cache(void);
extern inline void presto_cache_add(struct presto_cache *cache, kdev_t dev);
extern inline void presto_init_cache_hash(void);

int presto_remount(struct super_block *, int *, char *);
extern ssize_t presto_file_write(struct file *file, const char *buf, 
                                 size_t size, loff_t *off);

/*
 *  Reading the super block.
 *
 *
 *
 */

/* returns an allocated string, copied out from data if opt is found */
static char *read_opt(const char *opt, char *data)
{
        char *value;
        char *retval;

        CDEBUG(D_SUPER, "option: %s, data %s\n", opt, data);
        if ( strncmp(opt, data, strlen(opt)) )
                return NULL;

        if ( (value = strchr(data, '=')) == NULL )
                return NULL;

        value++;
        PRESTO_ALLOC(retval, char *, strlen(value) + 1);
        if ( !retval ) {
                printk("InterMezzo: Out of memory!\n");
                return NULL;
        }

        strcpy(retval, value);
        CDEBUG(D_SUPER, "Assigned option: %s, value %s\n", opt, retval);
        return retval;
}

static void store_opt(char **dst, char *opt, char *defval)
{
        if (dst) {
                if (*dst) { 
                        PRESTO_FREE(*dst, strlen(*dst) + 1);
                }
                *dst = opt;
        } else {
                printk("presto: store_opt, error dst == NULL\n"); 
        }


        if (!opt && defval) {
                char *def_alloced; 
                PRESTO_ALLOC(def_alloced, char *, strlen(defval)+1);
                strcpy(def_alloced, defval);
                *dst = def_alloced; 
        }
}


/* Find the options for InterMezzo in "options", saving them into the
 * passed pointers.  If the pointer is null, the option is discarded.
 * Copy out all non-InterMezzo options into cache_data (to be passed
 * to the read_super operation of the cache).  The return value will
 * be a pointer to the end of the cache_data.
 */
static char *presto_options(char *options, char *cache_data,
                            char **cache_type, char **fileset,
                            char **prestodev,  char **mtpt)
{
        char *this_char;
        char *cache_data_end = cache_data;

        if (!options || !cache_data)
                return cache_data_end;

        /* set the defaults */ 
        store_opt(cache_type, NULL, "ext3"); 
        store_opt(prestodev, NULL, PRESTO_PSDEV_NAME "0"); 

        CDEBUG(D_SUPER, "parsing options\n");
        for (this_char = strtok (options, ",");
             this_char != NULL;
             this_char = strtok (NULL, ",")) {
                char *opt;
                CDEBUG(D_SUPER, "this_char %s\n", this_char);

                if ( (opt = read_opt("fileset", this_char)) ) {
                        store_opt(fileset, opt, NULL);
                        continue;
                }
                if ( (opt = read_opt("cache_type", this_char)) ) {
                        store_opt(cache_type, opt, "ext3");
                        continue;
                }
                if ( (opt = read_opt("mtpt", this_char)) ) {
                        store_opt(mtpt, opt, NULL);
                        continue;
                }
                if ( (opt = read_opt("prestodev", this_char)) ) {
                        store_opt(prestodev, opt, PRESTO_PSDEV_NAME);
                        continue;
                }

                cache_data_end += sprintf(cache_data_end, "%s%s",
                                          cache_data_end != cache_data ? ",":"",
                                          this_char);
        }

        return cache_data_end;
}

/*
    map a /dev/intermezzoX path to a minor:
    used to validate mount options passed to InterMezzo
 */
static int presto_get_minor(char *dev_path, int *minor)
{
        struct nameidata nd;
        struct dentry *dentry;
        kdev_t devno = 0;
        int error; 
        ENTRY;

        /* Special case for root filesystem - use minor 0 always. */
        if ( current->pid == 1 ) {
                *minor = 0;
                return 0;
        }

        error = presto_walk(dev_path, &nd);
        if (error) {
		EXIT;
                return error;
	}
        dentry = nd.dentry;

	error = -ENODEV;
        if (!dentry->d_inode) { 
		EXIT;
		goto out;
	}

        if (!S_ISCHR(dentry->d_inode->i_mode)) {
		EXIT;
		goto out;
	}

        devno = dentry->d_inode->i_rdev;
        if ( MAJOR(devno) != PRESTO_PSDEV_MAJOR ) { 
		EXIT;
		goto out;
	}

        if ( MINOR(devno) >= MAX_PRESTODEV ) {
		EXIT;
		goto out;
	}

	EXIT;
 out:
        *minor = MINOR(devno);
        path_release(&nd);
        return 0;
}

/* We always need to remove the presto options before passing to bottom FS */
struct super_block * presto_read_super(struct super_block * presto_sb,
                                       void * data, int silent)
{
        struct super_block *mysb = NULL;
        struct file_system_type *fstype;
        struct presto_cache *cache = NULL;
        char *cache_data = NULL;
        char *cache_data_end;
        char *cache_type = NULL;
        char *fileset = NULL;
        char *presto_mtpt = NULL;
        char *prestodev = NULL;
        struct filter_fs *ops;
        int minor;
        struct upc_comm *psdev;

        ENTRY;
        CDEBUG(D_MALLOC, "before parsing: kmem %ld, vmem %ld\n",
               presto_kmemory, presto_vmemory);

        /* reserve space for the cache's data */
        PRESTO_ALLOC(cache_data, void *, PAGE_SIZE);
        if ( !cache_data ) {
                printk("presto_read_super: Cannot allocate data page.\n");
                EXIT;
                goto out_err;
        }

        CDEBUG(D_SUPER, "mount opts: %s\n", data ? (char *)data : "(none)");

        /* read and validate options */
        cache_data_end = presto_options(data, cache_data, &cache_type, &fileset,
                                        &prestodev, &presto_mtpt);

        /* was there anything for the cache filesystem in the data? */
        if (cache_data_end == cache_data) {
                PRESTO_FREE(cache_data, PAGE_SIZE);
                cache_data = NULL;
        } else {
                CDEBUG(D_SUPER, "cache_data at %p is: %s\n", cache_data,
                       cache_data);
        }

        /* prepare the communication channel */
        if ( presto_get_minor(prestodev, &minor) ) {
                /* if (!silent) */
                printk("InterMezzo: %s not a valid presto dev\n", prestodev);
                EXIT;
                goto out_err;
        }
        psdev = &upc_comms[minor];
        CDEBUG(D_SUPER, "\n");
        psdev->uc_no_filter = 1;

        CDEBUG(D_SUPER, "presto minor is %d\n", minor);

        /* set up the cache */
        cache = presto_init_cache();
        if ( !cache ) {
                printk("presto_read_super: failure allocating cache.\n");
                EXIT;
                goto out_err;
        }

        /* no options were passed: likely we are "/" readonly */
        if ( !presto_mtpt || !fileset ) {
                cache->cache_flags |= CACHE_LENTO_RO | CACHE_CLIENT_RO;
        }
        cache->cache_psdev = psdev;
        /* no options were passed: likely we are "/" readonly */
        /* before the journaling infrastructure can work, these
           need to be set; that happens in presto_remount */
        if ( !presto_mtpt || !fileset ) {
                if (!presto_mtpt) 
                        printk("No mountpoint marking cache RO\n");
                if (!fileset) 
                        printk("No fileset marking cache RO\n");
                cache->cache_flags |= CACHE_LENTO_RO | CACHE_CLIENT_RO;
        }

        cache->cache_mtpt = presto_mtpt;
        cache->cache_root_fileset = fileset;
        cache->cache_type = cache_type;

        printk("Presto: type=%s, vol=%s, dev=%s (minor %d), mtpt %s, flags %x\n",
               cache_type, fileset ? fileset : "NULL", prestodev, minor,
               presto_mtpt ? presto_mtpt : "NULL", cache->cache_flags);


        MOD_INC_USE_COUNT;
        fstype = get_fs_type(cache_type);

        cache->cache_filter = filter_get_filter_fs((const char *)cache_type); 
        if ( !fstype || !cache->cache_filter) {
                printk("Presto: unrecognized fs type or cache type\n");
                MOD_DEC_USE_COUNT;
                EXIT;
                goto out_err;
        }
        mysb = fstype->read_super(presto_sb, cache_data, silent);
        /* this might have been freed above */
        if (cache_data) {
                PRESTO_FREE(cache_data, PAGE_SIZE);
                cache_data = NULL;
        }
        if ( !mysb ) {
                /* if (!silent) */
                printk("InterMezzo: cache mount failure.\n");
                MOD_DEC_USE_COUNT;
                EXIT;
                goto out_err;
        }

	cache->cache_sb = mysb;
        ops = filter_get_filter_fs(cache_type);

        filter_setup_journal_ops(cache->cache_filter, cache->cache_type); 

        /* we now know the dev of the cache: hash the cache */
        presto_cache_add(cache, mysb->s_dev);

        /* make sure we have our own super operations: mysb
           still contains the cache operations */
        filter_setup_super_ops(cache->cache_filter, mysb->s_op, 
                               &presto_super_ops);
        mysb->s_op = filter_c2usops(cache->cache_filter);

        /* now get our own directory operations */
        if ( mysb->s_root && mysb->s_root->d_inode ) {
                CDEBUG(D_SUPER, "\n");
                filter_setup_dir_ops(cache->cache_filter, 
                                     mysb->s_root->d_inode,
                                     &presto_dir_iops, &presto_dir_fops);
                mysb->s_root->d_inode->i_op = filter_c2udiops(cache->cache_filter);
                CDEBUG(D_SUPER, "lookup at %p\n", 
                       mysb->s_root->d_inode->i_op->lookup);
                filter_setup_dentry_ops(cache->cache_filter, 
                                        mysb->s_root->d_op, 
                                        &presto_dentry_ops);
                presto_sb->s_root->d_op = filter_c2udops(cache->cache_filter);
                cache->cache_mtde = mysb->s_root;
                presto_set_dd(mysb->s_root);
        }

        CDEBUG(D_MALLOC, "after mounting: kmem %ld, vmem %ld\n",
               presto_kmemory, presto_vmemory);

        EXIT;
        return mysb;

 out_err:
        CDEBUG(D_SUPER, "out_err called\n");
        if (cache)
                PRESTO_FREE(cache, sizeof(struct presto_cache));
        if (cache_data)
                PRESTO_FREE(cache_data, PAGE_SIZE);
        if (fileset)
                PRESTO_FREE(fileset, strlen(fileset) + 1);
        if (presto_mtpt)
                PRESTO_FREE(presto_mtpt, strlen(presto_mtpt) + 1);
        if (prestodev)
                PRESTO_FREE(prestodev, strlen(prestodev) + 1);
        if (cache_type)
                PRESTO_FREE(cache_type, strlen(cache_type) + 1);

        CDEBUG(D_MALLOC, "mount error exit: kmem %ld, vmem %ld\n",
               presto_kmemory, presto_vmemory);
        return NULL;
}

int presto_remount(struct super_block * sb, int *flags, char *data)
{
        char *cache_data = NULL;
        char *cache_data_end;
        char **type;
        char **fileset;
        char **mtpt;
        char **prestodev;
        struct super_operations *sops;
        struct presto_cache *cache = NULL;
        int err = 0;

        ENTRY;
        CDEBUG(D_MALLOC, "before remount: kmem %ld, vmem %ld\n",
               presto_kmemory, presto_vmemory);
        CDEBUG(D_SUPER, "remount opts: %s\n", data ? (char *)data : "(none)");
        if (data) {
                /* reserve space for the cache's data */
                PRESTO_ALLOC(cache_data, void *, PAGE_SIZE);
                if ( !cache_data ) {
                        err = -ENOMEM;
                        EXIT;
                        goto out_err;
                }
        }

        cache = presto_find_cache(sb->s_dev);
        if (!cache) {
                printk(__FUNCTION__ ": cannot find cache on remount\n");
                err = -ENODEV;
                EXIT;
                goto out_err;
        }

        /* If an option has not yet been set, we allow it to be set on
         * remount.  If an option already has a value, we pass NULL for
         * the option pointer, which means that the InterMezzo option
         * will be parsed but discarded.
         */
        type = cache->cache_type ? NULL : &cache->cache_type;
        fileset = cache->cache_root_fileset ? NULL : &cache->cache_root_fileset;
        prestodev = cache->cache_psdev ? NULL : &cache->cache_psdev->uc_devname;
        mtpt = cache->cache_mtpt ? NULL : &cache->cache_mtpt;
        cache_data_end = presto_options(data, cache_data, type, fileset,
                                        prestodev, mtpt);

        if (cache_data) {
                if (cache_data_end == cache_data) {
                        PRESTO_FREE(cache_data, PAGE_SIZE);
                        cache_data = NULL;
                } else {
                        CDEBUG(D_SUPER, "cache_data at %p is: %s\n", cache_data,
                               cache_data);
                }
        }

        if (cache->cache_root_fileset && cache->cache_mtpt) {
                cache->cache_flags &= ~(CACHE_LENTO_RO|CACHE_CLIENT_RO);
        }

        sops = filter_c2csops(cache->cache_filter);
        if (sops->remount_fs) {
                err = sops->remount_fs(sb, flags, cache_data);
        }

        CDEBUG(D_MALLOC, "after remount: kmem %ld, vmem %ld\n",
               presto_kmemory, presto_vmemory);
        EXIT;
out_err:
        if (cache_data)
                PRESTO_FREE(cache_data, PAGE_SIZE);
        return err;
}

struct file_system_type presto_fs_type = {
#ifdef PRESTO_DEVEL
        "izofs",
#else 
        "intermezzo",
#endif
        FS_REQUIRES_DEV, /* can use Ibaskets when ext2 does */
        presto_read_super,
        NULL
};


int /* __init */ init_intermezzo_fs(void)
{
        int status;

        printk(KERN_INFO "InterMezzo Kernel/Lento communications, "
               "v1.04, braam@inter-mezzo.org\n");

        status = presto_psdev_init();
        if ( status ) {
                printk("Problem (%d) in init_intermezzo_psdev\n", status);
                return status;
        }

        status = init_intermezzo_sysctl();
        if (status) {
                printk("presto: failed in init_intermezzo_sysctl!\n");
        }

        presto_init_cache_hash();
        presto_init_ddata_cache();

        status = register_filesystem(&presto_fs_type);
        if (status) {
                printk("presto: failed in register_filesystem!\n");
        }
        return status;
}


#ifdef MODULE
MODULE_AUTHOR("Peter J. Braam <braam@inter-mezzo.org>");
MODULE_DESCRIPTION("InterMezzo Kernel/Lento communications, v1.0.5.1");

int init_module(void)
{
        return init_intermezzo_fs();
}


void cleanup_module(void)
{
        int err;

        ENTRY;

        if ( (err = unregister_filesystem(&presto_fs_type)) != 0 ) {
                printk("presto: failed to unregister filesystem\n");
        }

        presto_psdev_cleanup();
        cleanup_intermezzo_sysctl();
        presto_cleanup_ddata_cache();

#ifdef PRESTO_DEVEL
        unregister_chrdev(PRESTO_PSDEV_MAJOR, "intermezzo_psdev_devel");
#else 
        unregister_chrdev(PRESTO_PSDEV_MAJOR, "intermezzo_psdev");
#endif
        CDEBUG(D_MALLOC, "after cleanup: kmem %ld, vmem %ld\n",
               presto_kmemory, presto_vmemory);
}

#endif


/*
 * Directory operations for InterMezzo filesystem
 * Original version: (C) 1996 P. Braam and M. Callahan
 * Rewritten for Linux 2.1. (C) 1997 Carnegie Mellon University
 *
 * Stelias encourages users to contribute improvements to
 * the InterMezzo project. Contact Peter Braam (coda@stelias.com).
 */

#define __NO_VERSION__
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/slab.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/string.h>

#include <linux/intermezzo_fs.h>

static int presto_dentry_revalidate(struct dentry *de, int );
static kmem_cache_t * presto_dentry_slab;

/* called when a cache lookup succeeds */
static int presto_dentry_revalidate(struct dentry *de, int flag)
{
	struct inode *inode = de->d_inode;
	ENTRY;
	if (!inode) {
		EXIT;
		return 1;
	}
	if (is_bad_inode(inode)) {
		EXIT;
		return 0;
	}

	if ( S_ISDIR(inode->i_mode) ) {
		EXIT;
		return (presto_chk(de, PRESTO_DATA) &&
			(presto_chk(de, PRESTO_ATTR)));
	} else {
		EXIT;
		return presto_chk(de, PRESTO_ATTR);
	}
}

static void presto_d_release(struct dentry *dentry)
{
        if (!presto_d2d(dentry)) {
                /* This should really only happen in the case of a dentry
                 * with no inode. */
                return;
        }

        presto_d2d(dentry)->dd_count--;

        if (! presto_d2d(dentry)->dd_count) {
                kmem_cache_free(presto_dentry_slab, presto_d2d(dentry));
		dentry->d_fsdata = NULL;
        }
}

struct dentry_operations presto_dentry_ops = 
{
	d_revalidate: presto_dentry_revalidate,
        d_release: presto_d_release
};


// XXX THIS DEPENDS ON THE KERNEL LOCK!

void presto_set_dd(struct dentry * dentry)
{
        ENTRY;
        if (dentry == NULL)
                BUG();

        if (dentry->d_fsdata) {
                printk("VERY BAD: dentry: %p\n", dentry);
                if (dentry->d_inode)
                        printk("    inode: %ld\n", dentry->d_inode->i_ino);
                EXIT;
                return;
        }

        if (dentry->d_inode == NULL) {
                dentry->d_fsdata = kmem_cache_alloc(presto_dentry_slab,
                                                    SLAB_KERNEL);
                memset(dentry->d_fsdata, 0, sizeof(struct presto_dentry_data));
                presto_d2d(dentry)->dd_count = 1;
                EXIT;
                return;
        }

        /* If there's already a dentry for this inode, share the data */
        if (dentry->d_alias.next != &dentry->d_inode->i_dentry ||
            dentry->d_alias.prev != &dentry->d_inode->i_dentry) {
                struct dentry *de;

                if (dentry->d_alias.next != &dentry->d_inode->i_dentry)
                        de = list_entry(dentry->d_alias.next, struct dentry,
                                        d_alias);
                else
                        de = list_entry(dentry->d_alias.prev, struct dentry,
                                        d_alias);

                dentry->d_fsdata = de->d_fsdata;
                presto_d2d(dentry)->dd_count++;
                EXIT;
                return;
        }

        dentry->d_fsdata = kmem_cache_alloc(presto_dentry_slab, SLAB_KERNEL);
        memset(dentry->d_fsdata, 0, sizeof(struct presto_dentry_data));
        presto_d2d(dentry)->dd_count = 1;
        EXIT;
        return; 
}

void presto_init_ddata_cache(void)
{
        ENTRY;
        presto_dentry_slab =
                kmem_cache_create("presto_cache",
                                  sizeof(struct presto_dentry_data), 0,
                                  SLAB_HWCACHE_ALIGN, NULL,
                                  NULL);
        EXIT;
}

void presto_cleanup_ddata_cache(void)
{
        kmem_cache_destroy(presto_dentry_slab);
}

/*
 *
 *
 *  Copyright (C) 2000 Stelias Computing, Inc.
 *  Copyright (C) 2000 Red Hat, Inc.
 *
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
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

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>

/*
   This file contains the routines associated with managing a
   cache of files for InterMezzo.  These caches have two reqs:
   - need to be found fast so they are hashed by the device, 
     with an attempt to have collision chains of length 1.
   The methods for the cache are set up in methods.
*/

/* the intent of this hash is to have collision chains of length 1 */
#define CACHES_BITS 8
#define CACHES_SIZE (1 << CACHES_BITS)
#define CACHES_MASK CACHES_SIZE - 1
static struct list_head presto_caches[CACHES_SIZE];

static inline int presto_cache_hash(kdev_t dev)
{
        return (CACHES_MASK) & ((0x000F & (dev)) + ((0x0F00 & (dev)) >>8));
}

inline void presto_cache_add(struct presto_cache *cache, kdev_t dev)
{
        list_add(&cache->cache_chain,
                 &presto_caches[presto_cache_hash(dev)]);
        cache->cache_dev = dev;
}

inline void presto_init_cache_hash(void)
{
        int i;
        for ( i = 0; i < CACHES_SIZE; i++ ) {
                INIT_LIST_HEAD(&presto_caches[i]);
        }
}

/* map a device to a cache */
struct presto_cache *presto_find_cache(kdev_t dev)
{
        struct presto_cache *cache;
        struct list_head *lh, *tmp;

        lh = tmp = &(presto_caches[presto_cache_hash(dev)]);
        while ( (tmp = lh->next) != lh ) {
                cache = list_entry(tmp, struct presto_cache, cache_chain);
                if ( cache->cache_dev == dev ) {
                        return cache;
                }
        }
        return NULL;
}


/* map an inode to a cache */
struct presto_cache *presto_get_cache(struct inode *inode)
{
        struct presto_cache *cache;

        /* find the correct presto_cache here, based on the device */
        cache = presto_find_cache(inode->i_dev);
        if ( !cache ) {
                printk("WARNING: no presto cache for dev %x, ino %ld\n",
                       inode->i_dev, inode->i_ino);
                EXIT;
                return NULL;
        }
        return cache;
}


/* list cache mount points for ioctl's or /proc/fs/intermezzo/mounts */
int presto_sprint_mounts(char *buf, int buflen, int minor)
{
        int len = 0;
        int i;
        struct list_head *head, *tmp;
        struct presto_cache *cache;

        buf[0] = '\0';
        for (i=0 ; i<CACHES_SIZE ; i++) {
                head = tmp = &presto_caches[i];
                while ( (tmp = tmp->next) != head ) {
                        cache = list_entry(tmp, struct presto_cache,
                                            cache_chain);
                        if ( !cache->cache_root_fileset || !cache->cache_mtpt)
                                continue;
                        if ((minor != -1) &&
                            (cache->cache_psdev->uc_minor != minor))
                                continue;
                        if ( strlen(cache->cache_root_fileset) +
                             strlen(cache->cache_mtpt) + 
                             strlen(cache->cache_psdev->uc_devname) +
                             4 > buflen - len)
                                break;
                        len += sprintf(buf + len, "%s %s %s\n",
                                       cache->cache_root_fileset,
                                       cache->cache_mtpt,
                                       cache->cache_psdev->uc_devname);
                }
        }

        buf[buflen-1] = '\0';
        CDEBUG(D_SUPER, "%s\n", buf);
        return len;
}

#ifdef CONFIG_KREINT
/* get mount point by volname
       Arthur Ma, 2000.12.25
 */
int presto_get_mount (char *buf, int buflen, char *volname)
{
        int i;
        struct list_head *head, *tmp;
        struct presto_cache *cache = NULL;
        char *path = "";

        buf[0] = '\0';
        for (i=0 ; i<CACHES_SIZE ; i++) {
                head = tmp = &presto_caches[i];
                while ( (tmp = tmp->next) != head ) {
                        cache = list_entry(tmp, struct presto_cache,
                                            cache_chain);
                        if ( !cache->cache_root_fileset || !cache->cache_mtpt)
                                continue;
                        if ( strcmp(cache->cache_root_fileset, volname) == 0)
                                break;
                }
        }
        if (cache != NULL)
                path = cache->cache_mtpt;
        strncpy (buf, path, buflen);
        return strlen (buf);
}
#endif

/* another debugging routine: check fs is InterMezzo fs */
int presto_ispresto(struct inode *inode)
{
        struct presto_cache *cache;

        if ( !inode )
                return 0;
        cache = presto_get_cache(inode);
        if ( !cache )
                return 0;
        return (inode->i_dev == cache->cache_dev);
}

/* setup a cache structure when we need one */
struct presto_cache *presto_init_cache(void)
{
        struct presto_cache *cache;

        /* make a presto_cache structure for the hash */
        PRESTO_ALLOC(cache, struct presto_cache *, sizeof(struct presto_cache));
        if ( cache ) {
                memset(cache, 0, sizeof(struct presto_cache));
                INIT_LIST_HEAD(&cache->cache_chain);
                INIT_LIST_HEAD(&cache->cache_fset_list);
        }
	cache->cache_lock = SPIN_LOCK_UNLOCKED;
	cache->cache_reserved = 0; 
        return cache;
}


/* free a cache structure and all of the memory it is pointing to */
inline void presto_free_cache(struct presto_cache *cache)
{
        if (!cache)
                return;

        list_del(&cache->cache_chain);
        if (cache->cache_mtpt)
                PRESTO_FREE(cache->cache_mtpt, strlen(cache->cache_mtpt) + 1);
        if (cache->cache_type)
                PRESTO_FREE(cache->cache_type, strlen(cache->cache_type) + 1);
        if (cache->cache_root_fileset)
                PRESTO_FREE(cache->cache_root_fileset, strlen(cache->cache_root_fileset) + 1);

        PRESTO_FREE(cache, sizeof(struct presto_cache));
}

int presto_reserve_space(struct presto_cache *cache, loff_t req)
{
        struct filter_fs *filter; 
        loff_t avail; 
	struct super_block *sb = cache->cache_sb;
        filter = cache->cache_filter;
	if (!filter ) {
		EXIT;
		return 0; 
	}
	if (!filter->o_trops ) {
		EXIT;
		return 0; 
	}
	if (!filter->o_trops->tr_avail ) {
		EXIT;
		return 0; 
	}
        avail = filter->o_trops->tr_avail(cache, sb); 
        CDEBUG(D_SUPER, "ESC::%ld +++> %ld \n", (long) cache->cache_reserved,
	         (long) (cache->cache_reserved + req)); 
        CDEBUG(D_SUPER, "ESC::Avail::%ld \n", (long) avail);
	spin_lock(&cache->cache_lock);
        if (req + cache->cache_reserved > avail) {
		spin_unlock(&cache->cache_lock);
                EXIT;
                return -ENOSPC;
        }
	cache->cache_reserved += req; 
	spin_unlock(&cache->cache_lock);

        return 0;
}

void presto_release_space(struct presto_cache *cache, loff_t req)
{
        CDEBUG(D_SUPER, "ESC::%ld ---> %ld \n", (long) cache->cache_reserved,
	         (long) (cache->cache_reserved - req)); 
	spin_lock(&cache->cache_lock);
	cache->cache_reserved -= req; 
	spin_unlock(&cache->cache_lock);
}

/*
 * Cache operations for Coda.
 * For Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/list.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_psdev.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_cache.h>

/* replace or extend an acl cache hit */
void coda_cache_enter(struct inode *inode, int mask)
{
	struct coda_inode_info *cii = ITOC(inode);
        ENTRY;

        if ( !coda_cred_ok(&cii->c_cached_cred) ) {
                coda_load_creds(&cii->c_cached_cred);
                cii->c_cached_perm = mask;
        } else
                cii->c_cached_perm |= mask;
}

/* remove cached acl from an inode */
void coda_cache_clear_inode(struct inode *inode)
{
	struct coda_inode_info *cii = ITOC(inode);
	ENTRY;
        cii->c_cached_perm = 0;
}

/* remove all acl caches for a principal (or all principals when cred == NULL)*/
void coda_cache_clear_all(struct super_block *sb, struct coda_cred *cred)
{
        struct coda_sb_info *sbi;
        struct coda_inode_info *cii;
        struct list_head *tmp;

        ENTRY;
        sbi = coda_sbp(sb);
        if (!sbi) BUG();

        list_for_each(tmp, &sbi->sbi_cihead)
        {
		cii = list_entry(tmp, struct coda_inode_info, c_cilist);
                if ( cii->c_magic != CODA_CNODE_MAGIC ) BUG();

                if (!cred || coda_cred_eq(cred, &cii->c_cached_cred))
                        cii->c_cached_perm = 0;
	}
}


/* check if the mask has been matched against the acl already */
int coda_cache_check(struct inode *inode, int mask)
{
	struct coda_inode_info *cii = ITOC(inode);
        int hit;
	
        hit = ((mask & cii->c_cached_perm) == mask) &&
                coda_cred_ok(&cii->c_cached_cred);

        CDEBUG(D_CACHE, "%s for ino %ld\n", hit ? "HIT" : "MISS", inode->i_ino);
        return hit;
}


/* Purging dentries and children */
/* The following routines drop dentries which are not
   in use and flag dentries which are in use to be 
   zapped later.

   The flags are detected by:
   - coda_dentry_revalidate (for lookups) if the flag is C_PURGE
   - coda_dentry_delete: to remove dentry from the cache when d_count
     falls to zero
   - an inode method coda_revalidate (for attributes) if the 
     flag is C_VATTR
*/

/* 
   Some of this is pretty scary: what can disappear underneath us?
   - shrink_dcache_parent calls on purge_one_dentry which is safe:
     it only purges children.
   - dput is evil since it  may recurse up the dentry tree
 */

void coda_purge_dentries(struct inode *inode)
{
	if (!inode)
		return ;

	/* better safe than sorry: dput could kill us */
	iget(inode->i_sb, inode->i_ino);
	/* catch the dentries later if some are still busy */
	coda_flag_inode(inode, C_PURGE);
	d_prune_aliases(inode);
	iput(inode);
}

/* this won't do any harm: just flag all children */
static void coda_flag_children(struct dentry *parent, int flag)
{
	struct list_head *child;
	struct dentry *de;

	spin_lock(&dcache_lock);
	list_for_each(child, &parent->d_subdirs)
	{
		de = list_entry(child, struct dentry, d_child);
		/* don't know what to do with negative dentries */
		if ( ! de->d_inode ) 
			continue;
		CDEBUG(D_DOWNCALL, "%d for %*s/%*s\n", flag, 
		       de->d_name.len, de->d_name.name, 
		       de->d_parent->d_name.len, de->d_parent->d_name.name);
		coda_flag_inode(de->d_inode, flag);
	}
	spin_unlock(&dcache_lock);
	return; 
}

void coda_flag_inode_children(struct inode *inode, int flag)
{
	struct dentry *alias_de;

	ENTRY;
	if ( !inode || !S_ISDIR(inode->i_mode)) 
		return; 

	alias_de = d_find_alias(inode);
	if (!alias_de)
		return;
	coda_flag_children(alias_de, flag);
	shrink_dcache_parent(alias_de);
	dput(alias_de);
}


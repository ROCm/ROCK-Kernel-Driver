/*
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Howells <dhowells@redhat.com>
 *          David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "vnode.h"
#include "volume.h"
#include "cell.h"
#include "cmservice.h"
#include "fsclient.h"
#include "super.h"
#include "internal.h"

#define AFS_FS_MAGIC 0x6B414653 /* 'kAFS' */

static inline char *strdup(const char *s)
{
	char *ns = kmalloc(strlen(s)+1,GFP_KERNEL);
	if (ns)
		strcpy(ns,s);
	return ns;
}

static void afs_i_init_once(void *foo, kmem_cache_t *cachep, unsigned long flags);

static struct super_block *afs_get_sb(struct file_system_type *fs_type,
				      int flags, char *dev_name, void *data);

static struct inode *afs_alloc_inode(struct super_block *sb);

static void afs_put_super(struct super_block *sb);

static void afs_destroy_inode(struct inode *inode);

static struct file_system_type afs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "afs",
	.get_sb		= afs_get_sb,
	.kill_sb	= kill_anon_super,
};

static struct super_operations afs_super_ops = {
	.statfs		= simple_statfs,
	.alloc_inode	= afs_alloc_inode,
	.drop_inode	= generic_delete_inode,
	.destroy_inode	= afs_destroy_inode,
	.clear_inode	= afs_clear_inode,
	.put_super	= afs_put_super,
};

static kmem_cache_t *afs_inode_cachep;

/*****************************************************************************/
/*
 * initialise the filesystem
 */
int __init afs_fs_init(void)
{
	int ret;

	kenter("");

	/* create ourselves an inode cache */
	ret = -ENOMEM;
	afs_inode_cachep = kmem_cache_create("afs_inode_cache",
						sizeof(afs_vnode_t),
						0,
						SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
						afs_i_init_once,
						NULL);
	if (!afs_inode_cachep) {
		printk(KERN_NOTICE "kAFS: Failed to allocate inode cache\n");
		return ret;
	}

	/* now export our filesystem to lesser mortals */
	ret = register_filesystem(&afs_fs_type);
	if (ret<0) {
		kmem_cache_destroy(afs_inode_cachep);
		kleave(" = %d",ret);
		return ret;
	}

	kleave(" = 0");
	return 0;
} /* end afs_fs_init() */

/*****************************************************************************/
/*
 * clean up the filesystem
 */
void __exit afs_fs_exit(void)
{
	/* destroy our private inode cache */
	kmem_cache_destroy(afs_inode_cachep);

	unregister_filesystem(&afs_fs_type);

} /* end afs_fs_exit() */

/*****************************************************************************/
/*
 * check that an argument has a value
 */
static int want_arg(char **_value, const char *option)
{
	if (!_value || !*_value || !**_value) {
		printk(KERN_NOTICE "kAFS: %s: argument missing\n",option);
		return 0;
	}
	return 1;
} /* end want_arg() */

/*****************************************************************************/
/*
 * check that there is a value
 */
#if 0
static int want_value(char **_value, const char *option)
{
	if (!_value || !*_value || !**_value) {
		printk(KERN_NOTICE "kAFS: %s: argument incomplete\n",option);
		return 0;
	}
	return 1;
} /* end want_value() */
#endif

/*****************************************************************************/
/*
 * check that there's no subsequent value
 */
static int want_no_value(char *const *_value, const char *option)
{
	if (*_value && **_value) {
		printk(KERN_NOTICE "kAFS: %s: Invalid argument: %s\n",option,*_value);
		return 0;
	}
	return 1;
} /* end want_no_value() */

/*****************************************************************************/
/*
 * extract a number from an option string value
 */
#if 0
static int want_number(char **_value, const char *option, unsigned long *number,
		       unsigned long limit)
{
	char *value = *_value;

	if (!want_value(_value,option))
		return 0;

	*number = simple_strtoul(value,_value,0);

	if (value==*_value) {
		printk(KERN_NOTICE "kAFS: %s: Invalid number: %s\n",option,value);
		return 0;
	}

	if (*number>limit) {
		printk(KERN_NOTICE "kAFS: %s: numeric value %lu > %lu\n",option,*number,limit);
		return 0;
	}

	return 1;
} /* end want_number() */
#endif

/*****************************************************************************/
/*
 * extract a separator from an option string value
 */
#if 0
static int want_sep(char **_value, const char *option, char sep)
{
	if (!want_value(_value,option))
		return 0;

	if (*(*_value)++ != sep) {
		printk(KERN_NOTICE "kAFS: %s: '%c' expected: %s\n",option,sep,*_value-1);
		return 0;
	}

	return 1;
} /* end want_number() */
#endif

/*****************************************************************************/
/*
 * extract an IP address from an option string value
 */
#if 0
static int want_ipaddr(char **_value, const char *option, struct in_addr *addr)
{
	unsigned long number[4];

	if (!want_value(_value,option))
		return 0;

	if (!want_number(_value,option,&number[0],255) ||
	    !want_sep(_value,option,'.') ||
	    !want_number(_value,option,&number[1],255) ||
	    !want_sep(_value,option,'.') ||
	    !want_number(_value,option,&number[2],255) ||
	    !want_sep(_value,option,'.') ||
	    !want_number(_value,option,&number[3],255))
		return 0;

	((u8*)addr)[0] = number[0];
	((u8*)addr)[1] = number[1];
	((u8*)addr)[2] = number[2];
	((u8*)addr)[3] = number[3];

	return 1;
} /* end want_numeric() */
#endif

/*****************************************************************************/
/*
 * parse the mount options
 * - this function has been shamelessly adapted from the ext3 fs which shamelessly adapted it from
 *   the msdos fs
 */
static int afs_super_parse_options(struct afs_super_info *as, char *options, char **devname)
{
	char *key, *value;
	int ret;

	_enter("%s",options);

	ret = 0;
	while ((key = strsep(&options,",")))
	{
		value = strchr(key,'=');
		if (value)
			*value++ = 0;

		printk("kAFS: KEY: %s, VAL:%s\n",key,value?:"-");

		if (strcmp(key,"rwpath")==0) {
			if (!want_no_value(&value,"rwpath")) return -EINVAL;
			as->rwparent = 1;
			continue;
		}
		else if (strcmp(key,"vol")==0) {
			if (!want_arg(&value,"vol")) return -EINVAL;
			*devname = value;
			continue;
		}

#if 0
		if (strcmp(key,"servers")==0) {
			if (!want_arg(&value,"servers")) return -EINVAL;

			_debug("servers=%s",value);

			for (;;) {
				struct in_addr addr;

				if (!want_ipaddr(&value,"servers",&addr))
					return -EINVAL;

				ret = afs_create_server(as->cell,&addr,&as->server);
				if (ret<0) {
					printk("kAFS: unable to create server: %d\n",ret);
					return ret;
				}

				if (!*value)
					break;

				if (as->server) {
					printk(KERN_NOTICE
					       "kAFS: only one server can be specified\n");
					return -EINVAL;
				}

				if (!want_sep(&value,"servers",':'))
					return -EINVAL;
			}
			continue;
		}
#endif

		printk("kAFS: Unknown mount option: '%s'\n",key);
		ret = -EINVAL;
		goto error;
	}

	ret = 0;

 error:
	_leave(" = %d",ret);

	return ret;
} /* end afs_super_parse_options() */

/*****************************************************************************/
/*
 * fill in the superblock
 */
static int afs_fill_super(struct super_block *sb, void *_data, int silent)
{
	struct afs_super_info *as = NULL;
	struct dentry *root = NULL;
	struct inode *inode = NULL;
	afs_fid_t fid;
	void **data = _data;
	char *options, *devname;
	int ret;

	_enter("");

	if (!data) {
		_leave(" = -EINVAL");
		return -EINVAL;
	}
	devname = data[0];
	options = data[1];
	if (options)
		options[PAGE_SIZE-1] = 0;

	/* allocate a superblock info record */
	as = kmalloc(sizeof(struct afs_super_info),GFP_KERNEL);
	if (!as) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	memset(as,0,sizeof(struct afs_super_info));

	/* parse the options */
	if (options) {
		ret = afs_super_parse_options(as,options,&devname);
		if (ret<0)
			goto error;
		if (!devname) {
			printk("kAFS: no volume name specified\n");
			ret = -EINVAL;
			goto error;
		}
	}

	/* parse the device name */
	ret = afs_volume_lookup(devname,as->rwparent,&as->volume);
	if (ret<0)
		goto error;

	/* fill in the superblock */
	sb->s_blocksize		= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic		= AFS_FS_MAGIC;
	sb->s_op		= &afs_super_ops;
	sb->s_fs_info		= as;

	/* allocate the root inode and dentry */
	fid.vid		= as->volume->vid;
	fid.vnode	= 1;
	fid.unique	= 1;
	ret = afs_iget(sb,&fid,&inode);
	if (ret<0)
		goto error;

	ret = -ENOMEM;
	root = d_alloc_root(inode);
	if (!root)
		goto error;

	sb->s_root = root;

	_leave(" = 0");
	return 0;

 error:
	if (root) dput(root);
	if (inode) iput(inode);
	if (as) {
		if (as->volume)		afs_put_volume(as->volume);
		kfree(as);
	}
	sb->s_fs_info = NULL;

	_leave(" = %d",ret);
	return ret;
} /* end afs_fill_super() */

/*****************************************************************************/
/*
 * get an AFS superblock
 * - TODO: don't use get_sb_nodev(), but rather call sget() directly
 */
static struct super_block *afs_get_sb(struct file_system_type *fs_type,
				      int flags,
				      char *dev_name,
				      void *options)
{
	struct super_block *sb;
	void *data[2] = { dev_name, options };
	int ret;

	_enter(",,%s,%p",dev_name,options);

	/* start the cache manager */
	ret = afscm_start();
	if (ret<0) {
		_leave(" = %d",ret);
		return ERR_PTR(ret);
	}

	/* allocate a deviceless superblock */
	sb = get_sb_nodev(fs_type,flags,data,afs_fill_super);
	if (IS_ERR(sb)) {
		afscm_stop();
		return sb;
	}

	_leave("");
	return sb;
} /* end afs_get_sb() */

/*****************************************************************************/
/*
 * finish the unmounting process on the superblock
 */
static void afs_put_super(struct super_block *sb)
{
	struct afs_super_info *as = sb->s_fs_info;

	_enter("");

	if (as) {
		if (as->volume)		afs_put_volume(as->volume);
	}

	/* stop the cache manager */
	afscm_stop();

	_leave("");
} /* end afs_put_super() */

/*****************************************************************************/
/*
 * initialise an inode cache slab element prior to any use
 */
static void afs_i_init_once(void *_vnode, kmem_cache_t *cachep, unsigned long flags)
{
	afs_vnode_t *vnode = (afs_vnode_t *) _vnode;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) == SLAB_CTOR_CONSTRUCTOR) {
		memset(vnode,0,sizeof(*vnode));
		inode_init_once(&vnode->vfs_inode);
		init_waitqueue_head(&vnode->update_waitq);
		spin_lock_init(&vnode->lock);
		INIT_LIST_HEAD(&vnode->cb_link);
		INIT_LIST_HEAD(&vnode->cb_hash_link);
		afs_timer_init(&vnode->cb_timeout,&afs_vnode_cb_timed_out_ops);
	}

} /* end afs_i_init_once() */

/*****************************************************************************/
/*
 * allocate an AFS inode struct from our slab cache
 */
static struct inode *afs_alloc_inode(struct super_block *sb)
{
	afs_vnode_t *vnode;

	vnode = (afs_vnode_t *) kmem_cache_alloc(afs_inode_cachep,SLAB_KERNEL);
	if (!vnode)
		return NULL;

	memset(&vnode->fid,0,sizeof(vnode->fid));
	memset(&vnode->status,0,sizeof(vnode->status));

	vnode->volume = NULL;
	vnode->update_cnt = 0;
	vnode->flags = 0;

	return &vnode->vfs_inode;
} /* end afs_alloc_inode() */

/*****************************************************************************/
/*
 * destroy an AFS inode struct
 */
static void afs_destroy_inode(struct inode *inode)
{
	_enter("{%lu}",inode->i_ino);
	kmem_cache_free(afs_inode_cachep, AFS_FS_I(inode));
} /* end afs_destroy_inode() */

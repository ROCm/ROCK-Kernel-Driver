/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Perm (short for "permissions") plugins common stuff. */

#if !defined( __REISER4_PERM_H__ )
#define __REISER4_PERM_H__

#include "../../forward.h"
#include "../plugin_header.h"

#include <linux/types.h>
#include <linux/fs.h>		/* for struct file  */
#include <linux/dcache.h>	/* for struct dentry */

/* interface for perm plugin.

   Perm plugin method can be implemented through:

    1. consulting ->i_mode bits in stat data

    2. obtaining acl from the tree and inspecting it

    3. asking some kernel module or user-level program to authorize access.

   This allows for integration with things like capabilities, SELinux-style
   secutiry contexts, etc.

*/
/* NIKITA-FIXME-HANS: define what this is targeted for.  It does not seem to be intended for use with sys_reiser4.  Explain. */
typedef struct perm_plugin {
	/* generic plugin fields */
	plugin_header h;

	/* check permissions for read/write */
	int (*read_ok) (struct file * file, const char *buf, size_t size, loff_t * off);
	int (*write_ok) (struct file * file, const char *buf, size_t size, loff_t * off);

	/* check permissions for lookup */
	int (*lookup_ok) (struct inode * parent, struct dentry * dentry);

	/* check permissions for create */
	int (*create_ok) (struct inode * parent, struct dentry * dentry, reiser4_object_create_data * data);

	/* check permissions for linking @where to @existing */
	int (*link_ok) (struct dentry * existing, struct inode * parent, struct dentry * where);

	/* check permissions for unlinking @victim from @parent */
	int (*unlink_ok) (struct inode * parent, struct dentry * victim);

	/* check permissions for deletion of @object whose last reference is
	   by @parent */
	int (*delete_ok) (struct inode * parent, struct dentry * victim);
	int (*mask_ok) (struct inode * inode, int mask);
	/* check whether attribute change is acceptable */
	int (*setattr_ok) (struct dentry * dentry, struct iattr * attr);

	/* check whether stat(2) is allowed */
	int (*getattr_ok) (struct vfsmount * mnt UNUSED_ARG, struct dentry * dentry, struct kstat * stat);
	/* check whether rename(2) is allowed */
	int (*rename_ok) (struct inode * old_dir, struct dentry * old,
			  struct inode * new_dir, struct dentry * new);
} perm_plugin;
/* NIKITA-FIXME-HANS: I really hate things like this that kill the ability of Meta-. to work.  Please eliminate this macro, exce */
/* call ->check_ok method of perm plugin for inode */
#define perm_chk(inode, check, ...)			\
({							\
	perm_plugin *perm;				\
							\
	perm = inode_perm_plugin(inode);		\
	(perm == NULL || perm->check ## _ok == NULL) ?	\
		0 :					\
		perm->check ## _ok(__VA_ARGS__);	\
})

typedef enum { RWX_PERM_ID, LAST_PERM_ID } reiser4_perm_id;

/* __REISER4_PERM_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/

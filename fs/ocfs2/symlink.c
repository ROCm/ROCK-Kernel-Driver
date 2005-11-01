/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 *  linux/cluster/ssi/cfs/symlink.c
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE
 *	or NON INFRINGEMENT.  See the GNU General Public License for more
 *	details.
 *
 * 	You should have received a copy of the GNU General Public License
 * 	along with this program; if not, write to the Free Software
 * 	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	Questions/Comments/Bugfixes to ssic-linux-devel@lists.sourceforge.net
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  Jun 7 1999, cache symlink lookups in the page cache.  -DaveM
 *
 *  Portions Copyright (C) 2001 Compaq Computer Corporation
 *
 *  ocfs2 symlink handling code, including CDSL support
 *
 *  Copyright (C) 2004, 2005 Oracle.
 *
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/utsname.h>

#define MLOG_MASK_PREFIX ML_NAMEI
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "symlink.h"

#include "buffer_head_io.h"

static char *ocfs2_page_getlink(struct dentry * dentry,
				struct page **ppage);
static char *ocfs2_fast_symlink_getlink(struct inode *inode,
					struct buffer_head **bh);

/* get the link contents into pagecache */
static char *ocfs2_page_getlink(struct dentry * dentry,
				struct page **ppage)
{
	struct page * page;
	struct address_space *mapping = dentry->d_inode->i_mapping;
	page = read_cache_page(mapping, 0,
			       (filler_t *)mapping->a_ops->readpage, NULL);
	if (IS_ERR(page))
		goto sync_fail;
	wait_on_page_locked(page);
	if (!PageUptodate(page))
		goto async_fail;
	*ppage = page;
	return kmap(page);

async_fail:
	page_cache_release(page);
	return ERR_PTR(-EIO);

sync_fail:
	return (char*)page;
}

static char *ocfs2_fast_symlink_getlink(struct inode *inode,
					struct buffer_head **bh)
{
	int status;
	char *link = NULL;
	ocfs2_dinode *fe;

	mlog_entry_void();

	status = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				  OCFS2_I(inode)->ip_blkno,
				  bh,
				  OCFS2_BH_CACHED,
				  inode);
	if (status < 0) {
		mlog_errno(status);
		link = ERR_PTR(status);
		goto bail;
	}

	fe = (ocfs2_dinode *) (*bh)->b_data;
	link = (char *) fe->id2.i_symlink;
bail:
	mlog_exit(status);

	return link;
}

static int ocfs2_readlink(struct dentry *dentry,
			  char __user *buffer,
			  int buflen)
{
	int ret;
	char *link;
	struct buffer_head *bh = NULL;
	struct inode *inode = dentry->d_inode;

	mlog_entry_void();

	link = ocfs2_fast_symlink_getlink(inode, &bh);
	if (IS_ERR(link)) {
		ret = PTR_ERR(link);
		goto out;
	}

	ret = vfs_readlink(dentry, buffer, buflen, link);

	brelse(bh);
out:
	mlog_exit(ret);
	return ret;
}

#ifdef OCFS2_CDSL

struct ocfs2_symlink_ops {
	const char *name;
	const unsigned int len;
	unsigned int (*subst_fn) (char *str, void *data);
};

/**
 *** sym_hostname - Substitute system host name
 *** @str: String for result
 *** @len: Length of result buffer
 ***
 *** Returns: Length of hostname
 ***/
static unsigned int
sym_hostname(char *str, void *data)
{
	  unsigned int l = strlen(system_utsname.nodename);

	  if (str)
		memcpy(str, system_utsname.nodename, l);

	  return l;
}

/**
 *** sym_machine - Substitute machine type
 *** @str: String for result
 *** @len: Length of result buffer
 ***
 *** Returns: Length of machine type
 ***/

static unsigned int
sym_machine(char *str, void *data)
{
	unsigned int l = strlen(system_utsname.machine);

	if (str)
	       memcpy(str, system_utsname.machine, l);

	return l;
}

/**
 *** sym_os - Substitute OS name
 *** @str: String for result
 *** @len: Length of result buffer
 ***
 *** Returns: Length of OS name
 ***/

static unsigned int
sym_os(char *str, void *data)
{
	unsigned int l = strlen(system_utsname.sysname);

	if (str)
	       memcpy(str, system_utsname.sysname, l);

	return l;
}

/**
 *** sym_nodenum - Substitute node number
 *** @str: String for result
 *** @len: Length of result buffer
 ***
 *** Returns: Length of  nodeNum
 ***/

static unsigned int
sym_nodenum(char *str, void *data)
{
	unsigned int l;
	char buf[10];
	struct inode *inode = data;
	ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	l = sprintf(buf, "%lu", (unsigned long)osb->node_num);

	if (str) {
	      memcpy(str, buf, l);
	      str[l] = '\0';
	}

	return l;
}

static unsigned int
sym_system(char *str, void *data)
{
	unsigned int ml = strlen(system_utsname.machine);
	unsigned int sl = strlen(system_utsname.sysname);
	unsigned int l = ml + sl + 1;

	if (str) {
	       memcpy(str, system_utsname.machine, ml);
	       str[ml] = '_';
	       memcpy(str + ml + 1, system_utsname.sysname, sl);
	       str[l] = '\0';
	};

	return l;
}

static unsigned int
sym_uid(char *str, void *data)
{
	unsigned int l;
	char buf[10];

	l = sprintf(buf, "%lu", (unsigned long)current->fsuid);

	if (str) {
	      memcpy(str, buf, l);
	      str[l] = '\0';
	}

	return l;
}

static unsigned int
sym_gid(char *str, void *data)
{
	unsigned int l;
	char buf[10];

	l = sprintf(buf, "%lu", (unsigned long)current->fsgid);

	if (str) {
	      memcpy(str, buf, l);
	      str[l] = '\0';
	}

	return l;
}

static struct ocfs2_symlink_ops symlink_ops[] = {
	{"hostname}", 9, sym_hostname},
	{"mach}", 5, sym_machine},
	{"os}", 3, sym_os},
	{"nodenum}", 8, sym_nodenum},
	{"sys}", 4, sym_system},
	{"uid}", 4, sym_uid},
	{"gid}", 4, sym_gid},
	{NULL, 0, NULL}
};


/**
 *** ocfs2_link_expand - Expand a context sensitive symlink
 *** @ops: The symlink substitution operations table
 *** @out: Buffer to place result in
 *** @in: Buffer to get symlink from
 ***
 *** Returns: 0 or error code
 ***/

static void ocfs2_link_expand(struct ocfs2_symlink_ops *ops, char *out, char *in, struct inode *inode)
{
	unsigned int i;

	while (*in) {
		*out++ = *in;
		if (*in++ != '{')
			continue;

		for (i = 0; ops[i].name; i++) {
			if (memcmp(in, ops[i].name, ops[i].len) == 0) {
				out--;
				out += ops[i].subst_fn(out, inode);
				in += ops[i].len;
			}
		}
	}

	*out = 0;
}


/**
 *** ocfs2_link_size - Return expanded size required to store a symlink
 *** @str: The symlink
 *** @ops: The symlink substitution operations table
 ***
 *** Returns: The size of the expanded symlink.
 ***/


static unsigned int ocfs2_link_size(struct ocfs2_symlink_ops *ops, char *str, struct inode *inode)
{
	unsigned int len = 0;
	unsigned int i;

	while (*str) {
		len++;
		if (*str++ != '{')
			continue;

		for (i = 0; ops[i].name; i++) {
			if (memcmp(str, ops[i].name, ops[i].len) == 0) {
				len--;
				len += ops[i].subst_fn(NULL, inode);
				str += ops[i].len;
				break;
			}
		}
	}

	return len + 1;
}

static inline int ocfs2_cdsl_follow_link(struct nameidata *nd,
					 char *old_link,
					 struct inode *inode)
{
	int status;
	char *new_link;
	unsigned int len;

	len = ocfs2_link_size(symlink_ops, old_link, inode);
	new_link = kmalloc(len, GFP_KERNEL);
	if (new_link == NULL) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	ocfs2_link_expand(symlink_ops, new_link, old_link, inode);

	status = vfs_follow_link(nd, new_link);
	if (status < 0)
		mlog_errno(status);

	kfree(new_link);
bail:
	return status;
}
#endif

#define NEW_FOLLOW_LINK_API

#ifndef NEW_FOLLOW_LINK_API
static int ocfs2_follow_link(struct dentry *dentry,
			     struct nameidata *nd)
#else
static void *ocfs2_follow_link(struct dentry *dentry,
			       struct nameidata *nd)
#endif
{
	int status;
	char *link;
	struct inode *inode = dentry->d_inode;
	struct page *page = NULL;
	struct buffer_head *bh = NULL;
	
	if (ocfs2_inode_is_fast_symlink(inode))
		link = ocfs2_fast_symlink_getlink(inode, &bh);
	else
		link = ocfs2_page_getlink(dentry, &page);
	if (IS_ERR(link)) {
		status = PTR_ERR(link);
		mlog_errno(status);
		goto bail;
	}

#ifdef OCFS2_CDSL
	status = ocfs2_cdsl_follow_link(nd, link, inode);
#else
	status = vfs_follow_link(nd, link);
#endif
	if (status)
		mlog_errno(status);
bail:
	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
	if (bh)
		brelse(bh);

#ifndef NEW_FOLLOW_LINK_API
	return status;
#else
	return ERR_PTR(status);
#endif
}

struct inode_operations ocfs2_symlink_inode_operations = {
	.readlink	= page_readlink,
	.follow_link	= ocfs2_follow_link,
	.getattr	= ocfs2_getattr,
};
struct inode_operations ocfs2_fast_symlink_inode_operations = {
	.readlink	= ocfs2_readlink,
	.follow_link	= ocfs2_follow_link,
	.getattr	= ocfs2_getattr,
};

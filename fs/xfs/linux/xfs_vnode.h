/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_VNODE_H__
#define __XFS_VNODE_H__

/*
 * Vnode types (unrelated to on-disk inodes).  VNON means no type.
 */
typedef enum vtype {
	VNON	= 0,
	VREG	= 1,
	VDIR	= 2,
	VBLK	= 3,
	VCHR	= 4,
	VLNK	= 5,
	VFIFO	= 6,
	VBAD	= 7,
	VSOCK	= 8
} vtype_t;

typedef __u64	vnumber_t;

/*
 * Define the type of behavior head used by vnodes.
 */
#define vn_bhv_head_t	bhv_head_t

/*
 * MP locking protocols:
 *	v_flag, v_count				VN_LOCK/VN_UNLOCK
 *	v_vfsp					VN_LOCK/VN_UNLOCK
 *	v_type					read-only or fs-dependent
 *	v_list, v_hashp, v_hashn		freelist lock
 */
typedef struct vnode {
	__u32		v_flag;			/* vnode flags (see below) */
	enum vtype	v_type;			/* vnode type		*/
	struct vfs	*v_vfsp;		/* ptr to containing VFS*/
	vnumber_t	v_number;		/* in-core vnode number */
	vn_bhv_head_t	v_bh;			/* behavior head */

	spinlock_t	v_lock;			/* don't use VLOCK on Linux */
	struct inode	v_inode;		/* linux inode */
#ifdef	CONFIG_XFS_VNODE_TRACING
	struct ktrace	*v_trace;		/* trace header structure    */
#endif	/* CONFIG_XFS_VNODE_TRACING */
} vnode_t;

/*
 * Vnode to Linux inode mapping.
 */
#define LINVFS_GET_VP(inode)	((vnode_t *)list_entry(inode, vnode_t, v_inode))
#define LINVFS_GET_IP(vp)	(&(vp)->v_inode)

/*
 * Conversion between vnode types/modes and encoded type/mode as
 * seen by stat(2) and mknod(2).
 */
extern enum vtype	iftovt_tab[];
extern ushort		vttoif_tab[];
#define IFTOVT(M)	(iftovt_tab[((M) & S_IFMT) >> 12])
#define VTTOIF(T)	(vttoif_tab[(int)(T)])
#define MAKEIMODE(T, M) (VTTOIF(T) | ((M) & ~S_IFMT))

/*
 * Vnode flags.
 *
 * The vnode flags fall into two categories:
 * 1) Local only -
 *    Flags that are relevant only to a particular cell
 * 2) Single system image -
 *    Flags that must be maintained coherent across all cells
 */
 /* Local only flags */
#define VINACT		       0x1	/* vnode is being inactivated	*/
#define VRECLM		       0x2	/* vnode is being reclaimed	*/
#define VWAIT		       0x4	/* waiting for VINACT
					   or VRECLM to finish */
#define VMODIFIED	       0x8	/* xfs inode state possibly different
					 * from linux inode state.
					 */

/* Single system image flags */
#define VROOT		  0x100000	/* root of its file system	*/
#define VNOSWAP		  0x200000	/* cannot be used as virt swap device */
#define VISSWAP		  0x400000	/* vnode is part of virt swap device */
#define VREPLICABLE	  0x800000	/* Vnode can have replicated pages */
#define VNONREPLICABLE	 0x1000000	/* Vnode has writers. Don't replicate */
#define VDOCMP		 0x2000000	/* Vnode has special VOP_CMP impl. */
#define VSHARE		 0x4000000	/* vnode part of global cache	*/
					/* VSHARE applies to local cell only */
#define VFRLOCKS	 0x8000000	/* vnode has FR locks applied	*/
#define VENF_LOCKING	0x10000000	/* enf. mode FR locking in effect */
#define VOPLOCK		0x20000000	/* oplock set on the vnode	*/
#define VPURGE		0x40000000	/* In the linux 'put' thread	*/

typedef enum vrwlock	{ VRWLOCK_NONE, VRWLOCK_READ,
			  VRWLOCK_WRITE, VRWLOCK_WRITE_DIRECT,
			  VRWLOCK_TRY_READ, VRWLOCK_TRY_WRITE } vrwlock_t;

/*
 * Return values for VOP_INACTIVE.  A return value of
 * VN_INACTIVE_NOCACHE implies that the file system behavior
 * has disassociated its state and bhv_desc_t from the vnode.
 */
#define VN_INACTIVE_CACHE	0
#define VN_INACTIVE_NOCACHE	1

/*
 * Values for the cmd code given to VOP_VNODE_CHANGE.
 */
typedef enum vchange {
	VCHANGE_FLAGS_FRLOCKS		= 0,
	VCHANGE_FLAGS_ENF_LOCKING	= 1,
	VCHANGE_FLAGS_TRUNCATED		= 2,
	VCHANGE_FLAGS_PAGE_DIRTY	= 3,
	VCHANGE_FLAGS_IOEXCL_COUNT	= 4
} vchange_t;

/*
 * Macros for dealing with the behavior descriptor inside of the vnode.
 */
#define BHV_TO_VNODE(bdp)	((vnode_t *)BHV_VOBJ(bdp))
#define BHV_TO_VNODE_NULL(bdp)	((vnode_t *)BHV_VOBJNULL(bdp))

#define VNODE_TO_FIRST_BHV(vp)		(BHV_HEAD_FIRST(&(vp)->v_bh))
#define VN_BHV_HEAD(vp)			((vn_bhv_head_t *)(&((vp)->v_bh)))
#define VN_BHV_READ_LOCK(bhp)		BHV_READ_LOCK(bhp)
#define VN_BHV_READ_UNLOCK(bhp)		BHV_READ_UNLOCK(bhp)
#define VN_BHV_WRITE_LOCK(bhp)		BHV_WRITE_LOCK(bhp)
#define VN_BHV_NOT_READ_LOCKED(bhp)	BHV_NOT_READ_LOCKED(bhp)
#define VN_BHV_NOT_WRITE_LOCKED(bhp)	BHV_NOT_WRITE_LOCKED(bhp)
#define vn_bhv_head_init(bhp,name)	bhv_head_init(bhp,name)
#define vn_bhv_head_reinit(bhp)		bhv_head_reinit(bhp)
#define vn_bhv_insert_initial(bhp,bdp)	bhv_insert_initial(bhp,bdp)
#define vn_bhv_remove(bhp,bdp)		bhv_remove(bhp,bdp)
#define vn_bhv_lookup(bhp,ops)		bhv_lookup(bhp,ops)
#define vn_bhv_lookup_unlocked(bhp,ops) bhv_lookup_unlocked(bhp,ops)

#define v_fbhv		v_bh.bh_first	       /* first behavior */
#define v_fops		v_bh.bh_first->bd_ops  /* ops for first behavior */


union rval;
struct uio;
struct file;
struct vattr;
struct page_buf_bmap_s;
struct attrlist_cursor_kern;

typedef int	(*vop_open_t)(bhv_desc_t *, struct cred *);
typedef ssize_t (*vop_read_t)(bhv_desc_t *, struct file *, char *, size_t,
				loff_t *, struct cred *);
typedef ssize_t (*vop_write_t)(bhv_desc_t *, struct file *, const char *, size_t,
				loff_t *, struct cred *);
typedef int	(*vop_ioctl_t)(bhv_desc_t *, struct inode *, struct file *, unsigned int, unsigned long);
typedef int	(*vop_getattr_t)(bhv_desc_t *, struct vattr *, int,
				struct cred *);
typedef int	(*vop_setattr_t)(bhv_desc_t *, struct vattr *, int,
				struct cred *);
typedef int	(*vop_access_t)(bhv_desc_t *, int, struct cred *);
typedef int	(*vop_lookup_t)(bhv_desc_t *, struct dentry *, vnode_t **,
				int, vnode_t *, struct cred *);
typedef int	(*vop_create_t)(bhv_desc_t *, struct dentry *, struct vattr *,
				vnode_t **, struct cred *);
typedef int	(*vop_remove_t)(bhv_desc_t *, struct dentry *, struct cred *);
typedef int	(*vop_link_t)(bhv_desc_t *, vnode_t *, struct dentry *,
				struct cred *);
typedef int	(*vop_rename_t)(bhv_desc_t *, struct dentry *, vnode_t *,
				struct dentry *, struct cred *);
typedef int	(*vop_mkdir_t)(bhv_desc_t *, struct dentry *, struct vattr *,
				vnode_t **, struct cred *);
typedef	int	(*vop_rmdir_t)(bhv_desc_t *, struct dentry *, struct cred *);
typedef int	(*vop_readdir_t)(bhv_desc_t *, struct uio *, struct cred *,
				int *);
typedef int	(*vop_symlink_t)(bhv_desc_t *, struct dentry *,
				struct vattr *, char *,
				vnode_t **, struct cred *);
typedef int	(*vop_readlink_t)(bhv_desc_t *, struct uio *, struct cred *);
typedef int	(*vop_fsync_t)(bhv_desc_t *, int, struct cred *, xfs_off_t, xfs_off_t);
typedef int	(*vop_inactive_t)(bhv_desc_t *, struct cred *);
typedef int	(*vop_fid2_t)(bhv_desc_t *, struct fid *);
typedef int	(*vop_release_t)(bhv_desc_t *);
typedef int	(*vop_rwlock_t)(bhv_desc_t *, vrwlock_t);
typedef void	(*vop_rwunlock_t)(bhv_desc_t *, vrwlock_t);
typedef int	(*vop_bmap_t)(bhv_desc_t *, xfs_off_t, ssize_t, int, struct cred *, struct page_buf_bmap_s *, int *);
typedef int	(*vop_strategy_t)(bhv_desc_t *, xfs_off_t, ssize_t, int, struct cred *, struct page_buf_bmap_s *, int *);
typedef int	(*vop_reclaim_t)(bhv_desc_t *);
typedef int	(*vop_attr_get_t)(bhv_desc_t *, char *, char *, int *, int,
				struct cred *);
typedef int	(*vop_attr_set_t)(bhv_desc_t *, char *, char *, int, int,
				struct cred *);
typedef int	(*vop_attr_remove_t)(bhv_desc_t *, char *, int, struct cred *);
typedef int	(*vop_attr_list_t)(bhv_desc_t *, char *, int, int,
				struct attrlist_cursor_kern *, struct cred *);
typedef void	(*vop_link_removed_t)(bhv_desc_t *, vnode_t *, int);
typedef void	(*vop_vnode_change_t)(bhv_desc_t *, vchange_t, __psint_t);
typedef void	(*vop_ptossvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
typedef void	(*vop_pflushinvalvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
typedef int	(*vop_pflushvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t, uint64_t, int);
typedef int	(*vop_iflush_t)(bhv_desc_t *, int);


typedef struct vnodeops {
	bhv_position_t	vn_position;	/* position within behavior chain */
	vop_open_t		vop_open;
	vop_read_t		vop_read;
	vop_write_t		vop_write;
	vop_ioctl_t		vop_ioctl;
	vop_getattr_t		vop_getattr;
	vop_setattr_t		vop_setattr;
	vop_access_t		vop_access;
	vop_lookup_t		vop_lookup;
	vop_create_t		vop_create;
	vop_remove_t		vop_remove;
	vop_link_t		vop_link;
	vop_rename_t		vop_rename;
	vop_mkdir_t		vop_mkdir;
	vop_rmdir_t		vop_rmdir;
	vop_readdir_t		vop_readdir;
	vop_symlink_t		vop_symlink;
	vop_readlink_t		vop_readlink;
	vop_fsync_t		vop_fsync;
	vop_inactive_t		vop_inactive;
	vop_fid2_t		vop_fid2;
	vop_rwlock_t		vop_rwlock;
	vop_rwunlock_t		vop_rwunlock;
	vop_bmap_t		vop_bmap;
	vop_strategy_t		vop_strategy;
	vop_reclaim_t		vop_reclaim;
	vop_attr_get_t		vop_attr_get;
	vop_attr_set_t		vop_attr_set;
	vop_attr_remove_t	vop_attr_remove;
	vop_attr_list_t		vop_attr_list;
	vop_link_removed_t	vop_link_removed;
	vop_vnode_change_t	vop_vnode_change;
	vop_ptossvp_t		vop_tosspages;
	vop_pflushinvalvp_t	vop_flushinval_pages;
	vop_pflushvp_t		vop_flush_pages;
	vop_release_t		vop_release;
	vop_iflush_t		vop_iflush;
} vnodeops_t;

/*
 * VOP's.
 */
#define _VOP_(op, vp)	(*((vnodeops_t *)(vp)->v_fops)->op)

/*
 * Be careful with VOP_OPEN, since we're holding the chain lock on the
 * original vnode and VOP_OPEN semantic allows the new vnode to be returned
 * in vpp. The practice of passing &vp for vpp just doesn't work.
 */
#define VOP_READ(vp,file,buf,size,offset,cr,rv)				\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_read, vp)((vp)->v_fbhv,file,buf,size,offset,cr); \
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_WRITE(vp,file,buf,size,offset,cr,rv)			\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_write, vp)((vp)->v_fbhv,file,buf,size,offset,cr);\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_BMAP(vp,of,sz,rw,cr,b,n,rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_bmap, vp)((vp)->v_fbhv,of,sz,rw,cr,b,n);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_STRATEGY(vp,of,sz,rw,cr,b,n,rv)				\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_strategy, vp)((vp)->v_fbhv,of,sz,rw,cr,b,n);	\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_OPEN(vp, cr, rv)						\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_open, vp)((vp)->v_fbhv, cr);			\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_GETATTR(vp, vap, f, cr, rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_getattr, vp)((vp)->v_fbhv, vap, f, cr);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_SETATTR(vp, vap, f, cr, rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_setattr, vp)((vp)->v_fbhv, vap, f, cr);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_ACCESS(vp, mode, cr, rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_access, vp)((vp)->v_fbhv, mode, cr);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_LOOKUP(vp,d,vpp,f,rdir,cr,rv)				\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_lookup, vp)((vp)->v_fbhv,d,vpp,f,rdir,cr);	\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_CREATE(dvp,d,vap,vpp,cr,rv)					\
{									\
	VN_BHV_READ_LOCK(&(dvp)->v_bh);					\
	rv = _VOP_(vop_create, dvp)((dvp)->v_fbhv,d,vap,vpp,cr);	\
	VN_BHV_READ_UNLOCK(&(dvp)->v_bh);				\
}
#define VOP_REMOVE(dvp,d,cr,rv)						\
{									\
	VN_BHV_READ_LOCK(&(dvp)->v_bh);					\
	rv = _VOP_(vop_remove, dvp)((dvp)->v_fbhv,d,cr);		\
	VN_BHV_READ_UNLOCK(&(dvp)->v_bh);				\
}
#define VOP_LINK(tdvp,fvp,d,cr,rv)					\
{									\
	VN_BHV_READ_LOCK(&(tdvp)->v_bh);				\
	rv = _VOP_(vop_link, tdvp)((tdvp)->v_fbhv,fvp,d,cr);		\
	VN_BHV_READ_UNLOCK(&(tdvp)->v_bh);				\
}
#define VOP_RENAME(fvp,fnm,tdvp,tnm,cr,rv)				\
{									\
	VN_BHV_READ_LOCK(&(fvp)->v_bh);					\
	rv = _VOP_(vop_rename, fvp)((fvp)->v_fbhv,fnm,tdvp,tnm,cr);	\
	VN_BHV_READ_UNLOCK(&(fvp)->v_bh);				\
}
#define VOP_MKDIR(dp,d,vap,vpp,cr,rv)					\
{									\
	VN_BHV_READ_LOCK(&(dp)->v_bh);					\
	rv = _VOP_(vop_mkdir, dp)((dp)->v_fbhv,d,vap,vpp,cr);		\
	VN_BHV_READ_UNLOCK(&(dp)->v_bh);				\
}
#define	VOP_RMDIR(dp,d,cr,rv)	 					\
{									\
	VN_BHV_READ_LOCK(&(dp)->v_bh);					\
	rv = _VOP_(vop_rmdir, dp)((dp)->v_fbhv,d,cr);			\
	VN_BHV_READ_UNLOCK(&(dp)->v_bh);				\
}
#define VOP_READDIR(vp,uiop,cr,eofp,rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_readdir, vp)((vp)->v_fbhv,uiop,cr,eofp);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_SYMLINK(dvp,d,vap,tnm,vpp,cr,rv)				\
{									\
	VN_BHV_READ_LOCK(&(dvp)->v_bh);					\
	rv = _VOP_(vop_symlink, dvp) ((dvp)->v_fbhv,d,vap,tnm,vpp,cr); \
	VN_BHV_READ_UNLOCK(&(dvp)->v_bh);				\
}
#define VOP_READLINK(vp,uiop,cr,rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_readlink, vp)((vp)->v_fbhv,uiop,cr);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_FSYNC(vp,f,cr,b,e,rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_fsync, vp)((vp)->v_fbhv,f,cr,b,e);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_INACTIVE(vp, cr, rv)					\
{	/* vnode not reference-able, so no need to lock chain */	\
	rv = _VOP_(vop_inactive, vp)((vp)->v_fbhv, cr);			\
}
#define VOP_RELEASE(vp, rv)						\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_release, vp)((vp)->v_fbhv);			\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_FID2(vp, fidp, rv)						\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_fid2, vp)((vp)->v_fbhv, fidp);			\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_RWLOCK(vp,i)						\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	(void)_VOP_(vop_rwlock, vp)((vp)->v_fbhv, i);			\
	/* "allow" is done by rwunlock */				\
}
#define VOP_RWLOCK_TRY(vp,i)						\
	_VOP_(vop_rwlock, vp)((vp)->v_fbhv, i)

#define VOP_RWUNLOCK(vp,i)						\
{	/* "prevent" was done by rwlock */				\
	(void)_VOP_(vop_rwunlock, vp)((vp)->v_fbhv, i);			\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_RECLAIM(vp, rv)						\
{	/* vnode not reference-able, so no need to lock chain */	\
	rv = _VOP_(vop_reclaim, vp)((vp)->v_fbhv);			\
}
#define VOP_ATTR_GET(vp, name, val, vallenp, fl, cred, rv)		\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_attr_get, vp)((vp)->v_fbhv,name,val,vallenp,fl,cred); \
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_ATTR_SET(vp, name, val, vallen, fl, cred, rv)		\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_attr_set, vp)((vp)->v_fbhv,name,val,vallen,fl,cred); \
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_ATTR_REMOVE(vp, name, flags, cred, rv)			\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_attr_remove, vp)((vp)->v_fbhv,name,flags,cred);	\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_ATTR_LIST(vp, buf, buflen, fl, cursor, cred, rv)		\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_attr_list, vp)((vp)->v_fbhv,buf,buflen,fl,cursor,cred);\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_LINK_REMOVED(vp, dvp, linkzero)				\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	(void)_VOP_(vop_link_removed, vp)((vp)->v_fbhv, dvp, linkzero); \
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_VNODE_CHANGE(vp, cmd, val)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	(void)_VOP_(vop_vnode_change, vp)((vp)->v_fbhv,cmd,val);	\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
/*
 * These are page cache functions that now go thru VOPs.
 * 'last' parameter is unused and left in for IRIX compatibility
 */
#define VOP_TOSS_PAGES(vp, first, last, fiopt)				\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	_VOP_(vop_tosspages, vp)((vp)->v_fbhv,first, last, fiopt);	\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
/*
 * 'last' parameter is unused and left in for IRIX compatibility
 */
#define VOP_FLUSHINVAL_PAGES(vp, first, last, fiopt)			\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	_VOP_(vop_flushinval_pages, vp)((vp)->v_fbhv,first,last,fiopt); \
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
/*
 * 'last' parameter is unused and left in for IRIX compatibility
 */
#define VOP_FLUSH_PAGES(vp, first, last, flags, fiopt, rv)		\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_flush_pages, vp)((vp)->v_fbhv,first,last,flags,fiopt);\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_IOCTL(vp, inode, filp, cmd, arg, rv)			\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_ioctl, vp)((vp)->v_fbhv,inode,filp,cmd,arg);	\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}
#define VOP_IFLUSH(vp, flags, rv)					\
{									\
	VN_BHV_READ_LOCK(&(vp)->v_bh);					\
	rv = _VOP_(vop_iflush, vp)((vp)->v_fbhv, flags);		\
	VN_BHV_READ_UNLOCK(&(vp)->v_bh);				\
}

/*
 * Flags for VOP_IFLUSH call
 */

#define FLUSH_SYNC		1	/* wait for flush to complete	*/
#define FLUSH_INODE		2	/* flush the inode itself	*/
#define FLUSH_LOG		4	/* force the last log entry for
					 * this inode out to disk	*/

/*
 * Flush/Invalidate options for VOP_TOSS_PAGES, VOP_FLUSHINVAL_PAGES and
 *	VOP_FLUSH_PAGES.
 */
#define FI_NONE			0	/* none */
#define FI_REMAPF		1	/* Do a remapf prior to the operation */
#define FI_REMAPF_LOCKED	2	/* Do a remapf prior to the operation.
					   Prevent VM access to the pages until
					   the operation completes. */

/*
 * Vnode attributes.  va_mask indicates those attributes the caller
 * wants to set (setattr) or extract (getattr).
 */
typedef struct vattr {
	int		va_mask;	/* bit-mask of attributes */
	vtype_t		va_type;	/* vnode type (for create) */
	mode_t		va_mode;	/* file access mode */
	uid_t		va_uid;		/* owner user id */
	gid_t		va_gid;		/* owner group id */
	dev_t		va_fsid;	/* file system id (dev for now) */
	xfs_ino_t	va_nodeid;	/* node id */
	nlink_t		va_nlink;	/* number of references to file */
	xfs_off_t	va_size;	/* file size in bytes */
	timespec_t	va_atime;	/* time of last access */
	timespec_t	va_mtime;	/* time of last modification */
	timespec_t	va_ctime;	/* time file ``created'' */
	dev_t		va_rdev;	/* device the file represents */
	u_long		va_blksize;	/* fundamental block size */
	__int64_t	va_nblocks;	/* # of blocks allocated */
	u_long		va_vcode;	/* version code */
	u_long		va_xflags;	/* random extended file flags */
	u_long		va_extsize;	/* file extent size */
	u_long		va_nextents;	/* number of extents in file */
	u_long		va_anextents;	/* number of attr extents in file */
	int		va_projid;	/* project id */
	u_int		va_gencount;	/* object generation count */
} vattr_t;

/*
 * setattr or getattr attributes
 */
#define AT_TYPE		0x00000001
#define AT_MODE		0x00000002
#define AT_UID		0x00000004
#define AT_GID		0x00000008
#define AT_FSID		0x00000010
#define AT_NODEID	0x00000020
#define AT_NLINK	0x00000040
#define AT_SIZE		0x00000080
#define AT_ATIME	0x00000100
#define AT_MTIME	0x00000200
#define AT_CTIME	0x00000400
#define AT_RDEV		0x00000800
#define AT_BLKSIZE	0x00001000
#define AT_NBLOCKS	0x00002000
#define AT_VCODE	0x00004000
#define AT_MAC		0x00008000
#define AT_UPDATIME	0x00010000
#define AT_UPDMTIME	0x00020000
#define AT_UPDCTIME	0x00040000
#define AT_ACL		0x00080000
#define AT_CAP		0x00100000
#define AT_INF		0x00200000
#define AT_XFLAGS	0x00400000
#define AT_EXTSIZE	0x00800000
#define AT_NEXTENTS	0x01000000
#define AT_ANEXTENTS	0x02000000
#define AT_PROJID	0x04000000
#define AT_SIZE_NOPERM	0x08000000
#define AT_GENCOUNT	0x10000000

#define AT_ALL	(AT_TYPE|AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|\
		AT_NLINK|AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|AT_RDEV|\
		AT_BLKSIZE|AT_NBLOCKS|AT_VCODE|AT_MAC|AT_ACL|AT_CAP|\
		AT_INF|AT_XFLAGS|AT_EXTSIZE|AT_NEXTENTS|AT_ANEXTENTS|\
		AT_PROJID|AT_GENCOUNT)

#define AT_STAT (AT_TYPE|AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|AT_NLINK|\
		AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|AT_RDEV|AT_BLKSIZE|\
		AT_NBLOCKS|AT_PROJID)

#define AT_TIMES (AT_ATIME|AT_MTIME|AT_CTIME)

#define AT_UPDTIMES (AT_UPDATIME|AT_UPDMTIME|AT_UPDCTIME)

#define AT_NOSET (AT_NLINK|AT_RDEV|AT_FSID|AT_NODEID|AT_TYPE|\
		 AT_BLKSIZE|AT_NBLOCKS|AT_VCODE|AT_NEXTENTS|AT_ANEXTENTS|\
		 AT_GENCOUNT)

#define VREAD		00400
#define VWRITE		00200
#define VEXEC		00100
#define VSGID		02000		/* set group id on execution */
#define MODEMASK	07777		/* mode bits plus permission bits */

/*
 * Check whether mandatory file locking is enabled.
 */
#define MANDLOCK(vp, mode)	\
	((vp)->v_type == VREG && ((mode) & (VSGID|(VEXEC>>3))) == VSGID)

extern void	vn_init(void);
extern int	vn_wait(struct vnode *);
extern vnode_t	*vn_initialize(struct inode *);

/*
 * Acquiring and invalidating vnodes:
 *
 *	if (vn_get(vp, version, 0))
 *		...;
 *	vn_purge(vp, version);
 *
 * vn_get and vn_purge must be called with vmap_t arguments, sampled
 * while a lock that the vnode's VOP_RECLAIM function acquires is
 * held, to ensure that the vnode sampled with the lock held isn't
 * recycled (VOP_RECLAIMed) or deallocated between the release of the lock
 * and the subsequent vn_get or vn_purge.
 */

/*
 * vnode_map structures _must_ match vn_epoch and vnode structure sizes.
 */
typedef struct vnode_map {
	vfs_t		*v_vfsp;
	vnumber_t	v_number;		/* in-core vnode number */
	xfs_ino_t	v_ino;			/* inode #	*/
} vmap_t;

#define VMAP(vp, ip, vmap)	{(vmap).v_vfsp	 = (vp)->v_vfsp,	\
				 (vmap).v_number = (vp)->v_number,	\
				 (vmap).v_ino	 = (ip)->i_ino; }
extern void	vn_purge(struct vnode *, vmap_t *);
extern vnode_t	*vn_get(struct vnode *, vmap_t *);
extern int	vn_revalidate(struct vnode *, int);
extern void	vn_remove(struct vnode *);

static inline int vn_count(struct vnode *vp)
{
	return atomic_read(&LINVFS_GET_IP(vp)->i_count);
}

/*
 * Vnode reference counting functions (and macros for compatibility).
 */
extern vnode_t	*vn_hold(struct vnode *);
extern void	vn_rele(struct vnode *);

#if defined(CONFIG_XFS_VNODE_TRACING)

#define VN_HOLD(vp)		\
	((void)vn_hold(vp), \
	  vn_trace_hold(vp, __FILE__, __LINE__, (inst_t *)__return_address))
#define VN_RELE(vp)		\
	  (vn_trace_rele(vp, __FILE__, __LINE__, (inst_t *)__return_address), \
	   iput(LINVFS_GET_IP(vp)))

#else	/* ! (defined(CONFIG_XFS_VNODE_TRACING)) */

#define VN_HOLD(vp)		((void)vn_hold(vp))
#define VN_RELE(vp)		(iput(LINVFS_GET_IP(vp)))

#endif	/* ! (defined(CONFIG_XFS_VNODE_TRACING) */

/*
 * Vnode spinlock manipulation.
 */
#define VN_FLAGSET(vp,b)	vn_flagset(vp,b)
#define VN_FLAGCLR(vp,b)	vn_flagclr(vp,b)

static __inline__ void vn_flagset(struct vnode *vp, uint flag)
{
	spin_lock(&vp->v_lock);
	vp->v_flag |= flag;
	spin_unlock(&vp->v_lock);
}

static __inline__ void vn_flagclr(struct vnode *vp, uint flag)
{
	spin_lock(&vp->v_lock);
	vp->v_flag &= ~flag;
	spin_unlock(&vp->v_lock);
}

/*
 * Some useful predicates.
 */
#define VN_MAPPED(vp)	\
	(!list_empty(&(LINVFS_GET_IP(vp)->i_mapping->i_mmap)) || \
	(!list_empty(&(LINVFS_GET_IP(vp)->i_mapping->i_mmap_shared))))
#define VN_CACHED(vp)	(LINVFS_GET_IP(vp)->i_mapping->nrpages)
#define VN_DIRTY(vp)	(!list_empty(&(LINVFS_GET_IP(vp)->i_mapping->dirty_pages)))
#define VMODIFY(vp)	{ VN_FLAGSET(vp, VMODIFIED); \
			mark_inode_dirty(LINVFS_GET_IP(vp)); }
#define VUNMODIFY(vp)	VN_FLAGCLR(vp, VMODIFIED)

/*
 * Flags to VOP_SETATTR/VOP_GETATTR.
 */
#define ATTR_UTIME	0x01	/* non-default utime(2) request */
#define ATTR_EXEC	0x02	/* invocation from exec(2) */
#define ATTR_COMM	0x04	/* yield common vp attributes */
#define ATTR_DMI	0x08	/* invocation from a DMI function */
#define ATTR_LAZY	0x80	/* set/get attributes lazily */
#define ATTR_NONBLOCK	0x100	/* return EAGAIN if operation would block */
#define ATTR_NOLOCK	0x200	/* Don't grab any conflicting locks */
#define ATTR_NOSIZETOK	0x400	/* Don't get the DVN_SIZE_READ token */

/*
 * Flags to VOP_FSYNC and VOP_RECLAIM.
 */
#define FSYNC_NOWAIT	0	/* asynchronous flush */
#define FSYNC_WAIT	0x1	/* synchronous fsync or forced reclaim */
#define FSYNC_INVAL	0x2	/* flush and invalidate cached data */
#define FSYNC_DATA	0x4	/* synchronous fsync of data only */

#if (defined(CONFIG_XFS_VNODE_TRACING))

#define VNODE_TRACE_SIZE	16		/* number of trace entries */

/*
 * Tracing entries.
 */
#define VNODE_KTRACE_ENTRY	1
#define VNODE_KTRACE_EXIT	2
#define VNODE_KTRACE_HOLD	3
#define VNODE_KTRACE_REF	4
#define VNODE_KTRACE_RELE	5

extern void vn_trace_entry(struct vnode *, char *, inst_t *);
extern void vn_trace_exit(struct vnode *, char *, inst_t *);
extern void vn_trace_hold(struct vnode *, char *, int, inst_t *);
extern void vn_trace_ref(struct vnode *, char *, int, inst_t *);
extern void vn_trace_rele(struct vnode *, char *, int, inst_t *);
#define VN_TRACE(vp)		\
	vn_trace_ref(vp, __FILE__, __LINE__, (inst_t *)__return_address)

#else	/* ! (defined(CONFIG_XFS_VNODE_TRACING)) */

#define vn_trace_entry(a,b,c)
#define vn_trace_exit(a,b,c)
#define vn_trace_hold(a,b,c,d)
#define vn_trace_ref(a,b,c,d)
#define vn_trace_rele(a,b,c,d)
#define VN_TRACE(vp)

#endif	/* ! (defined(CONFIG_XFS_VNODE_TRACING)) */

#endif	/* __XFS_VNODE_H__ */

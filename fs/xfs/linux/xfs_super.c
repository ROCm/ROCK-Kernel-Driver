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

#include <xfs.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include "xfs_version.h"

/* xfs_vfs[ops].c */
extern int  xfs_init(void);
extern void xfs_cleanup(void);

/* For kernels which have the s_maxbytes field - set it */
#ifdef MAX_NON_LFS
# define set_max_bytes(sb)	((sb)->s_maxbytes = XFS_MAX_FILE_OFFSET)
#else
# define set_max_bytes(sb)	do { } while (0)
#endif

#ifdef CONFIG_XFS_POSIX_ACL
# define set_posix_acl(sb)	((sb)->s_flags |= MS_POSIXACL)
#else
# define set_posix_acl(sb)	do { } while (0)
#endif

#ifdef CONFIG_XFS_QUOTA
STATIC struct quotactl_ops linvfs_qops = {
	.get_xstate		= linvfs_getxstate,
	.set_xstate		= linvfs_setxstate,
	.get_xquota		= linvfs_getxquota,
	.set_xquota		= linvfs_setxquota,
};
# define set_quota_ops(sb)	((sb)->s_qcop = &linvfs_qops)
#else
# define set_quota_ops(sb)	do { } while (0)
#endif

#ifdef CONFIG_XFS_DMAPI
int dmapi_init(void);
void dmapi_uninit(void);
#else
#define dmapi_init()
#define dmapi_uninit()
#endif

STATIC struct super_operations linvfs_sops;
STATIC struct export_operations linvfs_export_ops;

#define MNTOPT_LOGBUFS	"logbufs"	/* number of XFS log buffers */
#define MNTOPT_LOGBSIZE "logbsize"	/* size of XFS log buffers */
#define MNTOPT_LOGDEV	"logdev"	/* log device */
#define MNTOPT_RTDEV	"rtdev"		/* realtime I/O device */
#define MNTOPT_DMAPI	"dmapi"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_XDSM	"xdsm"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_BIOSIZE	"biosize"	/* log2 of preferred buffered io size */
#define MNTOPT_WSYNC	"wsync"		/* safe-mode nfs compatible mount */
#define MNTOPT_INO64	"ino64"		/* force inodes into 64-bit range */
#define MNTOPT_NOALIGN	"noalign"	/* turn off stripe alignment */
#define MNTOPT_SUNIT	"sunit"		/* data volume stripe unit */
#define MNTOPT_SWIDTH	"swidth"	/* data volume stripe width */
#define MNTOPT_NORECOVERY "norecovery"	/* don't run XFS recovery */
#define MNTOPT_OSYNCISOSYNC "osyncisosync" /* o_sync is REALLY o_sync */
#define MNTOPT_QUOTA	"quota"		/* disk quotas */
#define MNTOPT_NOQUOTA	"noquota"	/* no quotas */
#define MNTOPT_UQUOTA	"usrquota"	/* user quota enabled */
#define MNTOPT_GQUOTA	"grpquota"	/* group quota enabled */
#define MNTOPT_UQUOTANOENF "uqnoenforce"/* user quota limit enforcement */
#define MNTOPT_GQUOTANOENF "gqnoenforce"/* group quota limit enforcement */
#define MNTOPT_QUOTANOENF  "qnoenforce" /* same as uqnoenforce */
#define MNTOPT_NOUUID	"nouuid"	/* Ignore FS uuid */
#define MNTOPT_NOLOGFLUSH  "nologflush"	/* Don't use hard flushes in
					   log writing */
#define MNTOPT_MTPT	"mtpt"		/* filesystem mount point */

STATIC int
xfs_parseargs(
	char			*options,
	int			flags,
	struct xfs_mount_args	*args)
{
	char			*this_char, *value, *eov;
	int			dsunit, dswidth, vol_dsunit, vol_dswidth;
	int			logbufs, logbufsize;
	int			iosize;

	/* Default to 32 bit inodes on linux all the time */
	args->flags |= XFSMNT_32BITINODES;

	/* Copy the already-parsed mount(2) flags we're interested in */
	if (flags & MS_NOATIME)
		args->flags |= XFSMNT_NOATIME;

	if (!options) {
		args->logbufs = args->logbufsize = -1;
		return 0;
	}

	logbufs = logbufsize = -1;
	iosize = dsunit = dswidth = vol_dsunit = vol_dswidth = 0;

	while ((this_char = strsep(&options, ",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char, MNTOPT_LOGBUFS)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_LOGBUFS);
				return -EINVAL;
			}
			logbufs = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_LOGBSIZE)) {
			int	in_kilobytes = 0;

			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_LOGBSIZE);
				return -EINVAL;
			}
			if (toupper(value[strlen(value)-1]) == 'K') {
				in_kilobytes = 1;
				value[strlen(value)-1] = '\0';
			}
			logbufsize = simple_strtoul(value, &eov, 10);
			if (in_kilobytes)
				logbufsize = logbufsize * 1024;
		} else if (!strcmp(this_char, MNTOPT_LOGDEV)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_LOGDEV);
				return -EINVAL;
			}
			strncpy(args->logname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_MTPT)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_MTPT);
				return -EINVAL;
			}
			strncpy(args->mtpt, value, MAXNAMELEN);
#if CONFIG_XFS_DMAPI
		} else if (!strcmp(this_char, MNTOPT_DMAPI)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_XDSM)) {
			args->flags |= XFSMNT_DMAPI;
#else
		} else if (!strcmp(this_char, MNTOPT_DMAPI) ||
			   !strcmp(this_char, MNTOPT_XDSM)) {
			printk("XFS: this kernel does not support dmapi/xdsm.\n");
			return -EINVAL;
#endif
		} else if (!strcmp(this_char, MNTOPT_RTDEV)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_RTDEV);
				return -EINVAL;
			}
			strncpy(args->rtname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_BIOSIZE)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_BIOSIZE); 
				return -EINVAL;
			}
			iosize = simple_strtoul(value, &eov, 10);
			args->flags |= XFSMNT_IOSIZE;
			args->iosizelog = (uint8_t) iosize;
		} else if (!strcmp(this_char, MNTOPT_WSYNC)) {
			args->flags |= XFSMNT_WSYNC;
		} else if (!strcmp(this_char, MNTOPT_OSYNCISOSYNC)) {
			args->flags |= XFSMNT_OSYNCISOSYNC;
		} else if (!strcmp(this_char, MNTOPT_NORECOVERY)) {
			args->flags |= XFSMNT_NORECOVERY;
		} else if (!strcmp(this_char, MNTOPT_INO64)) {
#ifdef XFS_BIG_FILESYSTEMS
			args->flags |= XFSMNT_INO64;
#else
			printk("XFS: %s option not allowed on this system\n",
				MNTOPT_INO64);
			return -EINVAL;
#endif
		} else if (!strcmp(this_char, MNTOPT_UQUOTA)) {
			args->flags |= XFSMNT_UQUOTA | XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_QUOTA)) {
			args->flags |= XFSMNT_UQUOTA | XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_UQUOTANOENF)) {
			args->flags |= XFSMNT_UQUOTA;
			args->flags &= ~XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_QUOTANOENF)) {
			args->flags |= XFSMNT_UQUOTA;
			args->flags &= ~XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_GQUOTA)) {
			args->flags |= XFSMNT_GQUOTA | XFSMNT_GQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_GQUOTANOENF)) {
			args->flags |= XFSMNT_GQUOTA;
			args->flags &= ~XFSMNT_GQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_NOALIGN)) {
			args->flags |= XFSMNT_NOALIGN;
		} else if (!strcmp(this_char, MNTOPT_SUNIT)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_SUNIT);
				return -EINVAL;
			}
			dsunit = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_SWIDTH)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					MNTOPT_SWIDTH);
				return -EINVAL;
			}
			dswidth = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_NOUUID)) {
			args->flags |= XFSMNT_NOUUID;
		} else if (!strcmp(this_char, MNTOPT_NOLOGFLUSH)) {
			args->flags |= XFSMNT_NOLOGFLUSH;
		} else if (!strcmp(this_char, "osyncisdsync")) {
			/* no-op, this is now the default */
printk("XFS: osyncisdsync is now the default, option is deprecated.\n");
		} else if (!strcmp(this_char, "irixsgid")) {
printk("XFS: irixsgid is now a sysctl(2) variable, option is deprecated.\n");
		} else {
			printk("XFS: unknown mount option [%s].\n", this_char);
			return -EINVAL;
		}
	}

	if (args->flags & XFSMNT_NORECOVERY) {
		if ((flags & MS_RDONLY) == 0) {
			printk("XFS: no-recovery mounts must be read-only.\n");
			return -EINVAL;
		}
	}

	if ((args->flags & XFSMNT_NOALIGN) && (dsunit || dswidth)) {
		printk(
	"XFS: sunit and swidth options incompatible with the noalign option\n");
		return -EINVAL;
	}

	if ((dsunit && !dswidth) || (!dsunit && dswidth)) {
		printk("XFS: sunit and swidth must be specified together\n");
		return -EINVAL;
	}

	if (dsunit && (dswidth % dsunit != 0)) {
		printk(
	"XFS: stripe width (%d) must be a multiple of the stripe unit (%d)\n",
			dswidth, dsunit);
		return -EINVAL;
	}

	if ((args->flags & XFSMNT_NOALIGN) != XFSMNT_NOALIGN) {
		if (dsunit) {
			args->sunit = dsunit;
			args->flags |= XFSMNT_RETERR;
		} else
			args->sunit = vol_dsunit;
		dswidth ? (args->swidth = dswidth) :
			  (args->swidth = vol_dswidth);
	} else
		args->sunit = args->swidth = 0;

	args->logbufs = logbufs;
	args->logbufsize = logbufsize;

	return 0;
}

STATIC int
xfs_showargs(
	struct vfs		*vfsp,
	struct seq_file		*m)
{
	static struct proc_xfs_info {
		int	flag;
		char	*str;
	} xfs_info[] = {
		/* the few simple ones we can get from the mount struct */
		{ XFS_MOUNT_NOALIGN,		"," MNTOPT_NOALIGN },
		{ XFS_MOUNT_NORECOVERY,		"," MNTOPT_NORECOVERY },
		{ XFS_MOUNT_OSYNCISOSYNC,	"," MNTOPT_OSYNCISOSYNC },
		{ XFS_MOUNT_NOUUID,		"," MNTOPT_NOUUID },
		{ 0, NULL }
	};
	struct proc_xfs_info	*xfs_infop;
	struct xfs_mount	*mp = XFS_BHVTOM(vfsp->vfs_fbhv);

	for (xfs_infop = xfs_info; xfs_infop->flag; xfs_infop++) {
		if (mp->m_flags & xfs_infop->flag)
			seq_puts(m, xfs_infop->str);
	}

	if (mp->m_qflags & XFS_UQUOTA_ACCT) {
		(mp->m_qflags & XFS_UQUOTA_ENFD) ?
			seq_puts(m, "," MNTOPT_UQUOTA) :
			seq_puts(m, "," MNTOPT_UQUOTANOENF);
	}

	if (mp->m_qflags & XFS_GQUOTA_ACCT) {
		(mp->m_qflags & XFS_GQUOTA_ENFD) ?
			seq_puts(m, "," MNTOPT_GQUOTA) :
			seq_puts(m, "," MNTOPT_GQUOTANOENF);
	}

	if (mp->m_flags & XFS_MOUNT_DFLT_IOSIZE)
		seq_printf(m, "," MNTOPT_BIOSIZE "=%d", mp->m_writeio_log);

	if (mp->m_logbufs > 0)
		seq_printf(m, "," MNTOPT_LOGBUFS "=%d", mp->m_logbufs);

	if (mp->m_logbsize > 0)
		seq_printf(m, "," MNTOPT_LOGBSIZE "=%d", mp->m_logbsize);

	if (mp->m_ddev_targp->pbr_dev != mp->m_logdev_targp->pbr_dev)
		seq_printf(m, "," MNTOPT_LOGDEV "=%s",
				bdevname(mp->m_logdev_targp->pbr_bdev));

	if (mp->m_rtdev_targp &&
	    mp->m_ddev_targp->pbr_dev != mp->m_rtdev_targp->pbr_dev)
		seq_printf(m, "," MNTOPT_RTDEV "=%s",
				bdevname(mp->m_rtdev_targp->pbr_bdev));

	if (mp->m_dalign > 0)
		seq_printf(m, "," MNTOPT_SUNIT "=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_dalign));

	if (mp->m_swidth > 0)
		seq_printf(m, "," MNTOPT_SWIDTH "=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_swidth));

	if (vfsp->vfs_flag & VFS_DMI)
		seq_puts(m, "," MNTOPT_DMAPI);

	return 0;
}

STATIC __inline__ void
xfs_set_inodeops(
	struct inode		*inode)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);

	if (vp->v_type == VNON) {
		make_bad_inode(inode);
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_op = &linvfs_file_inode_operations;
		inode->i_fop = &linvfs_file_operations;
		inode->i_mapping->a_ops = &linvfs_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &linvfs_dir_inode_operations;
		inode->i_fop = &linvfs_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &linvfs_symlink_inode_operations;
		if (inode->i_blocks)
			inode->i_mapping->a_ops = &linvfs_aops;
	} else {
		inode->i_op = &linvfs_file_inode_operations;
		init_special_inode(inode, inode->i_mode,
					kdev_t_to_nr(inode->i_rdev));
	}
}

STATIC __inline__ void
xfs_revalidate_inode(
	xfs_mount_t	*mp,
	vnode_t		*vp,
	xfs_inode_t	*ip)
{
	struct inode	*inode = LINVFS_GET_IP(vp);

	inode->i_mode	= (ip->i_d.di_mode & MODEMASK) | VTTOIF(vp->v_type);
	inode->i_nlink	= ip->i_d.di_nlink;
	inode->i_uid	= ip->i_d.di_uid;
	inode->i_gid 	= ip->i_d.di_gid;
	if (((1 << vp->v_type) & ((1<<VBLK) | (1<<VCHR))) == 0) {
		inode->i_rdev	= NODEV;
	} else {
		xfs_dev_t dev = ip->i_df.if_u2.if_rdev;
		inode->i_rdev	= XFS_DEV_TO_KDEVT(dev);
	}
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_generation = ip->i_d.di_gen;
	inode->i_size	= ip->i_d.di_size;
	inode->i_blocks =
		XFS_FSB_TO_BB(mp, ip->i_d.di_nblocks + ip->i_delayed_blks);
	inode->i_atime.tv_sec	= ip->i_d.di_atime.t_sec;
	inode->i_atime.tv_nsec	= ip->i_d.di_atime.t_nsec;
	inode->i_mtime.tv_sec	= ip->i_d.di_mtime.t_sec;
	inode->i_mtime.tv_nsec	= ip->i_d.di_mtime.t_nsec;
	inode->i_ctime.tv_sec	= ip->i_d.di_ctime.t_sec;
	inode->i_ctime.tv_nsec	= ip->i_d.di_ctime.t_nsec;

	vp->v_flag &= ~VMODIFIED;
}

void
xfs_initialize_vnode(
	bhv_desc_t	*bdp,
	vnode_t		*vp,
	bhv_desc_t	*inode_bhv,
	int		unlock)
{
	xfs_inode_t	*ip = XFS_BHVTOI(inode_bhv);
	struct inode	*inode = LINVFS_GET_IP(vp);

	if (vp->v_fbhv == NULL) {
		vp->v_vfsp = bhvtovfs(bdp);
		bhv_desc_init(&(ip->i_bhv_desc), ip, vp, &xfs_vnodeops);
		bhv_insert_initial(VN_BHV_HEAD(vp), &(ip->i_bhv_desc));
	}

	vp->v_type = IFTOVT(ip->i_d.di_mode);
	/* Have we been called during the new inode create process,
	 * in which case we are too early to fill in the linux inode.
	 */
	if (vp->v_type == VNON)
		return;

	xfs_revalidate_inode(XFS_BHVTOM(bdp), vp, ip);

	/* For new inodes we need to set the ops vectors,
	 * and unlock the inode.
	 */
	if (unlock && (inode->i_state & I_NEW)) {
		xfs_set_inodeops(inode);
		unlock_new_inode(inode);
	}
}

int
xfs_blkdev_get(
	xfs_mount_t		*mp,
	const char		*name,
	struct block_device	**bdevp)
{
	int			error = 0;

	*bdevp = open_bdev_excl(name, 0, BDEV_FS, mp);
	if (IS_ERR(*bdevp)) {
		error = PTR_ERR(*bdevp);
		printk("XFS: Invalid device [%s], error=%d\n", name, error);
	}

	return -error;
}

void
xfs_blkdev_put(
	struct block_device	*bdev)
{
	if (bdev)
		close_bdev_excl(bdev, BDEV_FS);
}

void
xfs_free_buftarg(
	xfs_buftarg_t		*btp)
{
	pagebuf_delwri_flush(btp, PBDF_WAIT, NULL);
	kfree(btp);
}

void
xfs_relse_buftarg(
	xfs_buftarg_t		*btp)
{
	invalidate_bdev(btp->pbr_bdev, 1);
	truncate_inode_pages(btp->pbr_mapping, 0LL);
}

unsigned int
xfs_getsize_buftarg(
	xfs_buftarg_t		*btp)
{
	return block_size(btp->pbr_bdev);
}

void
xfs_setsize_buftarg(
	xfs_buftarg_t		*btp,
	unsigned int		blocksize,
	unsigned int		sectorsize)
{
	btp->pbr_bsize = blocksize;
	btp->pbr_sshift = ffs(sectorsize) - 1;
	btp->pbr_smask = sectorsize - 1;

	if (set_blocksize(btp->pbr_bdev, sectorsize)) {
		printk(KERN_WARNING
			"XFS: Cannot set_blocksize to %u on device 0x%x\n",
			sectorsize, btp->pbr_dev);
	}
}

xfs_buftarg_t *
xfs_alloc_buftarg(
	struct block_device	*bdev)
{
	xfs_buftarg_t		*btp;

	btp = kmem_zalloc(sizeof(*btp), KM_SLEEP);

	btp->pbr_dev =  bdev->bd_dev;
	btp->pbr_bdev = bdev;
	btp->pbr_mapping = bdev->bd_inode->i_mapping;
	xfs_setsize_buftarg(btp, PAGE_CACHE_SIZE, bdev_hardsect_size(bdev));

	return btp;
}

STATIC kmem_cache_t * linvfs_inode_cachep;

STATIC __inline__ unsigned int gfp_mask(void)
{
	/* If we're not in a transaction, FS activity is ok */
	if (current->flags & PF_FSTRANS) return GFP_NOFS;
	return GFP_KERNEL;
}


STATIC struct inode *
linvfs_alloc_inode(
	struct super_block	*sb)
{
	vnode_t			*vp;

	vp = (vnode_t *)kmem_cache_alloc(linvfs_inode_cachep, gfp_mask());
	if (!vp)
		return NULL;
	return LINVFS_GET_IP(vp);
}

STATIC void
linvfs_destroy_inode(
	struct inode		*inode)
{
	kmem_cache_free(linvfs_inode_cachep, LINVFS_GET_VP(inode));
}

STATIC void
init_once(
	void			*data,
	kmem_cache_t		*cachep,
	unsigned long		flags)
{
	vnode_t			*vp = (vnode_t *)data;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(LINVFS_GET_IP(vp));
}

STATIC int
init_inodecache( void )
{
	linvfs_inode_cachep = kmem_cache_create("linvfs_icache",
				sizeof(vnode_t), 0, SLAB_HWCACHE_ALIGN,
				init_once, NULL);

	if (linvfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

STATIC void
destroy_inodecache( void )
{
	if (kmem_cache_destroy(linvfs_inode_cachep))
		printk(KERN_INFO
			"linvfs_inode_cache: not all structures were freed\n");
}

static int
linvfs_fill_super(
	struct super_block	*sb,
	void			*data,
	int			silent)
{
	vfs_t			*vfsp;
	vfsops_t		*vfsops;
	vnode_t			*rootvp;
	struct inode		*ip;
	struct xfs_mount_args	*args;
	struct statfs		statvfs;
	int			error = EINVAL;

	args = kmalloc(sizeof(struct xfs_mount_args), GFP_KERNEL);
	if (!args)
		return  -EINVAL;
	memset(args, 0, sizeof(struct xfs_mount_args));
	args->slcount = args->stimeout = args->ctimeout = -1;
	strncpy(args->fsname, sb->s_id, MAXNAMELEN);
	if (xfs_parseargs((char *)data, sb->s_flags, args))
		goto out_null;

	/*  Kludge in XFS until we have other VFS/VNODE FSs  */
	vfsops = &xfs_vfsops;

	/*  Set up the vfs_t structure	*/
	vfsp = vfs_allocate();
	if (!vfsp) {
		error = ENOMEM;
		goto out_null;
	}

	if (sb->s_flags & MS_RDONLY)
		vfsp->vfs_flag |= VFS_RDONLY;

	vfsp->vfs_super = sb;
	set_max_bytes(sb);
	set_quota_ops(sb);
	sb->s_op = &linvfs_sops;
	sb->s_export_op = &linvfs_export_ops;

	sb_min_blocksize(sb, BBSIZE);

	LINVFS_SET_VFS(sb, vfsp);

	VFSOPS_MOUNT(vfsops, vfsp, args, NULL, error);
	if (error)
		goto fail_vfsop;

	VFS_STATVFS(vfsp, &statvfs, NULL, error);
	if (error)
		goto fail_unmount;

	sb->s_magic = XFS_SB_MAGIC;
	sb->s_dirt = 1;
	sb->s_blocksize = statvfs.f_bsize;
	sb->s_blocksize_bits = ffs(statvfs.f_bsize) - 1;
	set_posix_acl(sb);

	VFS_ROOT(vfsp, &rootvp, error);
	if (error)
		goto fail_unmount;

	ip = LINVFS_GET_IP(rootvp);

	sb->s_root = d_alloc_root(ip);
	if (!sb->s_root)
		goto fail_vnrele;
	if (is_bad_inode(sb->s_root->d_inode))
		goto fail_vnrele;

	/* Don't set the VFS_DMI flag until here because we don't want
	 * to send events while replaying the log.
	 */
	if (args->flags & XFSMNT_DMAPI) {
		vfsp->vfs_flag |= VFS_DMI;
		VFSOPS_DMAPI_MOUNT(vfsops, vfsp, args->mtpt, args->fsname,
				   error);

		if (error) {
			if (atomic_read(&sb->s_active) == 1)
				vfsp->vfs_flag &= ~VFS_DMI;
			goto fail_vnrele;
		}
	}

	vn_trace_exit(rootvp, __FUNCTION__, (inst_t *)__return_address);

	kfree(args);
	return 0;

fail_vnrele:
	if (sb->s_root) {
		dput(sb->s_root);
		sb->s_root = NULL;
	} else {
		VN_RELE(rootvp);
	}

fail_unmount:
	VFS_UNMOUNT(vfsp, 0, NULL, error);

fail_vfsop:
	vfs_deallocate(vfsp);

out_null:
	kfree(args);
	return -error;
}

/*
 * We do not actually write the inode here, just mark the
 * super block dirty so that sync_supers calls us and
 * forces the flush.
 */
STATIC void
linvfs_write_inode(
	struct inode		*inode,
	int			sync)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);
	int			error, flags = FLUSH_INODE;

	if (vp) {
		vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);
		if (sync)
			flags |= FLUSH_SYNC;
		VOP_IFLUSH(vp, flags, error);
		if (error == EAGAIN)
			inode->i_sb->s_dirt = 1;
	}
}

STATIC void
linvfs_clear_inode(
	struct inode		*inode)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);

	if (vp) {
		vn_rele(vp);
		vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);
		/*
		 * Do all our cleanup, and remove this vnode.
		 */
		vn_remove(vp);
	}
}

STATIC void
linvfs_put_super(
	struct super_block	*sb)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	VFS_SYNC(vfsp, SYNC_ATTR|SYNC_DELWRI, NULL, error);
	if (error == 0)
		VFS_UNMOUNT(vfsp, 0, NULL, error);
	if (error) {
		printk("XFS unmount got error %d\n", error);
		printk("%s: vfsp/0x%p left dangling!\n", __FUNCTION__, vfsp);
		return;
	}

	vfs_deallocate(vfsp);
}

STATIC void
linvfs_write_super(
	struct super_block	*sb)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	sb->s_dirt = 0;
	if (sb->s_flags & MS_RDONLY)
		return;
	VFS_SYNC(vfsp, SYNC_FSDATA|SYNC_BDFLUSH|SYNC_ATTR,
		NULL, error);
}

STATIC int
linvfs_statfs(
	struct super_block	*sb,
	struct statfs		*statp)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	VFS_STATVFS(vfsp, statp, NULL, error);
	return error;
}

STATIC int
linvfs_remount(
	struct super_block	*sb,
	int			*flags,
	char			*options)
{
	struct xfs_mount_args	*args;
	vfs_t			*vfsp;
	xfs_mount_t		*mp;
	int			error = 0;

	vfsp = LINVFS_GET_VFS(sb);
	mp = XFS_BHVTOM(vfsp->vfs_fbhv);

	args = kmalloc(sizeof(struct xfs_mount_args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;
	memset(args, 0, sizeof(struct xfs_mount_args));
	args->slcount = args->stimeout = args->ctimeout = -1;
	if (xfs_parseargs(options, *flags, args)) {
		error = -EINVAL;
		goto out;
	}

	if (args->flags & XFSMNT_NOATIME)
		mp->m_flags |= XFS_MOUNT_NOATIME;
	else
		mp->m_flags &= ~XFS_MOUNT_NOATIME;

	set_posix_acl(sb);
	linvfs_write_super(sb);

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		goto out;

	if (*flags & MS_RDONLY) {
		sb->s_flags |= MS_RDONLY;
		XFS_log_write_unmount_ro(vfsp->vfs_fbhv);
		vfsp->vfs_flag |= VFS_RDONLY;
	} else {
		vfsp->vfs_flag &= ~VFS_RDONLY;
	}

out:
	kfree(args);
	return error;
}

STATIC void
linvfs_freeze_fs(
	struct super_block	*sb)
{
	vfs_t			*vfsp;
	vnode_t			*vp;
	int			error;

	vfsp = LINVFS_GET_VFS(sb);
	if (sb->s_flags & MS_RDONLY)
		return;
	VFS_ROOT(vfsp, &vp, error);
	VOP_IOCTL(vp, LINVFS_GET_IP(vp), NULL, XFS_IOC_FREEZE, 0, error);
	VN_RELE(vp);
}

STATIC void
linvfs_unfreeze_fs(
	struct super_block	*sb)
{
	vfs_t			*vfsp;
	vnode_t			*vp;
	int			error;

	vfsp = LINVFS_GET_VFS(sb);
	VFS_ROOT(vfsp, &vp, error);
	VOP_IOCTL(vp, LINVFS_GET_IP(vp), NULL, XFS_IOC_THAW, 0, error);
	VN_RELE(vp);
}

STATIC struct dentry *
linvfs_get_parent(
	struct dentry		*child)
{
	int			error;
	vnode_t			*vp, *cvp;
	struct dentry		*parent;
	struct inode		*ip = NULL;
	struct dentry		dotdot;

	dotdot.d_name.name = "..";
	dotdot.d_name.len = 2;
	dotdot.d_inode = 0;

	cvp = NULL;
	vp = LINVFS_GET_VP(child->d_inode);
	VOP_LOOKUP(vp, &dotdot, &cvp, 0, NULL, NULL, error);

	if (!error) {
		ASSERT(cvp);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			VN_RELE(cvp);
			return ERR_PTR(-EACCES);
		}
	}
	if (error)
		return ERR_PTR(-error);
	parent = d_alloc_anon(ip);
	if (!parent) {
		VN_RELE(cvp);
		parent = ERR_PTR(-ENOMEM);
	}
	return parent;
}

STATIC struct dentry *
linvfs_get_dentry(
	struct super_block	*sb,
	void			*data)
{
	vnode_t			*vp;
	struct inode		*inode;
	struct dentry		*result;
	xfs_fid2_t		xfid;
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	xfid.fid_len = sizeof(xfs_fid2_t) - sizeof(xfid.fid_len);
	xfid.fid_pad = 0;
	xfid.fid_gen = ((__u32 *)data)[1];
	xfid.fid_ino = ((__u32 *)data)[0];

	VFS_VGET(vfsp, &vp, (fid_t *)&xfid, error);
	if (error || vp == NULL)
		return ERR_PTR(-ESTALE) ;

	inode = LINVFS_GET_IP(vp);
	result = d_alloc_anon(inode);
        if (!result) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	result->d_vfs_flags |= DCACHE_REFERENCED;
	return result;
}

STATIC struct super_block *
linvfs_get_sb(
	struct file_system_type	*fs_type,
	int			flags,
	char			*dev_name,
	void			*data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, linvfs_fill_super);
}

STATIC int
linvfs_show_options(
	struct seq_file		*m,
	struct vfsmount		*mnt)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(mnt->mnt_sb);

	return xfs_showargs(vfsp, m);
}

STATIC struct export_operations linvfs_export_ops = {
	.get_parent		= linvfs_get_parent,
	.get_dentry		= linvfs_get_dentry,
};

STATIC struct super_operations linvfs_sops = {
	.alloc_inode		= linvfs_alloc_inode,
	.destroy_inode		= linvfs_destroy_inode,
	.write_inode		= linvfs_write_inode,
	.clear_inode		= linvfs_clear_inode,
	.put_super		= linvfs_put_super,
	.write_super		= linvfs_write_super,
	.write_super_lockfs	= linvfs_freeze_fs,
	.unlockfs		= linvfs_unfreeze_fs,
	.statfs			= linvfs_statfs,
	.remount_fs		= linvfs_remount,
	.show_options		= linvfs_show_options,
};

STATIC struct file_system_type xfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "xfs",
	.get_sb			= linvfs_get_sb,
	.kill_sb		= kill_block_super,
	.fs_flags		= FS_REQUIRES_DEV,
};

STATIC int __init
init_xfs_fs( void )
{
	int			error;
	struct sysinfo		si;
	static char		message[] __initdata =
		KERN_INFO "SGI XFS " XFS_VERSION_STRING " with " 
		XFS_BUILD_OPTIONS " enabled\n";

	printk(message);

	error = init_inodecache();
	if (error < 0)
		return error;

	si_meminfo(&si);
	xfs_physmem = si.totalram;

	error = pagebuf_init();
	if (error < 0)
		goto out;

	vn_init();
	xfs_init();
	dmapi_init();

	error = register_filesystem(&xfs_fs_type);
	if (error)
		goto out;
	return 0;

out:
	destroy_inodecache();
	return error;
}

STATIC void __exit
exit_xfs_fs( void )
{
	dmapi_uninit();
	xfs_cleanup();
	unregister_filesystem(&xfs_fs_type);
	pagebuf_terminate();
	destroy_inodecache();
}

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("SGI <sgi.com>");
MODULE_DESCRIPTION("SGI XFS " XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");

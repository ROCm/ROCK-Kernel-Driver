/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef __XFS_QUOTA_H__
#define __XFS_QUOTA_H__

/*
 * uid_t and gid_t are hard-coded to 32 bits in the inode.
 * Hence, an 'id' in a dquot is 32 bits..
 */
typedef __int32_t	xfs_dqid_t;

/*
 * Eventhough users may not have quota limits occupying all 64-bits,
 * they may need 64-bit accounting. Hence, 64-bit quota-counters,
 * and quota-limits. This is a waste in the common case, but hey ...
 */
typedef __uint64_t	xfs_qcnt_t;
typedef __uint16_t	xfs_qwarncnt_t;

/*
 * Disk quotas status in m_qflags, and also sb_qflags. 16 bits.
 */
#define XFS_UQUOTA_ACCT 0x0001	/* user quota accounting ON */
#define XFS_UQUOTA_ENFD 0x0002	/* user quota limits enforced */
#define XFS_UQUOTA_CHKD 0x0004	/* quotacheck run on usr quotas */
#define XFS_PQUOTA_ACCT 0x0008	/* (IRIX) project quota accounting ON */
#define XFS_GQUOTA_ENFD 0x0010	/* group quota limits enforced */
#define XFS_GQUOTA_CHKD 0x0020	/* quotacheck run on grp quotas */
#define XFS_GQUOTA_ACCT 0x0040	/* group quota accounting ON */

/*
 * Incore only flags for quotaoff - these bits get cleared when quota(s)
 * are in the process of getting turned off. These flags are in m_qflags but
 * never in sb_qflags.
 */
#define XFS_UQUOTA_ACTIVE	0x0080	/* uquotas are being turned off */
#define XFS_GQUOTA_ACTIVE	0x0100	/* gquotas are being turned off */

/*
 * Checking XFS_IS_*QUOTA_ON() while holding any inode lock guarantees
 * quota will be not be switched off as long as that inode lock is held.
 */
#define XFS_IS_QUOTA_ON(mp)	((mp)->m_qflags & (XFS_UQUOTA_ACTIVE | \
						   XFS_GQUOTA_ACTIVE))
#define XFS_IS_UQUOTA_ON(mp)	((mp)->m_qflags & XFS_UQUOTA_ACTIVE)
#define XFS_IS_GQUOTA_ON(mp)	((mp)->m_qflags & XFS_GQUOTA_ACTIVE)

/*
 * Flags to tell various functions what to do. Not all of these are meaningful
 * to a single function. None of these XFS_QMOPT_* flags are meant to have
 * persistent values (ie. their values can and will change between versions)
 */
#define XFS_QMOPT_DQLOCK	0x0000001 /* dqlock */
#define XFS_QMOPT_DQALLOC	0x0000002 /* alloc dquot ondisk if needed */
#define XFS_QMOPT_UQUOTA	0x0000004 /* user dquot requested */
#define XFS_QMOPT_GQUOTA	0x0000008 /* group dquot requested */
#define XFS_QMOPT_FORCE_RES	0x0000010 /* ignore quota limits */
#define XFS_QMOPT_DQSUSER	0x0000020 /* don't cache super users dquot */
#define XFS_QMOPT_SBVERSION	0x0000040 /* change superblock version num */
#define XFS_QMOPT_QUOTAOFF	0x0000080 /* quotas are being turned off */
#define XFS_QMOPT_UMOUNTING	0x0000100 /* filesys is being unmounted */
#define XFS_QMOPT_DOLOG		0x0000200 /* log buf changes (in quotacheck) */
#define XFS_QMOPT_DOWARN	0x0000400 /* increase warning cnt if necessary */
#define XFS_QMOPT_ILOCKED	0x0000800 /* inode is already locked (excl) */
#define XFS_QMOPT_DQREPAIR	0x0001000 /* repair dquot, if damaged. */

/*
 * flags to xfs_trans_mod_dquot to indicate which field needs to be
 * modified.
 */
#define XFS_QMOPT_RES_REGBLKS	0x0010000
#define XFS_QMOPT_RES_RTBLKS	0x0020000
#define XFS_QMOPT_BCOUNT	0x0040000
#define XFS_QMOPT_ICOUNT	0x0080000
#define XFS_QMOPT_RTBCOUNT	0x0100000
#define XFS_QMOPT_DELBCOUNT	0x0200000
#define XFS_QMOPT_DELRTBCOUNT	0x0400000
#define XFS_QMOPT_RES_INOS	0x0800000

/*
 * flags for dqflush and dqflush_all.
 */
#define XFS_QMOPT_SYNC		0x1000000
#define XFS_QMOPT_ASYNC		0x2000000
#define XFS_QMOPT_DELWRI	0x4000000

/*
 * flags for dqalloc.
 */
#define XFS_QMOPT_INHERIT	0x8000000

/*
 * flags to xfs_trans_mod_dquot.
 */
#define XFS_TRANS_DQ_RES_BLKS	XFS_QMOPT_RES_REGBLKS
#define XFS_TRANS_DQ_RES_RTBLKS XFS_QMOPT_RES_RTBLKS
#define XFS_TRANS_DQ_RES_INOS	XFS_QMOPT_RES_INOS
#define XFS_TRANS_DQ_BCOUNT	XFS_QMOPT_BCOUNT
#define XFS_TRANS_DQ_DELBCOUNT	XFS_QMOPT_DELBCOUNT
#define XFS_TRANS_DQ_ICOUNT	XFS_QMOPT_ICOUNT
#define XFS_TRANS_DQ_RTBCOUNT	XFS_QMOPT_RTBCOUNT
#define XFS_TRANS_DQ_DELRTBCOUNT XFS_QMOPT_DELRTBCOUNT


#define XFS_QMOPT_QUOTALL	(XFS_QMOPT_UQUOTA|XFS_QMOPT_GQUOTA)
#define XFS_QMOPT_RESBLK_MASK	(XFS_QMOPT_RES_REGBLKS | XFS_QMOPT_RES_RTBLKS)

/*
 * This check is done typically without holding the inode lock;
 * that may seem racey, but it is harmless in the context that it is used.
 * The inode cannot go inactive as long a reference is kept, and
 * therefore if dquot(s) were attached, they'll stay consistent.
 * If, for example, the ownership of the inode changes while
 * we didnt have the inode locked, the appropriate dquot(s) will be
 * attached atomically.
 */
#define XFS_NOT_DQATTACHED(mp, ip) ((XFS_IS_UQUOTA_ON(mp) &&\
				     (ip)->i_udquot == NULL) || \
				    (XFS_IS_GQUOTA_ON(mp) && \
				     (ip)->i_gdquot == NULL))

#define XFS_QM_NEED_QUOTACHECK(mp) ((XFS_IS_UQUOTA_ON(mp) && \
				     (mp->m_sb.sb_qflags & \
				      XFS_UQUOTA_CHKD) == 0) || \
				    (XFS_IS_GQUOTA_ON(mp) && \
				     (mp->m_sb.sb_qflags & \
				      XFS_GQUOTA_CHKD) == 0))

#define XFS_MOUNT_QUOTA_ALL	(XFS_UQUOTA_ACCT|XFS_UQUOTA_ENFD|\
				 XFS_UQUOTA_CHKD|XFS_GQUOTA_ACCT|\
				 XFS_GQUOTA_ENFD|XFS_GQUOTA_CHKD)
#define XFS_MOUNT_QUOTA_MASK	(XFS_MOUNT_QUOTA_ALL | XFS_UQUOTA_ACTIVE | \
				 XFS_GQUOTA_ACTIVE)

#define XFS_IS_REALTIME_INODE(ip) ((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME)


#ifdef __KERNEL__

#ifdef CONFIG_XFS_QUOTA
/*
 * External Interface to the XFS disk quota subsystem.
 */
struct	xfs_disk_dquot;
struct	xfs_dqhash;
struct	xfs_dquot;
struct	xfs_inode;
struct	xfs_mount;
struct	xfs_trans;

/*
 * Quota Manager Interface.
 */
extern struct xfs_qm   *xfs_qm_init(void);
extern void		xfs_qm_destroy(struct xfs_qm *);
extern int		xfs_qm_dqflush_all(struct xfs_mount *, int);
extern int		xfs_qm_dqattach(struct xfs_inode *, uint);
extern int		xfs_qm_dqpurge_all(struct xfs_mount *, uint);
extern void		xfs_qm_mount_quotainit(struct xfs_mount *, uint);
extern void		xfs_qm_unmount_quotadestroy(struct xfs_mount *);
extern int		xfs_qm_mount_quotas(struct xfs_mount *);
extern int		xfs_qm_unmount_quotas(struct xfs_mount *);
extern void		xfs_qm_dqdettach_inode(struct xfs_inode *);
extern int		xfs_qm_sync(struct xfs_mount *, short);

/*
 * Dquot interface.
 */
extern void		xfs_dqlock(struct xfs_dquot *);
extern void		xfs_dqunlock(struct xfs_dquot *);
extern void		xfs_dqunlock_nonotify(struct xfs_dquot *);
extern void		xfs_dqlock2(struct xfs_dquot *, struct xfs_dquot *);
extern void		xfs_qm_dqput(struct xfs_dquot *);
extern void		xfs_qm_dqrele(struct xfs_dquot *);
extern xfs_dqid_t	xfs_qm_dqid(struct xfs_dquot *);
extern int		xfs_qm_dqget(struct xfs_mount *,
				     struct xfs_inode *, xfs_dqid_t,
				      uint, uint, struct xfs_dquot **);
extern int		xfs_qm_dqcheck(struct xfs_disk_dquot *,
				       xfs_dqid_t, uint, uint, char *);

/*
 * Vnodeops specific code that should actually be _in_ xfs_vnodeops.c, but
 * is here because it's nicer to keep vnodeops (therefore, XFS) lean
 * and clean.
 */
extern struct xfs_dquot *	xfs_qm_vop_chown(struct xfs_trans *,
						 struct xfs_inode *,
						 struct xfs_dquot **,
						 struct xfs_dquot *);
extern int		xfs_qm_vop_dqalloc(struct xfs_mount *,
					   struct xfs_inode *,
					   uid_t, gid_t, uint,
					   struct xfs_dquot	**,
					   struct xfs_dquot	**);

extern int		xfs_qm_vop_chown_reserve(struct xfs_trans *,
						 struct xfs_inode *,
						 struct xfs_dquot *,
						 struct xfs_dquot *,
						 uint);

extern int		xfs_qm_vop_rename_dqattach(struct xfs_inode **);
extern void		xfs_qm_vop_dqattach_and_dqmod_newinode(
						struct xfs_trans *,
						struct xfs_inode *,
						struct xfs_dquot *,
						struct xfs_dquot *);


/*
 * Dquot Transaction interface
 */
extern void		xfs_trans_alloc_dqinfo(struct xfs_trans *);
extern void		xfs_trans_free_dqinfo(struct xfs_trans *);
extern void		xfs_trans_dup_dqinfo(struct xfs_trans *,
					     struct xfs_trans *);
extern void		xfs_trans_mod_dquot(struct xfs_trans *,
					    struct xfs_dquot *,
					    uint, long);
extern void		xfs_trans_mod_dquot_byino(struct xfs_trans *,
						  struct xfs_inode *,
						  uint, long);
extern void		xfs_trans_apply_dquot_deltas(struct xfs_trans *);
extern void		xfs_trans_unreserve_and_mod_dquots(struct xfs_trans *);

extern int		xfs_trans_reserve_quota_nblks(struct xfs_trans *,
						      struct xfs_inode *,
						      long, long, uint);


extern int		xfs_trans_reserve_quota_bydquots(struct xfs_trans *,
							 struct xfs_dquot *,
							 struct xfs_dquot *,
							 long, long, uint);
extern void		xfs_trans_log_dquot(struct xfs_trans *,
					    struct xfs_dquot *);
extern void		xfs_trans_dqjoin(struct xfs_trans *,
					 struct xfs_dquot *);
extern void		xfs_qm_dqrele_all_inodes(struct xfs_mount *, uint);

# define _XQM_ZONE_DESTROY(z)	((z)? kmem_cache_destroy(z) : (void)0)

#else
# define xfs_qm_init()					(NULL)
# define xfs_qm_destroy(xqm)				do { } while (0)
# define xfs_qm_dqflush_all(m,t)			(ENOSYS)
# define xfs_qm_dqattach(i,t)				(ENOSYS)
# define xfs_qm_dqpurge_all(m,t)			(ENOSYS)
# define xfs_qm_mount_quotainit(m,t)			do { } while (0)
# define xfs_qm_unmount_quotadestroy(m)			do { } while (0)
# define xfs_qm_mount_quotas(m)				(ENOSYS)
# define xfs_qm_unmount_quotas(m)			(ENOSYS)
# define xfs_qm_dqdettach_inode(i)			do { } while (0)
# define xfs_qm_sync(m,t)				(ENOSYS)
# define xfs_dqlock(d)					do { } while (0)
# define xfs_dqunlock(d)				do { } while (0)
# define xfs_dqunlock_nonotify(d)			do { } while (0)
# define xfs_dqlock2(d1,d2)				do { } while (0)
# define xfs_qm_dqput(d)				do { } while (0)
# define xfs_qm_dqrele(d)				do { } while (0)
# define xfs_qm_dqid(d)					(-1)
# define xfs_qm_dqget(m,i,di,t,f,d)			(ENOSYS)
# define xfs_qm_dqcheck(dd,di,t,f,s)			(ENOSYS)
# define xfs_trans_alloc_dqinfo(t)			do { } while (0)
# define xfs_trans_free_dqinfo(t)			do { } while (0)
# define xfs_trans_dup_dqinfo(t1,t2)			do { } while (0)
# define xfs_trans_mod_dquot(t,d,f,x)			do { } while (0)
# define xfs_trans_mod_dquot_byino(t,i,f,x)		do { } while (0)
# define xfs_trans_apply_dquot_deltas(t)		do { } while (0)
# define xfs_trans_unreserve_and_mod_dquots(t)		do { } while (0)
# define xfs_trans_reserve_quota_nblks(t,i,nb,ni,f)	(ENOSYS)
# define xfs_trans_reserve_quota_bydquots(t,x,y,b,i,f)	(ENOSYS)
# define xfs_trans_log_dquot(t,d)			do { } while (0)
# define xfs_trans_dqjoin(t,d)				do { } while (0)
# define xfs_qm_dqrele_all_inodes(m,t)			do { } while (0)
# define xfs_qm_vop_chown(t,i,d1,d2)			(NULL)
# define xfs_qm_vop_dqalloc(m,i,u,g,f,d1,d2)		(ENOSYS)
# define xfs_qm_vop_chown_reserve(t,i,d1,d2,f)		(ENOSYS)
# define xfs_qm_vop_rename_dqattach(i)			(ENOSYS)
# define xfs_qm_vop_dqattach_and_dqmod_newinode(t,i,x,y) do { } while (0)
# define _XQM_ZONE_DESTROY(z)				do { } while (0)
#endif	/* CONFIG_XFS_QUOTA */

/*
 * Regular disk block quota reservations
 */
#define		xfs_trans_reserve_blkquota(tp, ip, nblks) \
xfs_trans_reserve_quota_nblks(tp, ip, nblks, 0, XFS_QMOPT_RES_REGBLKS)

#define		xfs_trans_reserve_blkquota_force(tp, ip, nblks) \
xfs_trans_reserve_quota_nblks(tp, ip, nblks, 0, \
		XFS_QMOPT_RES_REGBLKS|XFS_QMOPT_FORCE_RES)

#define		xfs_trans_unreserve_blkquota(tp, ip, nblks) \
(void)xfs_trans_reserve_quota_nblks(tp, ip, -(nblks), 0, XFS_QMOPT_RES_REGBLKS)

#define		xfs_trans_reserve_quota(tp, udq, gdq, nb, ni, f) \
xfs_trans_reserve_quota_bydquots(tp, udq, gdq, nb, ni, f|XFS_QMOPT_RES_REGBLKS)

#define		xfs_trans_unreserve_quota(tp, ud, gd, b, i, f) \
xfs_trans_reserve_quota_bydquots(tp, ud, gd, -(b), -(i), f|XFS_QMOPT_RES_REGBLKS)

/*
 * Realtime disk block quota reservations
 */
#define		xfs_trans_reserve_rtblkquota(mp, tp, ip, nblks) \
xfs_trans_reserve_quota_nblks(tp, ip, nblks, 0, XFS_QMOPT_RES_RTBLKS)

#define		xfs_trans_unreserve_rtblkquota(tp, ip, nblks) \
(void)xfs_trans_reserve_quota_nblks(tp, ip, -(nblks), 0, XFS_QMOPT_RES_RTBLKS)

#define		xfs_trans_reserve_rtquota(mp, tp, uq, pq, blks, f) \
xfs_trans_reserve_quota_bydquots(mp, tp, uq, pq, blks, 0, f|XFS_QMOPT_RES_RTBLKS)

#define		xfs_trans_unreserve_rtquota(tp, uq, pq, blks) \
xfs_trans_reserve_quota_bydquots(tp, uq, pq, -(blks), XFS_QMOPT_RES_RTBLKS)


#endif	/* __KERNEL__ */

#endif	/* __XFS_QUOTA_H__ */

/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
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

#define MNTOPT_QUOTA	"quota"		/* disk quotas (user) */
#define MNTOPT_NOQUOTA	"noquota"	/* no quotas */
#define MNTOPT_USRQUOTA	"usrquota"	/* user quota enabled */
#define MNTOPT_GRPQUOTA	"grpquota"	/* group quota enabled */
#define MNTOPT_UQUOTA	"uquota"	/* user quota (IRIX variant) */
#define MNTOPT_GQUOTA	"gquota"	/* group quota (IRIX variant) */
#define MNTOPT_UQUOTANOENF "uqnoenforce"/* user quota limit enforcement */
#define MNTOPT_GQUOTANOENF "gqnoenforce"/* group quota limit enforcement */
#define MNTOPT_QUOTANOENF  "qnoenforce"	/* same as uqnoenforce */

STATIC int
xfs_qm_parseargs(
	struct bhv_desc		*bhv,
	char			*options,
	struct xfs_mount_args	*args,
	int			update)
{
	size_t			length;
	char			*local_options = options;
	char			*this_char;
	int			error;
	int			referenced = update;

	while ((this_char = strsep(&local_options, ",")) != NULL) {
		length = strlen(this_char);
		if (local_options)
			length++;

		if (!strcmp(this_char, MNTOPT_NOQUOTA)) {
			args->flags &= ~(XFSMNT_UQUOTAENF|XFSMNT_UQUOTA);
			args->flags &= ~(XFSMNT_GQUOTAENF|XFSMNT_GQUOTA);
			referenced = update;
		} else if (!strcmp(this_char, MNTOPT_QUOTA) ||
			   !strcmp(this_char, MNTOPT_UQUOTA) ||
			   !strcmp(this_char, MNTOPT_USRQUOTA)) {
			args->flags |= XFSMNT_UQUOTA | XFSMNT_UQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_QUOTANOENF) ||
			   !strcmp(this_char, MNTOPT_UQUOTANOENF)) {
			args->flags |= XFSMNT_UQUOTA;
			args->flags &= ~XFSMNT_UQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_GQUOTA) ||
			   !strcmp(this_char, MNTOPT_GRPQUOTA)) {
			args->flags |= XFSMNT_GQUOTA | XFSMNT_GQUOTAENF;
			referenced = 1;
		} else if (!strcmp(this_char, MNTOPT_GQUOTANOENF)) {
			args->flags |= XFSMNT_GQUOTA;
			args->flags &= ~XFSMNT_GQUOTAENF;
			referenced = 1;
		} else {
			if (local_options)
				*(local_options-1) = ',';
			continue;
		}

		while (length--)
			*this_char++ = ',';
	}

	PVFS_PARSEARGS(BHV_NEXT(bhv), options, args, update, error);
	if (!error && !referenced)
		bhv_remove_vfsops(bhvtovfs(bhv), VFS_POSITION_QM);
	return error;
}

STATIC int
xfs_qm_showargs(
	struct bhv_desc		*bhv,
	struct seq_file		*m)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	struct xfs_mount	*mp = XFS_VFSTOM(vfsp);
	int			error;

	if (mp->m_qflags & XFS_UQUOTA_ACCT) {
		(mp->m_qflags & XFS_UQUOTA_ENFD) ?
			seq_puts(m, "," MNTOPT_USRQUOTA) :
			seq_puts(m, "," MNTOPT_UQUOTANOENF);
	}

	if (mp->m_qflags & XFS_GQUOTA_ACCT) {
		(mp->m_qflags & XFS_GQUOTA_ENFD) ?
			seq_puts(m, "," MNTOPT_GRPQUOTA) :
			seq_puts(m, "," MNTOPT_GQUOTANOENF);
	}

	if (!(mp->m_qflags & (XFS_UQUOTA_ACCT|XFS_GQUOTA_ACCT)))
		seq_puts(m, "," MNTOPT_NOQUOTA);

	PVFS_SHOWARGS(BHV_NEXT(bhv), m, error);
	return error;
}

STATIC int
xfs_qm_mount(
	struct bhv_desc		*bhv,
	struct xfs_mount_args	*args,
	struct cred		*cr)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	struct xfs_mount	*mp = XFS_VFSTOM(vfsp);
	int			error;

	if (args->flags & (XFSMNT_UQUOTA | XFSMNT_GQUOTA))
		xfs_qm_mount_quotainit(mp, args->flags);
	PVFS_MOUNT(BHV_NEXT(bhv), args, cr, error);
	return error;
}

STATIC int
xfs_qm_syncall(
	struct bhv_desc		*bhv,
	int			flags,
	cred_t			*credp)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	struct xfs_mount	*mp = XFS_VFSTOM(vfsp);
	int			error;

	/*
	 * Get the Quota Manager to flush the dquots.
	 */
	if (XFS_IS_QUOTA_ON(mp)) {
		if ((error = xfs_qm_sync(mp, flags))) {
			/*
			 * If we got an IO error, we will be shutting down.
			 * So, there's nothing more for us to do here.
			 */
			ASSERT(error != EIO || XFS_FORCED_SHUTDOWN(mp));
			if (XFS_FORCED_SHUTDOWN(mp)) {
				return XFS_ERROR(error);
			}
		}
	}
	PVFS_SYNC(BHV_NEXT(bhv), flags, credp, error);
	return error;
}


vfsops_t xfs_qmops_xfs = {
	BHV_IDENTITY_INIT(VFS_BHV_QM, VFS_POSITION_QM),
	.vfs_parseargs		= xfs_qm_parseargs,
	.vfs_showargs		= xfs_qm_showargs,
	.vfs_mount		= xfs_qm_mount,
	.vfs_sync		= xfs_qm_syncall,
	.vfs_quotactl		= xfs_qm_quotactl,
};
